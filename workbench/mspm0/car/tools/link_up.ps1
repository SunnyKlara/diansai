# link_up.ps1 - one command that gets the wireless command link WORKING, or tells you exactly
#               which physical thing is broken. Run it whenever the car has been power-cycled.
#
# WHY THIS EXISTS (2026-07-31, cost us most of an afternoon):
#   The wireless link is two ESP-01S modules + an AT session + a UDP passthrough socket. All of
#   that state is volatile: power-cycle the car and the link has to be rebuilt by hand. Worse,
#   the failure is ASYMMETRIC -- telemetry (car -> PC) keeps flowing while commands (PC -> car)
#   silently go nowhere. Every collection script then reports something misleading like
#   "virtual button never started the run", and you go hunting in the firmware.
#
# THE ONE JUDGEMENT THAT MATTERS: does a '?' get a REPLY?
#   Telemetry arriving proves NOTHING about the command path. Only a reply does.
#
# THREE OUTCOMES, THREE DIFFERENT PHYSICAL CAUSES -- never conflate them:
#   PASS        '?' answered. Link is fully usable.
#   NO_BYTES    nothing arrives at all => car unpowered, or PC-side ESP not joined to the car AP.
#   ONE_WAY     telemetry flows but '?' is never answered => the DOWNLINK is broken. After this
#               script has retried the socket and reset the PC-side module, the remaining suspect
#               is the car-side wire  ESP.TXD -> MCU PB3 (UART3_RX)  -- that is the only part
#               this script cannot fix from the PC.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\link_up.ps1
#   ... -Port COM7      pin the port instead of auto-detecting the CH340
#   ... -Quiet          only print the final RESULT line
# Exit: 0 = PASS, 1 = ONE_WAY (downlink dead), 2 = NO_BYTES, 3 = port/other error
#
# ASCII only on purpose: PS 5.1 reads .ps1 as ANSI and mangles UTF-8 Chinese in string literals.
param(
    [string]$Port = "",
    [int]$Baud = 115200,
    [string]$Ssid = "DIANSAI_CAR",
    [string]$Pwd = "car2026wifi",
    [string]$CarIp = "192.168.4.1",
    [int]$UdpPort = 3333,
    [switch]$Quiet
)

function Say([string]$s) { if (-not $Quiet) { Write-Output $s } }

# ---- pick a port: prefer an explicit one, else the CH340 that the PC-side ESP hangs off ----
if (-not $Port) {
    $cands = @(Get-CimInstance Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match 'CH340' -and $_.Name -match 'COM(\d+)' } |
        ForEach-Object { if ($_.Name -match 'COM(\d+)') { "COM" + $Matches[1] } })
    if ($cands.Count -eq 0) {
        Write-Output "RESULT: FAIL - no CH340 serial port found (PC-side ESP not plugged in?)"; exit 3
    }
    $Port = $cands[0]
}
Say ("port: {0}" -f $Port)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch {
    Write-Output ("RESULT: FAIL - cannot open {0}: {1}" -f $Port, $_.Exception.Message)
    Write-Output "  (a leftover child PowerShell may still hold the port; wait a few seconds and retry)"
    exit 3
}

function Soak([int]$ms) {
    $a = ''; $t = Get-Date
    while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $a += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 80 }
    return $a
}
function AtCmd([string]$c, [int]$ms) { $sp.Write($c + "`r`n"); return (Soak $ms) }

# Probe: send '?' and classify. Returns PASS / ONE_WAY / NO_BYTES.
function Probe() {
    try { $sp.DiscardInBuffer() } catch {}
    $sp.Write("?`n")
    $r = Soak 2200
    $hasTelemetry = ($r -match '\[ctl\]\s+\w+\s+tgt=')
    # these lines are printed ONLY as the answer to '?', never by the periodic telemetry
    $hasReply = ($r -match '\[task\]' -or $r -match '\[vseg\]' -or $r -match '\[ctl\] mode=')
    if ($hasReply) { return "PASS" }
    if ($hasTelemetry) { return "ONE_WAY" }
    return "NO_BYTES"
}

$state = Probe
Say ("probe 1: {0}" -f $state)
if ($state -eq "PASS") {
    $sp.Close(); Write-Output ("RESULT: PASS - link already good on {0}" -f $Port); exit 0
}

# ---- repair attempt 1: rebuild the UDP passthrough socket (leaves wifi join alone) ----
if ($state -eq "ONE_WAY") {
    Say "repair 1: rebuilding UDP passthrough socket"
    Start-Sleep -Milliseconds 1100; $sp.Write("+++"); Start-Sleep -Milliseconds 1100; $null = Soak 200
    $null = AtCmd "AT+CIPCLOSE" 1200
    $null = AtCmd "AT+CIPMODE=0" 800
    $null = AtCmd ('AT+CIPSTART="UDP","' + $CarIp + '",' + $UdpPort + ',' + $UdpPort + ',0') 2500
    $null = AtCmd "AT+CIPMODE=1" 800
    $null = AtCmd "AT+CIPSEND" 1200
    Start-Sleep -Milliseconds 600
    $state = Probe
    Say ("probe 2: {0}" -f $state)
    if ($state -eq "PASS") { $sp.Close(); Write-Output ("RESULT: PASS - repaired by rebuilding the socket"); exit 0 }
}

# ---- repair attempt 2: hard reset the PC-side module, rejoin, rebuild ----
Say "repair 2: AT+RST on the PC-side module, rejoin, rebuild"
$null = AtCmd "AT+RST" 1500
$t = Get-Date; $banner = ''
while (((Get-Date) - $t).TotalSeconds -lt 8) {
    $banner += Soak 400
    if ($banner -match 'ready') { break }      # wait for the boot banner, not for '>'
}
Say ("  boot banner seen: {0}" -f ($banner -match 'ready'))
$null = AtCmd "AT+CWMODE_CUR=1" 1000
$null = AtCmd ('AT+CWJAP_DEF="' + $Ssid + '","' + $Pwd + '"') 12000
$null = AtCmd "AT+SLEEP=0" 800                 # not persisted; costs ~4x latency jitter if skipped
$null = AtCmd ('AT+CIPSTART="UDP","' + $CarIp + '",' + $UdpPort + ',' + $UdpPort + ',0') 2500
$null = AtCmd "AT+CIPMODE=1" 800
$null = AtCmd "AT+CIPSEND" 1200
Start-Sleep -Milliseconds 800
$state = Probe
Say ("probe 3: {0}" -f $state)
$sp.Close()

if ($state -eq "PASS") { Write-Output "RESULT: PASS - repaired after resetting the PC-side module"; exit 0 }

if ($state -eq "ONE_WAY") {
    Write-Output "RESULT: ONE_WAY - telemetry flows but commands are NOT reaching the MCU."
    Write-Output "  Everything fixable from the PC has been retried (socket rebuilt, module reset, rejoined)."
    Write-Output "  Remaining suspect, in order:"
    Write-Output "    1) car-side wire  ESP.TXD -> MCU PB3 (UART3_RX)  -- loose/off. Most likely after"
    Write-Output "       any work near the battery or power wiring."
    Write-Output "    2) car-side ESP no longer in passthrough (its saved TRANSLINK did not restore)."
    Write-Output "  Wired fallback that does NOT need any of this: plug the DAP in and use its VCOM"
    Write-Output "  (MCU UART0 = PA10/PA11). Bench tests do not need wireless at all."
    exit 1
}

Write-Output "RESULT: NO_BYTES - nothing arriving at all."
Write-Output "  Check, in order: car powered on / PC-side ESP joined to $Ssid (AT+CWJAP?) /"
Write-Output "  car-side ESP has 3V3 / car-side AP actually on air (AT+CWLAP, use a RAW session --"
Write-Output "  esp_at.ps1 cannot capture CWLAP's multi-line reply and will look like 'no such AP')."
exit 2
