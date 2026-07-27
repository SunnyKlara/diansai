# enc_delta.ps1 - read the encoder counters and turn a hand-rotated wheel into counts/rev.
#
# WHY A "READ TWICE" PROTOCOL INSTEAD OF A TIMED CAPTURE WINDOW
#   The counters are CUMULATIVE and survive, so there is nothing to catch in real time: read the
#   total before, let the human turn the wheel at their own pace, read the total after. No timing
#   pressure, no missed window, and the human can do one wheel at a time and count carefully.
#   (Repo rule, learned the hard way: for accumulating quantities read the kept total, do not try
#   to catch them live.)
#
# WHAT IT CHECKS BESIDES THE NUMBER
#   * static drift during this read - must be 0. Anything else means noise is being decoded as
#     edges (this is exactly what a loose pull-up on an open-collector encoder looks like).
#   * cross-channel movement - turning ONE wheel must move only ITS counter. If the other one
#     moves instead, the two encoder channels are swapped, which is easy to cause when re-seating
#     or re-soldering encoder wiring.
#
# ENC_CPR matters more than it looks: every RPM reading is counts/window scaled by it, so a wrong
# ENC_CPR silently rescales the speed loop, every gain tuned against it, and the odometry.
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#
# Usage:
#   powershell -File enc_delta.ps1 -Port COM4                      # just read the counters
#   powershell -File enc_delta.ps1 -Port COM4 -Base1 15152 -Base2 16494 -Turns 10 -Wheel L

param(
    [string]$Port = 'COM4',
    [int]$Baud    = 115200,
    [double]$Sec  = 3.0,
    [int]$Base1   = [int]::MinValue,   # baseline C1; omit to only read
    [int]$Base2   = [int]::MinValue,   # baseline C2
    [int]$Turns   = 0,                 # revolutions the human turned
    [ValidateSet('L','R','')] [string]$Wheel = '',
    [double]$CprRef = 800.0            # current config.h value, for the delta report
)

$ErrorActionPreference = 'Continue'
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$rows = New-Object System.Collections.ArrayList
$buf = ''

try {
    $sp.Open()
    Start-Sleep -Milliseconds 300
    $sp.DiscardInBuffer()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Sec) {
        $s = ''
        try { $s = $sp.ReadExisting() } catch { }
        if ($s) { $buf += $s }
        while ($buf.Contains("`n")) {
            $i = $buf.IndexOf("`n")
            $ln = $buf.Substring(0, $i)
            $buf = $buf.Substring($i + 1)
            if ($ln -match 'C:(-?\d+),(-?\d+)') {
                [void]$rows.Add([pscustomobject]@{ c1 = [int]$Matches[1]; c2 = [int]$Matches[2] })
            }
        }
        Start-Sleep -Milliseconds 10
    }
}
catch { Write-Host ("EXCEPTION: " + $_.Exception.Message) }
finally {
    if ($sp.IsOpen) { try { $sp.Close() } catch { } }
    $sp.Dispose()
}

$n = $rows.Count
Write-Host ("================ enc_delta  " + (Get-Date -Format 'HH:mm:ss') + " ================")
Write-Host ("samples : {0}" -f $n)
if ($n -lt 5) { Write-Host "RESULT: INCONCLUSIVE - not enough telemetry"; exit 2 }

$c1 = $rows[$n - 1].c1
$c2 = $rows[$n - 1].c2
$dr1 = $c1 - $rows[0].c1
$dr2 = $c2 - $rows[0].c2
Write-Host ("counters now      : C1(left) {0,9}   C2(right) {1,9}" -f $c1, $c2)
Write-Host ("drift during read : C1 {0,9}   C2 {1,9}   <- both must be 0 (else noise is being counted)" -f $dr1, $dr2)

if ($Base1 -eq [int]::MinValue -or $Base2 -eq [int]::MinValue) {
    Write-Host ""
    Write-Host ("*** baseline to quote next time:  -Base1 {0} -Base2 {1} ***" -f $c1, $c2)
    if ($dr1 -ne 0 -or $dr2 -ne 0) { Write-Host "RESULT: FAIL - counters move while nothing is being turned"; exit 1 }
    Write-Host "RESULT: PASS - counters read, no static drift"
    exit 0
}

$d1 = $c1 - $Base1
$d2 = $c2 - $Base2
Write-Host ("delta vs baseline : C1 {0,9}   C2 {1,9}" -f $d1, $d2)

if ($Turns -le 0 -or $Wheel -eq '') {
    Write-Host "RESULT: INCONCLUSIVE - pass -Turns and -Wheel L|R to get counts/rev"
    exit 2
}

# which counter is supposed to have moved, and which must have stayed put
if ($Wheel -eq 'L') { $moved = $d1; $still = $d2; $movedName = 'C1(left)';  $stillName = 'C2(right)' }
else                { $moved = $d2; $still = $d1; $movedName = 'C2(right)'; $stillName = 'C1(left)' }

Write-Host ""
Write-Host ("---- wheel $Wheel turned $Turns revolutions ----")
if ([Math]::Abs($moved) -lt 50) {
    Write-Host ("** {0} barely moved ({1} counts) while that wheel was turned {2} revs" -f $movedName, $moved, $Turns)
    Write-Host "   => that encoder is not reporting: check the pull-up, VCC and the A/B pins."
    Write-Host "RESULT: FAIL - no counts from the wheel that was turned"
    exit 1
}
$cpr = [Math]::Abs($moved) / [double]$Turns
Write-Host ("counts per revolution : {0} / {1} = {2:N1}" -f [Math]::Abs($moved), $Turns, $cpr)
Write-Host ("vs config.h ENC_CPR={0:N0} : {1:N1} %" -f $CprRef, (100.0 * ($cpr - $CprRef) / $CprRef))
Write-Host ("direction : {0} counted {1}  (turn the other wheel the SAME way - it must get the same sign)" -f `
    $movedName, $(if ($moved -ge 0) { 'UP (+)' } else { 'DOWN (-)' }))

Write-Host ""
Write-Host "---- cross-channel check ----"
if ([Math]::Abs($still) -le 20) {
    Write-Host ("OK: {0} stayed put ({1}) => wheel $Wheel really maps to {2}" -f $stillName, $still, $movedName)
} elseif ([Math]::Abs($still) -gt [Math]::Abs($moved) / 2) {
    Write-Host ("** {0} moved {1} while {2} moved only {3}" -f $stillName, $still, $movedName, $moved)
    Write-Host "   => the two encoder channels look SWAPPED (easy to cause when re-seating/re-soldering)."
    Write-Host "RESULT: FAIL - channel mapping is wrong"
    exit 1
} else {
    Write-Host ("?? {0} moved {1} - not zero but not dominant either. Either the chassis/other wheel got" -f $stillName, $still)
    Write-Host "   dragged along, or there is crosstalk. Re-run holding the other wheel still."
}
Write-Host "RESULT: PASS - counts/rev measured above"
exit 0
