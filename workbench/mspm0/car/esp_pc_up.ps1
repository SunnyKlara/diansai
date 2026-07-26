# esp_pc_up.ps1 - bring up the PC-side ESP-01S (module B) so that its COM port becomes a
#                 TRANSPARENT serial link to the car's MCU. Run this ONCE per debug session.
#
#   After this script succeeds, COM<B> behaves exactly like a wire to the car's UART, so ALL the
#   existing tools work UNCHANGED, just point them at module B's port:
#       read_serial.ps1  -Port COM6
#       uart_send.ps1    -Port COM6 -Cmd "m3"
#       tune_step.ps1 / pos_step.ps1 / disturb_test.ps1  -Port COM6
#
# Why module B is NOT set to auto-passthrough (no AT+SAVETRANSLINK) while module A (car) is:
#   * AT+SLEEP is NOT persisted across reboot, and modem-sleep costs ~4x latency jitter
#     (measured 2026-07-27: std 28.1ms / max 214ms with sleep, std 7.5ms / max 38.9ms without).
#     So B must be told AT+SLEEP=0 on every power-up -> it needs an AT session anyway.
#   * B sits on the PC, so running a setup script costs nothing; the car has nobody to talk AT to it.
#   * Keeping B in AT mode by default means it stays inspectable (RSSI, link state) and can never
#     get stuck in a state that only '+++' can undo.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_pc_up.ps1            # defaults
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_pc_up.ps1 -Port COM6 -Verify
#
# To put B back into plain AT mode:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_at.ps1 -Port COM6 -Escape -Cmds "AT+CIPMODE=0;AT+CIPCLOSE"
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM6",
    [int]$Baud       = 115200,
    [string]$Ssid    = "DIANSAI_CAR",
    [string]$Pwd     = "car2026wifi",
    [string]$CarIp   = "192.168.4.1",
    [int]$UdpPort    = 3333,
    [switch]$Verify,                     # listen a moment afterwards and show whatever the car sends
    [int]$VerifyMs   = 3000,
    [string]$Out     = "esp_pc_up_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }
function Clean([string]$s) { return ($s -replace "`r`n", " | ").Trim() }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout  = 200
$sp.WriteTimeout = 1000
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false

function AT([string]$cmd, [int]$ms) {
    $sp.Write($cmd + "`r`n")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 30
    }
    return $rx.ToString()
}
function Drain([int]$ms) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 20
    }
    return $rx.ToString()
}

L "==== esp_pc_up  $Port @ $Baud -> UDP $CarIp`:$UdpPort   $(Get-Date -Format 'HH:mm:ss') ===="
try { $sp.Open() } catch {
    L "OPEN_FAIL: $($_.Exception.Message)"
    L "  (another tool may still hold $Port - close it first)"
    Set-Content $Out $log.ToString() -Encoding ASCII
    exit 1
}
Start-Sleep -Milliseconds 250

# step 0: escape any leftover passthrough (harmless if already in AT mode)
Start-Sleep -Milliseconds 1100
try { $sp.Write("+++") } catch {}
Start-Sleep -Milliseconds 1100
try { $sp.Write("`r`n") } catch {}
Start-Sleep -Milliseconds 300
[void](Drain 300)

$bad = 0

$r = AT "AT" 700
if ($r -match "OK") { L "[1/7] AT alive              OK" } else { L "[1/7] AT alive              FAIL -> $(Clean $r)"; $bad++ }

[void](AT "AT+CWMODE_DEF=1" 600)
[void](AT "AT+CWAUTOCONN=1" 500)
L "[2/7] station mode          set"

$r = AT "AT+CWJAP?" 900
if ($r -match [regex]::Escape($Ssid)) {
    L "[3/7] wifi                  already joined -> $(Clean $r)"
} else {
    L "[3/7] wifi                  joining $Ssid ..."
    $r = AT "AT+CWJAP_DEF=`"$Ssid`",`"$Pwd`"" 9000
    if ($r -match "WIFI GOT IP") { L "        joined              OK -> $(Clean $r)" }
    else { L "        join                FAIL -> $(Clean $r)"; $bad++ }
}

# AT+SLEEP is per-session (not saved to flash) - this is the whole reason this script exists
$r = AT "AT+SLEEP=0" 700
$r2 = AT "AT+SLEEP?" 600
if ($r2 -match "\+SLEEP:0") { L "[4/7] radio sleep OFF       OK  (kills ~4x latency jitter)" }
else { L "[4/7] radio sleep OFF       FAIL -> $(Clean $r2)"; $bad++ }

[void](AT "AT+CIPCLOSE" 500)
[void](AT "AT+CIPMODE=0" 400)
[void](AT "AT+CIPMUX=0" 400)
$r = AT "AT+CIPSTART=`"UDP`",`"$CarIp`",$UdpPort,$UdpPort,0" 1500
if ($r -match "CONNECT|OK|ALREADY CONNECTED") { L "[5/7] UDP link              OK" }
else { L "[5/7] UDP link              FAIL -> $(Clean $r)"; $bad++ }

$r = AT "AT+CIPMODE=1" 500
$r2 = AT "AT+CIPSEND" 900
if ($r2 -match ">") { L "[6/7] passthrough           OK  ($Port is now a transparent wire)" }
else { L "[6/7] passthrough           FAIL -> $(Clean $r2)"; $bad++ }

# Knock: the car side is a SoftAP with a FIXED UDP destination. On a cold boot it has no ARP
# entry for us, and a lightweight IP stack DROPS the datagram that triggers the ARP request
# (observed 2026-07-27: first A->B packet lost, every later one fine). One packet FROM us makes
# the AP learn our MAC, so the car's very first telemetry line is not sacrificed.
try { $sp.Write("`r`n") } catch {}
Start-Sleep -Milliseconds 200
L "[7/7] ARP knock sent        OK  (so the car's first telemetry line is not lost)"

if ($Verify) {
    L ""
    L "---- listening ${VerifyMs}ms for anything from the car ----"
    $got = Drain $VerifyMs
    if ($got.Length -gt 0) { L $got } else { L "(nothing - car powered off / not sending, link itself is up)" }
}

L ""
if ($bad -eq 0) {
    L "==== READY ===="
    L "Now use the normal tools against $Port :"
    L "  powershell -NoProfile -ExecutionPolicy Bypass -File read_serial.ps1 -Port $Port -Seconds 12"
    L "  powershell -NoProfile -ExecutionPolicy Bypass -File uart_send.ps1   -Port $Port -Cmd `"?`""
} else {
    L "==== $bad STEP(S) FAILED - link not ready ===="
}

Set-Content -Path $Out -Value $log.ToString() -Encoding ASCII
try { $sp.Close() } catch {}
