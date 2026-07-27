# servo_sweep.ps1 - drive the steering servo over the serial console during bring-up.
#
# WHY A SCRIPT: finding centre and the two mechanical limits needs MANY pulse values.
# Doing that by editing config.h + re-flashing would be ~115s per try and would trip this
# repo's "no rapid repeated flashing" taboo (SSOT section D2 rule 2 - that is how this chip
# was bricked once). So the servo is driven live over UART with the U<us> command, and the
# value that wins gets written back to config.h afterwards.
#
# TWO MODES:
#   -Mode Rock : alternate between -From and -To, -Cycles times. Use this to answer
#                "is the servo responding at all, and is the travel symmetric?".
#                A repeating motion is observable even if the human starts watching late -
#                a single one-shot move is easy to miss.
#   -Mode Step : walk from -From to -To in -StepUs increments, dwelling at each. Use this to
#                creep up on the centre or on a mechanical limit while a human watches.
#
# SAFETY:
#   * Always ends at -EndUs (default 1500) so the servo is never left parked at an extreme.
#   * The firmware clamps every value to CFG_SERVO_MIN_US..MAX_US, so this script cannot
#     command past the configured hard limits even if you ask it to.
#   * Ctrl-C leaves the servo wherever it was; re-run with -EndUs to recover, or send U0
#     (limp) with uart_send.ps1.
#
# Usage:
#   powershell -File servo_sweep.ps1 -Mode Rock -From 1300 -To 1700 -Cycles 4
#   powershell -File servo_sweep.ps1 -Mode Step -From 1500 -To 1650 -StepUs 10 -DwellMs 700
#
# ASCII-only by repo rule.

param(
    [ValidateSet('Rock', 'Step')]
    [string] $Mode    = 'Rock',
    [int]    $From    = 1300,
    [int]    $To      = 1700,
    [int]    $Cycles  = 4,
    [int]    $StepUs  = 10,
    [int]    $DwellMs = 1200,
    [int]    $EndUs   = 1500,
    [string] $Port    = 'COM30',
    [int]    $Baud    = 115200
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 300
try { $sp.Open() } catch { Write-Output "OPEN_FAIL: $($_.Exception.Message)"; exit 1 }
Start-Sleep -Milliseconds 250
try { $sp.DiscardInBuffer() } catch { }

# The MCU RX FIFO is small; a burst write drops bytes. Same 25ms/char pacing as uart_send.ps1.
function Send-Us([int]$us) {
    foreach ($ch in ("U$us" + "`n").ToCharArray()) {
        $sp.Write([string]$ch); Start-Sleep -Milliseconds 25
    }
}

# Pull the [srv] echo out of the telemetry stream so we can prove the firmware took the value.
function Read-Srv([int]$ms) {
    $deadline = (Get-Date).AddMilliseconds($ms)
    $buf = ''
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 60
        try { $buf += $sp.ReadExisting() } catch { }
    }
    $hit = @($buf -split "`n" | Where-Object { $_ -match '\[srv\]' })
    if ($hit.Count -gt 0) { return ($hit[-1].Trim() -replace '\s+', ' ') }
    return '(no [srv] echo)'
}

$plan = @()
switch ($Mode) {
    'Rock' {
        for ($i = 0; $i -lt $Cycles; $i++) { $plan += $From; $plan += $To }
    }
    'Step' {
        $dir = if ($To -ge $From) { $StepUs } else { -$StepUs }
        if ($StepUs -le 0) { Write-Output 'BAD_ARGS: -StepUs must be > 0'; $sp.Close(); exit 1 }
        for ($u = $From; ; $u += $dir) {
            $plan += $u
            if ($dir -gt 0 -and $u -ge $To) { break }
            if ($dir -lt 0 -and $u -le $To) { break }
        }
    }
}

Write-Output "mode=$Mode  steps=$($plan.Count)  dwell=${DwellMs}ms  port=$Port"
Write-Output '--------------------------------------------------------------'
$n = 0
foreach ($us in $plan) {
    $n++
    Send-Us $us
    $echo = Read-Srv $DwellMs
    Write-Output ("[{0,3}/{1}] U{2}  ->  {3}" -f $n, $plan.Count, $us, $echo)
}

# RETURN TO CENTRE IN SMALL STEPS, not one jump.
# 2026-07-27: the original one-shot "Send-Us $EndUs" made the servo slam from e.g. 1900 to 1500
# (400us in one go) at full speed and brake hard at the end -> audible gear-lash CLACK.
# That noise was initially misread as "hitting the mechanical limit / stalling", which is a
# different failure (limit stall = CONTINUOUS noise while HOLDING; this = noise WHILE MOVING).
# Ramping the return removes the artefact so the only remaining noise is a real limit stall.
$last = if ($plan.Count -gt 0) { [int]$plan[-1] } else { [int]$EndUs }
if ($last -ne $EndUs) {
    $rampStep = 25
    $dir = if ($EndUs -ge $last) { $rampStep } else { -$rampStep }
    Write-Output '--------------------------------------------------------------'
    Write-Output ("ramping back to U$EndUs from U$last in ${rampStep}us steps (avoids gear-lash clack)")
    for ($u = $last + $dir; ; $u += $dir) {
        if (($dir -gt 0 -and $u -ge $EndUs) -or ($dir -lt 0 -and $u -le $EndUs)) { break }
        Send-Us $u
        Start-Sleep -Milliseconds 120
    }
}
Send-Us $EndUs
$echo = Read-Srv 800
Write-Output '--------------------------------------------------------------'
Write-Output ("parked at U$EndUs  ->  $echo")
$sp.Close()
Write-Output 'RESULT: sequence sent. The verdict is what the HUMAN saw - this script only proves the firmware accepted the values.'
exit 0
