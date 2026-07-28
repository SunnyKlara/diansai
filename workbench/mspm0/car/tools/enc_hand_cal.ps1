# enc_hand_cal.ps1 - hand-turn encoder check + ENC_CPR calibration, motors OFF.
#
# WHY THIS FIRST (methodology rule: prove the feedback before touching the controller)
#   "0 rpm while the PWM saturates" is reported BY THE ENCODER. If an encoder is dead or was
#   rewired wrong, a wheel that is spinning perfectly reads 0 rpm - and the closed loop then
#   pushes the duty to the cap, which looks exactly like a mechanical stall. Those two cases
#   need different fixes, so they must be separated before anything else.
#   Turning the wheels by hand removes the motor, the driver and the control loop from the
#   picture entirely: if counts come in, the feedback chain is good.
#
# IT ALSO RE-CALIBRATES ENC_CPR, which a motor swap invalidates. ENC_CPR=800 in config.h was
# measured on the OLD motor (hand-turned 8 revs, 6402/8). A different gearbox or a different
# encoder disc makes it wrong, and a wrong ENC_CPR silently rescales every RPM reading and
# therefore every speed-loop gain.
#
# HOW IT WORKS (no interactive prompts - the harness cannot show them live)
#   One long capture. You do this inside it, at your own pace:
#     1. leave everything alone for a few seconds
#     2. turn the LEFT wheel exactly -Turns full revolutions (steady, one direction)
#     3. pause
#     4. turn the RIGHT wheel exactly -Turns full revolutions (same direction as the car
#        would drive forward, so the sign is meaningful)
#   Afterwards the script reports each channel's total delta, whether the two overlapped, and
#   the resulting counts-per-revolution.
#
# SAFETY: sends `z` first and never drives a motor. Wheels are turned by hand only.
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#
# Usage: powershell -File enc_hand_cal.ps1 -Port COM4 [-Turns 8] [-Sec 45]

param(
    [string]$Port = 'COM4',
    [int]$Baud    = 115200,
    [int]$Turns   = 8,
    [double]$Sec  = 45.0,
    [double]$BaseSec = 4.0
)

$ErrorActionPreference = 'Continue'

Write-Host ("================ enc_hand_cal  " + (Get-Date -Format 'HH:mm:ss') + " ================")
Write-Host ("port $Port   capture ${Sec}s   expecting $Turns revolutions per wheel")
Write-Host "MOTORS STAY OFF. Turn the wheels by hand."
Write-Host ("  first ~${BaseSec}s : touch nothing (baseline / noise check)")
Write-Host ("  then           : LEFT wheel  x $Turns revs, pause, RIGHT wheel x $Turns revs")
Write-Host ""

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$rows = New-Object System.Collections.ArrayList
$buf = ''

try {
    $sp.Open()
    Start-Sleep -Milliseconds 400
    foreach ($ch in "z`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    Start-Sleep -Milliseconds 300
    $sp.DiscardInBuffer()

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $Sec) {
        $s = ''
        try { $s = $sp.ReadExisting() } catch { }
        if ($s) { $buf += $s }
        while ($buf.Contains("`n")) {
            $i   = $buf.IndexOf("`n")
            $ln  = $buf.Substring(0, $i)
            $buf = $buf.Substring($i + 1)
            if ($ln -match 'C:(-?\d+),(-?\d+)') {
                [void]$rows.Add([pscustomobject]@{
                    ts = $sw.Elapsed.TotalSeconds
                    c1 = [int]$Matches[1]
                    c2 = [int]$Matches[2]
                })
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
Write-Host ("samples : {0}" -f $n)
if ($n -lt 20) {
    Write-Host "RESULT: INCONCLUSIVE - almost no telemetry captured (link or firmware)"
    exit 2
}

# Baseline: nothing should move while nobody touches the wheels. Drift here means electrical
# noise being counted as edges, which would poison every measurement downstream.
$base = @($rows | Where-Object { $_.ts -le $BaseSec })
$baseDrift1 = 0; $baseDrift2 = 0
if ($base.Count -ge 3) {
    $baseDrift1 = [Math]::Abs($base[$base.Count - 1].c1 - $base[0].c1)
    $baseDrift2 = [Math]::Abs($base[$base.Count - 1].c2 - $base[0].c2)
}
Write-Host ("baseline drift (should be 0) : L {0}  R {1}   over first {2}s" -f $baseDrift1, $baseDrift2, $BaseSec)

$d1 = $rows[$n - 1].c1 - $rows[0].c1
$d2 = $rows[$n - 1].c2 - $rows[0].c2
Write-Host ("total delta : L {0}  R {1} counts" -f $d1, $d2)

# Did the two wheels get turned in separate time windows? If they overlap the deltas are still
# valid (the counters are independent), but overlap would hide a cross-wired channel, so say so.
$mv1 = New-Object System.Collections.ArrayList
$mv2 = New-Object System.Collections.ArrayList
for ($i = 1; $i -lt $n; $i++) {
    if ($rows[$i].c1 -ne $rows[$i - 1].c1) { [void]$mv1.Add($rows[$i].ts) }
    if ($rows[$i].c2 -ne $rows[$i - 1].c2) { [void]$mv2.Add($rows[$i].ts) }
}
function Span($a) { if ($a.Count -eq 0) { return 'never moved' } return ("{0:N1}s .. {1:N1}s" -f $a[0], $a[$a.Count - 1]) }
Write-Host ("L moved during : {0}   ({1} changing samples)" -f (Span $mv1), $mv1.Count)
Write-Host ("R moved during : {0}   ({1} changing samples)" -f (Span $mv2), $mv2.Count)

Write-Host ""
Write-Host "---- verdict ----"
$dead = @()
if ([Math]::Abs($d1) -lt 50) { $dead += 'LEFT (enc1: PA7/PB19)' }
if ([Math]::Abs($d2) -lt 50) { $dead += 'RIGHT (enc2: PB20/PB21)' }

if ($baseDrift1 -gt 5 -or $baseDrift2 -gt 5) {
    Write-Host ("** counts moved while nothing was touched (L $baseDrift1 / R $baseDrift2) - electrical noise is")
    Write-Host "   being decoded as edges. Fix that before trusting anything below."
}

if ($dead.Count -gt 0) {
    foreach ($x in $dead) { Write-Host ("** $x produced no counts while being hand-turned") }
    Write-Host "   => that encoder chain is broken (wiring after the motor swap / power / pinout),"
    Write-Host "      NOT the motor. A wheel that spins but reads 0 rpm makes the speed loop push"
    Write-Host "      the duty to PWM_CAP, which looks identical to a mechanical stall."
    Write-Host "   Check: encoder connector orientation, VCC present, A/B on the right pins."
    Write-Host "RESULT: FAIL - feedback chain is not trustworthy yet"
    exit 1
}

$cpr1 = [Math]::Abs($d1) / [double]$Turns
$cpr2 = [Math]::Abs($d2) / [double]$Turns
Write-Host ("counts per output revolution : L {0:N1}   R {1:N1}   (config.h ENC_CPR = 800)" -f $cpr1, $cpr2)
$mismatch = 100.0 * [Math]::Abs($cpr1 - $cpr2) / [Math]::Max($cpr1, $cpr2)
Write-Host ("L vs R mismatch : {0:N1} %   (should be ~0 - same motor model both sides)" -f $mismatch)
$avg = ($cpr1 + $cpr2) / 2.0
Write-Host ("=> suggested ENC_CPR = {0:N1}   (was 800; {1:N0} % off)" -f $avg, (100.0 * ($avg - 800.0) / 800.0))
Write-Host ("signs : L {0}  R {1}   (both wheels turned the 'forward' way should give the SAME sign;" -f `
    $(if ($d1 -ge 0) { '+' } else { '-' }), $(if ($d2 -ge 0) { '+' } else { '-' }))
Write-Host "         opposite signs mean one encoder's A/B is swapped - fix in encoder.c, not by negating gains)"
if ($mismatch -gt 15.0) {
    Write-Host "RESULT: INCONCLUSIVE - the two sides disagree by more than 15 %, so at least one of them"
    Write-Host "  was not turned exactly $Turns revolutions. Re-run before writing anything into config.h."
    exit 2
}
Write-Host "RESULT: PASS - both encoders read cleanly; back-fill ENC_CPR and re-tune the speed loop"
exit 0
