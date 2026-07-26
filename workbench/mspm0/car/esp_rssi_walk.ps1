# esp_rssi_walk.ps1 - measure the ONE dimension the bench cannot answer: HOW FAR does the link reach.
#
# Setup (no MCU / no car needed):
#   * Module B stays on the PC (this port). It must be in AT mode, NOT passthrough - the script
#     escapes with '+++' first, because in passthrough the module answers nothing on the UART.
#   * Module A is carried around, powered by a power bank / battery (3.3V ONLY - never 5V).
#     Its TXD/RXD do not need to be connected to anything: we are measuring the RF path only.
#   * B is the STA, so B is the side that can report RSSI of A's SoftAP -> we poll it from here.
#
# The script polls `AT+CWJAP?` every -PollMs, logs wall-clock time + RSSI + link state to CSV,
# and at the end reports the RSSI range plus every disconnect it saw. Walk away steadily and
# note the clock time at each distance, then match it against the CSV.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_rssi_walk.ps1 -Port COM6 -Seconds 120
#
# IMPORTANT caveat on the result: a module carried in your hand is the BEST case. Bolted on the
# car it will read worse - metal chassis, antenna near the battery/motors, and lower mounting
# height all cost several dB. Treat this walk as an upper bound, then re-measure on the car.
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM6",
    [int]$Baud       = 115200,
    [int]$Seconds    = 120,
    [int]$PollMs     = 1000,
    [string]$Csv     = "esp_rssi_walk.csv",
    [string]$Out     = "esp_rssi_walk_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

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
        Start-Sleep -Milliseconds 25
    }
    return $rx.ToString()
}

try { $sp.Open() } catch { L "OPEN_FAIL: $($_.Exception.Message)  (close other serial tools first)"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
Start-Sleep -Milliseconds 250

# leave passthrough if a previous run left B in it (harmless if already in AT mode)
Start-Sleep -Milliseconds 1100
try { $sp.Write("+++") } catch {}
Start-Sleep -Milliseconds 1100
try { $sp.Write("`r`n") } catch {}
Start-Sleep -Milliseconds 300
try { $sp.DiscardInBuffer() } catch {}

$r = AT "AT" 600
if ($r -notmatch "OK") { L "module on $Port not answering AT -> $($r -replace "`r`n",' | ')"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }

L "==== RSSI walk on $Port for ${Seconds}s, poll ${PollMs}ms   $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===="
L "Carry module A away now. Note the clock time at each distance; match it to $Csv afterwards."
L ""
L "  time      RSSI(dBm)  state        quality"
Set-Content -Path $Csv -Value "wallclock,elapsed_s,rssi_dbm,state" -Encoding ASCII

$samples = @()
$drops = @()
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$lastState = "?"
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    $t0 = $sw.Elapsed.TotalSeconds
    $r = AT "AT+CWJAP?" 700
    $now = Get-Date -Format 'HH:mm:ss'
    $rssi = ""
    $state = "DISCONNECTED"
    # +CWJAP:"ssid","bssid",<ch>,<rssi>,...
    $m = [regex]::Match($r, '\+CWJAP:"[^"]*","[^"]*",\s*(\d+),\s*(-?\d+)')
    if ($m.Success) {
        $rssi = [int]$m.Groups[2].Value
        $state = "CONNECTED"
        $samples += $rssi
    } elseif ($r -match "No AP") {
        $state = "NO_AP"
    }

    $qual = "-"
    if ($state -eq "CONNECTED") {
        if     ($rssi -ge -50) { $qual = "excellent" }
        elseif ($rssi -ge -67) { $qual = "good" }
        elseif ($rssi -ge -75) { $qual = "marginal  <-- expect loss" }
        elseif ($rssi -ge -85) { $qual = "bad       <-- unreliable" }
        else                   { $qual = "about to drop" }
    }
    if ($state -ne $lastState) {
        if ($lastState -ne "?") { $drops += ("{0} (t={1:N0}s): {2} -> {3}" -f $now, $t0, $lastState, $state) }
        $lastState = $state
    }

    L ("  {0}  {1,8}   {2,-12} {3}" -f $now, $rssi, $state, $qual)
    Add-Content -Path $Csv -Value ("{0},{1:N1},{2},{3}" -f $now, $t0, $rssi, $state) -Encoding ASCII

    $remain = $PollMs - ($sw.Elapsed.TotalSeconds - $t0) * 1000
    if ($remain -gt 0) { Start-Sleep -Milliseconds $remain }
}

L ""
L "==== SUMMARY ===="
if ($samples.Count -gt 0) {
    $avg = [math]::Round(($samples | Measure-Object -Average).Average, 1)
    $mn  = ($samples | Measure-Object -Minimum).Minimum
    $mx  = ($samples | Measure-Object -Maximum).Maximum
    L "connected samples : $($samples.Count) / $([math]::Floor($Seconds * 1000.0 / $PollMs))"
    L "RSSI dBm          : avg $avg   best $mx   worst $mn"
} else {
    L "never connected - check that module A is powered and its SoftAP is up"
}
if ($drops.Count -gt 0) {
    L "state changes     :"
    foreach ($d in $drops) { L "    $d" }
} else {
    L "state changes     : none (link held the whole time)"
}
L ""
L "CSV -> $Csv    (match the wallclock column against where you were standing)"
L "NOTE: hand-carried = upper bound. Re-measure with the module mounted on the car."
L "NOTE: RSSI only says 'is there signal'. Actual packet loss needs data flowing, i.e. the MCU"
L "      on the car sending telemetry - measure that with esp_link_test.ps1 once it is wired."

Set-Content -Path $Out -Value $log.ToString() -Encoding ASCII
try { $sp.Close() } catch {}
