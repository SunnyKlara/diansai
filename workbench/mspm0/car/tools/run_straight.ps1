# run_straight.ps1 - one straight-line ground run, in ONE serial session, with its own brake.
#
# WHY A DEDICATED SCRIPT (and not uart_send + run_log)
#   Those are separate processes, so the capture holds the COM port and NOTHING can send `z`
#   while it runs. On a bench that is harmless; on the ground the car keeps going until a
#   firmware timeout fires - at 150 rpm that is metres of travel, i.e. into a wall.
#   Here the send / capture / BRAKE all live in one session, so run time is exactly -RunSec.
#
# WHAT IT MEASURES (this is the run that turns bench numbers into car numbers)
#   left/right encoder delta, DIFF and ratio  -> did it actually go straight
#   yaw net change + peak |wz|                -> heading drift while driving (needs `k` first)
#   telemetry loss (from #seq)                -> is the wireless view trustworthy while moving
#   and, if you tape-measure the distance and pass -DistMm, it prints the value to put in
#   config.h for ENC_COUNTS_PER_MM (which is still 0.0f, so "go N cm" cannot work yet).
#
# SAFETY
#   * clear runway ahead, and NOTHING tethered to the car (a taut cable steers it and destroys
#     the whole point of measuring straightness)
#   * -RunSec is capped below the firmware hard cap; the script also brakes itself
#   * start small: -Rpm 80 -RunSec 2 for the very first ground run, then scale up
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File run_straight.ps1 -Port COM4 -Rpm 80 -RunSec 2
#   powershell -NoProfile -ExecutionPolicy Bypass -File run_straight.ps1 -Port COM4 -Rpm 150 -RunSec 4 -DistMm 1320
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port   = "COM4",
    [int]$Baud      = 115200,
    [int]$Rpm       = 80,
    [double]$RunSec = 2.0,
    [int]$DistMm    = 0,       # tape-measured travel; 0 = skip the calibration maths
    [switch]$NoCal,            # skip the `k` gyro bias calibration (it needs 2 s of stillness)
    [double]$CalTimeout = 12.0, # how long to wait for the firmware to report `cal done`
    [int]$CalTelemMs = 200,     # telemetry period DURING the calibration (see the note below)
    [int]$TelemMs   = 20,      # f<ms>: 50 Hz gives good resolution over a short run
    [string]$Csv    = "",
    [string]$Out    = "run_straight_out.txt"
)

if ($RunSec -gt 10) { Write-Host "RunSec > 10 s is refused: keep it well under the firmware hard cap." -ForegroundColor Red; exit 1 }

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L "OPEN_FAIL ($Port): $($_.Exception.Message)"; exit 1 }
function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

$re = [regex]'\[ctl\]\s+(?<mode>\S+).*?\|\s*V:(?<v1>-?\d+),(?<v2>-?\d+).*?\|\s*PWM:(?<p1>-?\d+),(?<p2>-?\d+).*?\|\s*C:(?<c1>-?\d+),(?<c2>-?\d+).*?\|\s*D:(?<dv>-?\d+),(?<dw>-?\d+).*?\|\s*Y:(?<y>-?\d+)\s+W:(?<w>-?\d+).*?\|\s*#(?<seq>\d+)\s+t(?<t>\d+)'
$rows = New-Object System.Collections.Generic.List[object]

# Line assembly. ReadExisting() cuts wherever the driver happens to have buffered, so a raw
# chunk usually ends mid-line. Splitting each chunk on its own therefore (a) silently drops the
# split line and (b) - the reason this got rewritten - can show a telemetry line whose trailing
# " CAL" flag has not arrived yet, which reads exactly like "calibration finished". So: keep a
# rolling buffer and only ever look at lines that are terminated by a newline.
$script:rxbuf   = ''
$script:calDone = $false     # saw the firmware's `[imu] cal done`
$script:calSeen = $false     # saw at least one telemetry line still flagged CAL
$script:calFlag = $false     # was the most recent complete telemetry line flagged CAL
function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i   = $script:rxbuf.IndexOf("`n")
        $ln  = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)

        if ($ln -match '\[imu\]\s+cal done') { $script:calDone = $true }
        # The CAL flag sits after the `t<ms>` field, so anchoring on t<digits> makes both tests
        # reject a truncated line instead of guessing.
        if ($ln -match 't\d+\s+CAL\s*$')     { $script:calSeen = $true; $script:calFlag = $true }
        elseif ($ln -match 't\d+\s*$')       { $script:calFlag = $false }

        $m = $re.Match($ln)
        if (-not $m.Success) { continue }
        $g = $m.Groups
        $rows.Add([pscustomobject]@{
            seq=[int]$g['seq'].Value; t_ms=[int]$g['t'].Value; mode=$g['mode'].Value
            v1=[int]$g['v1'].Value; v2=[int]$g['v2'].Value
            pwm1=[int]$g['p1'].Value; pwm2=[int]$g['p2'].Value
            c1=[int]$g['c1'].Value; c2=[int]$g['c2'].Value
            dv=[int]$g['dv'].Value; dw=[int]$g['dw'].Value
            yaw=[double]$g['y'].Value/10.0; wz=[double]$g['w'].Value/100.0 })
    }
}
function Wait([double]$sec) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Drain; Start-Sleep -Milliseconds 20 } }

# Wait for the gyro-bias calibration to ACTUALLY finish instead of guessing a delay.
# WHY: CFG_IMU_CAL_N=400 samples are taken one per IMU tick, and a tick only happens when the
# main loop comes round - which the LCD redraw and 50 Hz dual-sink telemetry can stretch well
# past CFG_IMU_MS=5 ms. So the nominal 2.0 s can take much longer, and while g_cal_left > 0 the
# firmware SKIPS the integration branch entirely: g_wz_dps freezes at its last pre-`k` value and
# yaw never accumulates. A fixed sleep that is too short therefore produces a run whose heading
# data is silently frozen and looks like a perfect 0.0 deg of drift. Measured on this car:
# a 3.2 s fixed wait was not enough (wz sat at a constant -0.73 dps for the whole run).
# Two independent completion signals, because the link is lossy: the one-off `[imu] cal done`
# line, and the CAL flag disappearing from the (redundant, 50 per second) telemetry lines.
function WaitCal([double]$timeout) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeout) {
        Drain
        if ($script:calDone) { return $sw.Elapsed.TotalSeconds }
        if ($script:calSeen -and -not $script:calFlag) { return $sw.Elapsed.TotalSeconds }
        Start-Sleep -Milliseconds 20
    }
    return -1.0
}

L "================ run_straight  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port   $Rpm rpm for ${RunSec}s   telemetry f$TelemMs"
L "CLEAR RUNWAY AHEAD. NOTHING TETHERED TO THE CAR."
L ""

Send "z";          Start-Sleep -Milliseconds 400
$headingValid = $true
if ($NoCal) {
    Send "f$TelemMs";  Start-Sleep -Milliseconds 300
    $headingValid = $false
    L "-- gyro bias calibration SKIPPED (-NoCal): heading numbers below are not trustworthy --"
} else {
    # Calibrate at a SLOW telemetry rate, then speed up for the run itself.
    # WHY: the 400 calibration samples are taken one per main-loop pass, and the telemetry print is
    # a blocking UART write - 76 bytes x 2 sinks x 10 bits / 115200 = ~13 ms of the loop gone per
    # printed line. Measured on this car: `k` finishes in 8.55 s at f20 (50 Hz, dual sink) but in
    # 2.41 s at f200 (5 Hz) - i.e. the IMU tick degrades from the intended 200 Hz to about 47 Hz
    # purely because of telemetry. Slowing telemetry down for those two seconds costs nothing (the
    # car is standing still and there is nothing to watch) and makes the calibration ~3.5x faster.
    Send "f$CalTelemMs"; Start-Sleep -Milliseconds 300
    L ("-- gyro bias calibration (k) at f${CalTelemMs}: keep the car STILL until the firmware reports done --")
    Send "k"
    $calSec = WaitCal $CalTimeout
    if ($calSec -lt 0) {
        $headingValid = $false
        L ("  WARNING: no 'cal done' within {0:N1}s - the calibration is STILL RUNNING." -f $CalTimeout)
        L "  While it runs the firmware does not integrate yaw, so heading data for this run is dead."
        L "  Encoder counts are unaffected, so the odometry calibration below is still valid."
    } else {
        L ("  cal done after {0:N2}s   (nominal is 2.0s = 400 samples x 5ms; longer means the main loop is being stretched)" -f $calSec)
    }
    Send "o"; Start-Sleep -Milliseconds 300
    Send "f$TelemMs"; Start-Sleep -Milliseconds 300      # back up to run resolution
}
[void]$sp.ReadExisting(); $rows.Clear()

L "-- baseline (0.6s still) --"
Wait 0.6
$base = $rows.Count
if ($base -lt 5) { L "RESULT: INCONCLUSIVE - no telemetry before the run; aborting WITHOUT driving."; Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 2 }
$c1_0 = $rows[$rows.Count-1].c1; $c2_0 = $rows[$rows.Count-1].c2
$yaw0 = $rows[$rows.Count-1].yaw

L ("-- GO : m7 v$Rpm for ${RunSec}s --")
# CONFIRM THE COMMANDS LANDED. The wireless bridge is UDP with no retransmit: measured 15-22%
# telemetry loss once the car is on the floor, and the DOWNLINK drops too. A lost `m7` or `v<rpm>`
# means the car silently never moves and the capture returns zeros - which reads like "the motor
# is broken" instead of "the command never arrived". So: send, then read back the mode / D field,
# and retry. Confirmation comes from the telemetry itself (mode must become DRVC, D must show the
# commanded speed), not from the fact that we called Send().
$ok = $false
for ($try = 1; $try -le 4; $try++) {
    Send "m7"; Start-Sleep -Milliseconds 200
    Send "v$Rpm"
    $rows.Clear(); Wait 0.5
    $conf = $rows | Where-Object { $_.mode -eq 'DRVC' -and $_.dv -eq $Rpm }
    if ($conf.Count -gt 0) { $ok = $true; L ("  commands confirmed on attempt {0} (mode=DRVC, D={1})" -f $try, $Rpm); break }
    L ("  attempt {0}: no confirmation (mode/D not echoed back) - resending" -f $try)
}
if (-not $ok) {
    Send "z"
    L ""
    L "RESULT: INCONCLUSIVE - could not confirm the drive command over the link; did NOT keep driving."
    L "  The link is dropping the DOWNLINK, not just telemetry. Fix the radio path first:"
    L "  move the PC-side module down near the floor / closer to the car, or try channel 1 or 6."
    Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 2
}
Wait $RunSec
Send "z"                                  # <= the whole reason this script exists
L "-- brake sent, settling 1.0s --"
Wait 1.0

$n = $rows.Count
$last = $rows[$n-1]
$moving = $rows | Where-Object { $_.mode -eq 'DRVC' -and ($_.pwm1 -ne 0 -or $_.pwm2 -ne 0) }
$dC1 = $last.c1 - $c1_0; $dC2 = $last.c2 - $c2_0
$yawNet = $last.yaw - $yaw0
$wzPeak = ($rows | ForEach-Object { [Math]::Abs($_.wz) } | Measure-Object -Maximum).Maximum
$vPk1 = ($rows | ForEach-Object { [Math]::Abs($_.v1) } | Measure-Object -Maximum).Maximum
$vPk2 = ($rows | ForEach-Object { [Math]::Abs($_.v2) } | Measure-Object -Maximum).Maximum
$expect = $last.seq - $rows[0].seq + 1
$loss = if ($expect -gt 0) { 100.0 * ($expect - $n) / $expect } else { 0.0 }
$avgC = ($dC1 + $dC2) / 2.0

L ""
L "---- result ----"
L ("samples / telemetry loss : {0} / {1:N2} %" -f $n, $loss)
L ("frames with PWM on       : {0}" -f $moving.Count)
L ("encoder delta L / R      : {0} / {1} counts" -f $dC1, $dC2)
L ("DIFF (L-R) / ratio       : {0} / {1}" -f ($dC1 - $dC2), $(if ($dC2 -ne 0) { "{0:N3}" -f ($dC1/[double]$dC2) } else { "n/a" }))
L ("average of the two       : {0:N0} counts   <- use this for the mm calibration" -f $avgC)
# Frozen-heading detector. A real gyro always jitters (noise floor ~0.2 dps), so an identical wz
# in every single frame is not a measurement - it is a stale global being reprinted. That is what
# an unfinished `k` looks like from the outside, and without this check it reads as "0.0 deg of
# drift", i.e. a perfect run.
$wzDistinct = ($rows | ForEach-Object { $_.wz } | Sort-Object -Unique).Count
if ($wzDistinct -le 1 -and $n -ge 10) {
    $headingValid = $false
    L ("heading data             : FROZEN - wz was {0} dps in all {1} frames (stale global, not a measurement)" -f $rows[0].wz, $n)
}
if ($headingValid) {
    L ("yaw net change           : {0:N1} deg   peak |wz| {1:N1} dps" -f $yawNet, $wzPeak)
} else {
    L ("yaw net change           : INVALID ({0:N1} deg / peak {1:N1} dps shown for the record only)" -f $yawNet, $wzPeak)
}
L ("peak |rpm| L / R         : {0} / {1}   (target {2})" -f $vPk1, $vPk2, $Rpm)
L ("final mode / PWM         : {0} / {1},{2}   (must be IDLE 0,0 = brake worked)" -f $last.mode, $last.pwm1, $last.pwm2)
if ($Csv -ne "") { $rows | Export-Csv -Path $Csv -NoTypeInformation -Encoding ASCII; L ("CSV written              : {0}" -f $Csv) }

L ""
if ($DistMm -gt 0) {
    $cpm = $avgC / [double]$DistMm
    L "---- ENC_COUNTS_PER_MM calibration ----"
    L ("tape-measured travel     : {0} mm" -f $DistMm)
    L ("=> ENC_COUNTS_PER_MM     : {0:N4}  counts/mm" -f $cpm)
    L ("   (implied wheel travel per rev = {0:N1} mm, with ENC_CPR=800)" -f (800.0 / $cpm))
    L "   Put it in config.h, rebuild, reflash. Sanity-check the implied wheel circumference:"
    L "   a ~65 mm wheel should come out near 204 mm/rev. Far off => the tape measurement or the"
    L "   run was not clean (wheel slip, curved path, brake distance included)."
} else {
    L "---- next step ----"
    L "Tape-measure how far the car actually travelled, then re-run with -DistMm <mm> (or just"
    L "compute: ENC_COUNTS_PER_MM = average_counts / measured_mm) and back-fill config.h."
    L "NOTE measure the distance the WHEELS rolled; the settling after `z` is included in the"
    L "counts above, so brake coast is part of the number - that is intentional and consistent."
}

L ""
if ($moving.Count -lt 3) {
    L "RESULT: INCONCLUSIVE - almost no frames with PWM on. Did it enter m7 / was it stopped early?"
    Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 2
}
if ($last.mode -ne 'IDLE' -or $last.pwm1 -ne 0 -or $last.pwm2 -ne 0) {
    L "RESULT: FAIL - car did not come to a stop. CHECK IT PHYSICALLY."
    Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 1
}

# Drivetrain sanity BEFORE declaring success. "It braked cleanly" is not the same as "it drove":
# a run where one wheel never turned used to be reported as PASS, which then invites calibrating
# ENC_COUNTS_PER_MM off the average of a good wheel and a dead one - a wrong number that would
# quietly poison every distance command afterwards.
$a1 = [Math]::Abs($dC1); $a2 = [Math]::Abs($dC2)
$mn = [Math]::Min($a1, $a2); $mx = [Math]::Max($a1, $a2)
if ($mx -lt 50) {
    L "RESULT: FAIL - neither wheel turned (both encoder deltas ~0). The car never moved."
    L "  Next: powershell -File motor_probe.ps1 -Port $Port   (splits electrically-dead from mechanically-stalled)"
    Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 1
}
if ($mn -lt 0.5 * $mx) {
    L ("RESULT: FAIL - only one wheel turned (L {0} vs R {1} counts). Do NOT calibrate mm from this run." -f $dC1, $dC2)
    L "  Next: powershell -File motor_probe.ps1 -Port $Port   (splits electrically-dead from mechanically-stalled)"
    Set-Content $Out $log.ToString() -Encoding ASCII; try{$sp.Close()}catch{}; exit 1
}
if ($mn -lt 0.85 * $mx) {
    L ("RESULT: PASS with a warning - wheels differ by {0:N0} % (L {1} vs R {2}). It curved; the mm number is usable but re-run for a straighter one." -f (100.0*($mx-$mn)/$mx), $dC1, $dC2)
    Set-Content $Out $log.ToString() -Encoding ASCII
    try { $sp.Close(); $sp.Dispose() } catch {}
    exit 0
}
L "RESULT: PASS - ran and stopped cleanly. Judge straightness from DIFF and yaw net change above."
Set-Content $Out $log.ToString() -Encoding ASCII
try { $sp.Close(); $sp.Dispose() } catch {}
exit 0
