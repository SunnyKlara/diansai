# nav_test.ps1 - acceptance test for the car-level navigation layer (ladders 2.5 / 3 / 4).
#
# ONE serial session that sets up, runs ONE move, waits for the firmware's [nav] scorecard,
# and brakes itself. Same reason as run_straight.ps1: if the capture is a separate process it
# holds the COM port and nothing can send `z` while the car is driving.
#
# WHAT IT CAN AND CANNOT PROVE  (read this before trusting a PASS)
#   The car cannot validate its own sensors. `n1000` reporting err_mm=3 only means "the encoders
#   think it went 1000 mm" - if ENC_COUNTS_PER_MM is wrong, every run reports success while the
#   car travels the wrong distance. Likewise a 90 deg turn is judged by the same gyro that drove it.
#   So: without an INDEPENDENT measurement this script returns INCONCLUSIVE, not PASS.
#     -DistMm <mm>   tape-measured travel      (straight runs)
#     -MeasDeg <deg> protractor-measured angle (turns)
#   Both tools are on the official parts list (5 m tape, protractor) - use them.
#
# MODES
#   -Straight <mm>   go straight N mm holding heading      (ladder 2.5 + 3)
#   -Turn <deg>      turn N deg in place, + = left         (ladder 4)
#   -CalTurn <deg>   OPEN-LOOP spin to calibrate ENC_COUNTS_PER_DEG (the no-IMU fallback)
#
# CALIBRATION VALUES (live in RAM only - back-fill config.h and commit once settled)
#   -CountsPerMm <f>   sent as c<x100>   ; ENC_COUNTS_PER_MM  is 0.0 in config.h => n<mm> is REFUSED
#   -CountsPerDeg <f>  sent as q<x100>   ; only needed for the no-IMU fallback
#
# SAFETY
#   * clear runway, nothing tethered to the car (a taut cable steers it and ruins the measurement)
#   * for -Turn / -CalTurn: wheels ON THE GROUND with ~0.5 m clear around (spinning in the air
#     tells you nothing - the gyro sees no rotation)
#   * the firmware brakes itself too (silence timeout + 15 s hard cap), this script is belt+braces
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File nav_test.ps1 -Port COM4 -CountsPerMm 3.92 -Straight 1000 -DistMm 995
#   powershell -NoProfile -ExecutionPolicy Bypass -File nav_test.ps1 -Port COM4 -Turn 90 -MeasDeg 88
#   powershell -NoProfile -ExecutionPolicy Bypass -File nav_test.ps1 -Port COM4 -CalTurn 360
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM4",
    [int]$Baud       = 115200,
    [int]$Straight   = 0,          # mm, signed; 0 = not this mode
    [int]$Turn       = 0,          # deg, signed, + = left
    [int]$CalTurn    = 0,          # deg, open-loop spin for counts/deg calibration
    [double]$CountsPerMm  = 0,     # push before the run (0 = leave firmware value alone)
    [double]$CountsPerDeg = 0,
    [int]$DistMm     = 0,          # tape-measured travel  -> needed for a real PASS
    [double]$MeasDeg = 0,          # protractor-measured angle -> needed for a real PASS
    [double]$TolMm   = 10,         # acceptance tolerance, distance
    [double]$TolDeg  = 3,          # acceptance tolerance, angle (ladder 4 target is 3 deg)
    [switch]$NoCal,                # skip the gyro bias calibration `k`
    [double]$CalTimeout = 15.0,
    [int]$TelemMs    = 50,
    [int]$SpinRpm    = 120,        # -CalTurn only: open-loop spin speed
    [double]$Timeout = 20.0,       # how long to wait for the [nav] scorecard
    [string]$Csv     = "",
    [string]$Out     = "nav_test_out.txt"
)

$modes = @($Straight, $Turn, $CalTurn) | Where-Object { $_ -ne 0 }
if ($modes.Count -ne 1) {
    Write-Host "Pick exactly one of -Straight / -Turn / -CalTurn (non-zero)." -ForegroundColor Red
    exit 2
}

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }
function Finish([string]$verdict, [int]$code) {
    L ""
    L "RESULT: $verdict"
    Set-Content $Out $log.ToString() -Encoding ASCII
    try { $sp.Close(); $sp.Dispose() } catch {}
    exit $code
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }
# One char at a time with a gap: a single burst write overruns the MCU RX FIFO and drops bytes.
function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

# --- line assembly -----------------------------------------------------------------
# ReadExisting() cuts wherever the driver happened to buffer, so a raw chunk usually ends
# mid-line. Keep a rolling buffer and only ever inspect newline-terminated lines, otherwise a
# half-arrived scorecard gets parsed with fields missing.
$script:rxbuf   = ''
$script:calDone = $false
$script:calSeen = $false
$script:calFlag = $false
$script:report  = $null      # the [nav] scorecard line
$script:calLine = $null      # the [nav] counts/mm*100=... readback
$script:lines   = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

$reTelem = [regex]'\[ctl\]\s+(?<mode>\S+).*?\|\s*V:(?<v1>-?\d+),(?<v2>-?\d+).*?\|\s*PWM:(?<p1>-?\d+),(?<p2>-?\d+).*?\|\s*C:(?<c1>-?\d+),(?<c2>-?\d+).*?\|\s*D:(?<dv>-?\d+),(?<dw>-?\d+).*?\|\s*Y:(?<y>-?\d+)\s+W:(?<w>-?\d+)'

function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i  = $script:rxbuf.IndexOf("`n")
        $ln = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)
        $script:lines.Add($ln)

        if ($ln -match '\[imu\]\s+cal done')            { $script:calDone = $true }
        if ($ln -match 't\d+\s+CAL\s*$')                { $script:calSeen = $true; $script:calFlag = $true }
        elseif ($ln -match 't\d+\s*$')                  { $script:calFlag = $false }
        if ($ln -match '\[nav\]\s+counts/mm')           { $script:calLine = $ln }
        if ($ln -match '\[nav\]\s+(DONE|STOP)\s')       { $script:report  = $ln }

        $m = $reTelem.Match($ln)
        if ($m.Success) {
            $g = $m.Groups
            $rows.Add([pscustomobject]@{
                mode=$g['mode'].Value
                v1=[int]$g['v1'].Value; v2=[int]$g['v2'].Value
                pwm1=[int]$g['p1'].Value; pwm2=[int]$g['p2'].Value
                c1=[int]$g['c1'].Value; c2=[int]$g['c2'].Value
                dv=[int]$g['dv'].Value; dw=[int]$g['dw'].Value
                yaw=[double]$g['y'].Value/10.0; wz=[double]$g['w'].Value/100.0 })
        }
    }
}
function Wait([double]$sec) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Drain; Start-Sleep -Milliseconds 20 } }

# Wait for the gyro bias calibration to ACTUALLY finish rather than guessing a delay.
# While g_cal_left > 0 the firmware skips the integration branch entirely: yaw never accumulates
# and wz freezes at its last value. A too-short fixed sleep therefore produces a run whose heading
# data is silently frozen and reads as a perfect 0.0 deg of drift. Two independent signals, because
# the wireless link is lossy: the one-off `[imu] cal done`, and the CAL flag leaving the telemetry.
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

# Confirm a mode change from the TELEMETRY, not from the fact that we called Send().
# The wireless bridge is UDP with no retransmit and the DOWNLINK drops too (measured 15-22% loss
# once the car is on the floor). A lost `n1000` means the car never moves and the capture returns
# zeros - which reads like "the motor is broken" instead of "the command never arrived".
function SendConfirm([string]$cmd, [string]$wantMode, [int]$tries = 4) {
    for ($t = 1; $t -le $tries; $t++) {
        Send $cmd
        $rows.Clear(); Wait 0.6
        if (($rows | Where-Object { $_.mode -eq $wantMode }).Count -gt 0) {
            L ("  '{0}' confirmed on attempt {1} (mode={2})" -f $cmd, $t, $wantMode)
            return $true
        }
        L ("  attempt {0}: mode never became {1} - resending" -f $t, $wantMode)
    }
    return $false
}

L "================ nav_test  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port  telemetry f$TelemMs"
if ($Straight) { L ("mode: STRAIGHT {0} mm" -f $Straight) }
if ($Turn)     { L ("mode: TURN {0} deg (+ = left)" -f $Turn) }
if ($CalTurn)  { L ("mode: CAL-TURN {0} deg open loop at {1} rpm" -f $CalTurn, $SpinRpm) }
L "CLEAR SPACE AROUND THE CAR. NOTHING TETHERED."
L ""

Send "z"; Start-Sleep -Milliseconds 400
Send "f$TelemMs"; Start-Sleep -Milliseconds 300

# --- push calibration values -------------------------------------------------------
if ($CountsPerMm -gt 0)  { Send ("c" + [int][Math]::Round($CountsPerMm  * 100)); Start-Sleep -Milliseconds 300 }
if ($CountsPerDeg -gt 0) { Send ("q" + [int][Math]::Round($CountsPerDeg * 100)); Start-Sleep -Milliseconds 300 }
$script:calLine = $null
Send "c0"; Wait 0.6      # c0 = read back only
if ($script:calLine) { L ("calibration in firmware : {0}" -f $script:calLine) }
else                 { L "calibration in firmware : NO READBACK (link problem?)" }

# A straight run in mm is impossible without the odometry calibration, and the firmware will say
# so (FAIL=NO_CAL). Catch it here so the car never even starts.
if ($Straight -ne 0 -and $script:calLine -and $script:calLine -match 'counts/mm\*100=(\d+)') {
    if ([int]$Matches[1] -le 0) {
        L ""
        L "ENC_COUNTS_PER_MM is 0 in the firmware => 'go N mm' cannot work (by design, it refuses)."
        L "Fix first, in this order:"
        L "  1) powershell -File run_straight.ps1 -Port $Port -Rpm 80 -RunSec 2 -DistMm <tape mm>"
        L "     -> it prints the counts/mm value"
        L "  2) re-run this script with -CountsPerMm <that value>   (RAM only, no reflash)"
        L "  3) once happy: back-fill ENC_COUNTS_PER_MM in config.h and commit"
        Finish "INCONCLUSIVE - odometry not calibrated, did NOT drive." 2
    }
}

# --- gyro bias calibration ---------------------------------------------------------
$headingExpected = $true
if ($NoCal) {
    $headingExpected = $false
    L "-- gyro bias calibration SKIPPED (-NoCal) --"
} else {
    # Calibrate at a SLOW telemetry rate, then speed up. The 400 samples are taken one per main
    # loop pass and the telemetry print is a blocking UART write, so a fast rate stretches the
    # calibration badly (measured on this car: 8.55 s at f20 vs 2.41 s at f200).
    Send "f200"; Start-Sleep -Milliseconds 300
    L "-- gyro bias calibration (k): keep the car STILL, IN ITS DRIVING POSE, hands off --"
    Send "k"
    $calSec = WaitCal $CalTimeout
    if ($calSec -lt 0) {
        $headingExpected = $false
        L ("  WARNING: no 'cal done' within {0:N1}s - calibration is STILL RUNNING." -f $CalTimeout)
        L "  While it runs the firmware does not integrate yaw, so heading data is dead."
    } else {
        L ("  cal done after {0:N2}s (nominal 2.0s; longer = main loop being stretched)" -f $calSec)
    }
    Send "f$TelemMs"; Start-Sleep -Milliseconds 300
    Send "o"; Start-Sleep -Milliseconds 300
}
[void]$sp.ReadExisting(); $rows.Clear(); $script:report = $null

L "-- baseline (0.6s still) --"
Wait 0.6
if ($rows.Count -lt 3) { Finish "INCONCLUSIVE - no telemetry before the run; aborted WITHOUT driving." 2 }
$c1_0 = $rows[$rows.Count-1].c1; $c2_0 = $rows[$rows.Count-1].c2
$yaw0 = $rows[$rows.Count-1].yaw
L ("  baseline C={0},{1}  yaw={2:N1}" -f $c1_0, $c2_0, $yaw0)

# =========================== CAL-TURN (open loop) ==================================
if ($CalTurn -ne 0) {
    L ""
    L ("-- open-loop spin: mark the floor and the chassis so you can see {0} deg go by --" -f $CalTurn)
    L "   (this measures counts per degree for the NO-IMU fallback; it does not use the gyro)"
    if (-not (SendConfirm "m7" "DRVC")) { Send "z"; Finish "INCONCLUSIVE - could not enter m7 (link dropping commands)." 2 }
    $dir = if ($CalTurn -ge 0) { 1 } else { -1 }
    Send "v0"; Start-Sleep -Milliseconds 200
    Send ("r" + ($dir * $SpinRpm))
    # Spin until the gyro says we got there if it is available, else fixed time and let the user
    # stop it. Using the gyro here is fine: we are calibrating the ENCODERS against it.
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 12.0) {
        Drain
        if ($rows.Count -gt 0) {
            $turned = $rows[$rows.Count-1].yaw - $yaw0
            if ([Math]::Abs($turned) -ge [Math]::Abs($CalTurn)) { break }
        }
        Start-Sleep -Milliseconds 20
    }
    Send "z"; Wait 1.0
    $last = $rows[$rows.Count-1]
    $dL = $last.c1 - $c1_0; $dR = $last.c2 - $c2_0
    $turnedYaw = $last.yaw - $yaw0
    L ""
    L "---- result ----"
    L ("encoder delta L / R     : {0} / {1} counts" -f $dL, $dR)
    L ("(R-L)                   : {0} counts" -f ($dR - $dL))
    L ("gyro says turned        : {0:N1} deg" -f $turnedYaw)
    if ($MeasDeg -ne 0) { L ("protractor says turned  : {0:N1} deg  <- authoritative" -f $MeasDeg) }
    $refDeg = if ($MeasDeg -ne 0) { $MeasDeg } else { $turnedYaw }
    if ([Math]::Abs($refDeg) -lt 5.0) { Finish "FAIL - the car barely turned; check it physically." 1 }
    $cpd = ($dR - $dL) / $refDeg
    L ""
    L "---- ENC_COUNTS_PER_DEG calibration ----"
    L ("=> ENC_COUNTS_PER_DEG   : {0:N4}  counts/deg   (reference: {1:N1} deg)" -f $cpd, $refDeg)
    L "   Push it live with:  -CountsPerDeg $([Math]::Round($cpd,4))   then back-fill config.h."
    if ($MeasDeg -eq 0) {
        Finish "INCONCLUSIVE - value computed against the GYRO, not an independent measurement. Re-run with -MeasDeg <protractor reading> for a number you can trust." 2
    }
    Finish "PASS - counts/deg computed against a protractor reading." 0
}

# =========================== STRAIGHT / TURN (closed loop) =========================
$cmd      = if ($Straight -ne 0) { "n$Straight" } else { "j$Turn" }
$wantMode = if ($Straight -ne 0) { "NAVS" }      else { "NAVT" }
L ""
L ("-- GO : {0} --" -f $cmd)
if (-not (SendConfirm $cmd $wantMode)) {
    Send "z"
    # Two very different causes look identical here, and the old message only named one of them.
    # Distinguish them with what ALREADY happened earlier in this same session: `f<ms>` and `k`
    # both landed (we have telemetry and a cal-done line), so the downlink demonstrably works and
    # the only remaining explanation is that THE FIRMWARE DOES NOT KNOW THIS COMMAND.
    # Cost of getting this wrong, measured on 2026-07-27: the old text sent us off to move the ESP
    # module and change WiFi channels, when the real cause was that the nav layer had never been
    # flashed - it existed only in the source tree.
    $linkAlive = ($script:calDone -or $rows.Count -gt 20)
    if ($script:calLine) {
        L "  The firmware DOES have the nav layer (it answered the c0 readback above) and the link"
        L "  is alive, so neither of the usual suspects fits. Look at the firmware's own reason:"
        L "  re-run and watch for a [nav] FAIL=... line, and check the move is not being refused"
        L "  (NO_CAL = odometry not calibrated, STALL = wheels blocked)."
    } elseif ($linkAlive) {
        L "  BUT the link is fine: telemetry arrived and earlier commands (f/k) did land in this"
        L "  same session. So this is NOT a link problem - the chip most likely does not have this"
        L "  command at all, i.e. it is running an older image than the source tree."
        L "  Decisive check: send c0 - a firmware WITH the nav layer answers [nav] counts/mm*100=..."
        L "  and one without it answers nothing. If the answer is missing: build.ps1 then flash.ps1."
        L "  Do NOT use 'telemetry has a NAV: field' as the version fingerprint - that field is only"
        L "  appended while in a nav mode, so it is absent in IDLE on BOTH versions."
    } else {
        L "  No telemetry arrived either, so the link itself is suspect."
        L "  Move the PC-side ESP module down near the floor / closer to the car, or try channel 1 or 6."
    }
    Finish "INCONCLUSIVE - could not confirm the move command; did NOT keep driving." 2
}

# Wait for the firmware's own scorecard. It prints one on DONE and on every failure reason, so
# "no scorecard" means either the link ate it or the firmware is still driving.
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Timeout -and -not $script:report) { Drain; Start-Sleep -Milliseconds 20 }
Send "z"; Wait 1.0

if (-not $script:report) {
    L ""
    L "no [nav] scorecard arrived. Either the link dropped it, or the move never finished"
    L "(the firmware would then have stopped itself via the silence timeout / 15 s hard cap)."
    Finish "INCONCLUSIVE - no scorecard within ${Timeout}s. Braked. Check the car and the link." 2
}

L ""
L "---- firmware scorecard ----"
L $script:report

# Parse `key=value` pairs; some keys carry a scale suffix like tgt_deg*10
$f = @{}
foreach ($m in [regex]::Matches($script:report, '([A-Za-z_]+(?:\*\d+)?)=(-?\d+)')) { $f[$m.Groups[1].Value] = [double]$m.Groups[2].Value }
$hdgSrc = if ($script:report -match 'hdg=(\w+)') { $Matches[1] } else { "?" }
$failTag = if ($script:report -match 'FAIL=(\w+)') { $Matches[1] } else { $null }
$warnTag = if ($script:report -match 'WARN=(\w+)') { $Matches[1] } else { $null }
$isDone  = $script:report -match '\[nav\]\s+DONE'

$last = $rows[$rows.Count-1]
L ""
L "---- measured ----"
L ("heading source          : {0}   (GYRO = closed on the gyro, ENC = encoder fallback, NONE = not corrected)" -f $hdgSrc)
L ("final mode / PWM        : {0} / {1},{2}   (must be IDLE 0,0)" -f $last.mode, $last.pwm1, $last.pwm2)
L ("encoder delta L / R     : {0} / {1}" -f ($last.c1 - $c1_0), ($last.c2 - $c2_0))
L ("yaw net change          : {0:N1} deg" -f ($last.yaw - $yaw0))
if ($Csv -ne "") { $rows | Export-Csv -Path $Csv -NoTypeInformation -Encoding ASCII; L ("CSV written             : {0}" -f $Csv) }

if ($failTag) { Finish "FAIL - firmware stopped the move: $failTag  (NO_CAL=calibrate odometry / STALL=blocked or wheels not turning / OFFCOURSE=drifted too far / NO_HDG=send k first)" 1 }
if ($warnTag -eq 'NO_HDG') {
    L ""
    L "WARN=NO_HDG : this run had NO heading correction at all (no gyro calibration, no encoder"
    L "fallback). Even if it went straight, that is NOT evidence that the heading loop works."
}
if (-not $isDone) { Finish "FAIL - the move did not complete (scorecard says STOP)." 1 }
if ($last.mode -ne 'IDLE' -or $last.pwm1 -ne 0 -or $last.pwm2 -ne 0) { Finish "FAIL - car did not come to a stop. CHECK IT PHYSICALLY." 1 }

# --- acceptance against an INDEPENDENT measurement ---------------------------------
if ($Straight -ne 0) {
    $doneMm = if ($f.ContainsKey('done_mm')) { $f['done_mm'] } else { 0 }
    $errMm  = if ($f.ContainsKey('err_mm'))  { $f['err_mm'] }  else { 0 }
    $peak   = if ($f.ContainsKey('peak_hdg_deg*10')) { $f['peak_hdg_deg*10'] / 10.0 } else { 0 }
    L ""
    L ("odometry says travelled : {0:N0} mm   (residual err {1:N0} mm, firmware tolerance is the floor here)" -f $doneMm, $errMm)
    L ("peak heading deviation  : {0:N1} deg" -f $peak)
    if ($DistMm -le 0) {
        L ""
        L "No -DistMm given, so the distance above is the ENCODERS grading their own homework."
        L "Tape-measure the actual travel and re-run with -DistMm <mm>."
        Finish "INCONCLUSIVE - move completed and stopped cleanly, but distance is unverified." 2
    }
    $realErr = $DistMm - $Straight
    $lat = $DistMm * [Math]::Sin($peak * [Math]::PI / 180.0)
    L ("tape says travelled     : {0} mm  => REAL error {1:N0} mm (tolerance {2:N0})" -f $DistMm, $realErr, $TolMm)
    L ("implied lateral offset  : ~{0:N0} mm at peak heading dev (ladder 3 wants <=50 mm over 2 m)" -f $lat)
    L "   NOTE that is an estimate from the heading; the authoritative lateral offset is measured"
    L "   with the tape at the finish line, perpendicular to the intended path."
    if ([Math]::Abs($realErr) -gt $TolMm) {
        L ""
        L "The odometry and the tape disagree by more than the tolerance => ENC_COUNTS_PER_MM is off."
        L ("Suggested value: current x {0:N4}   (i.e. scale it by measured/commanded)" -f ($DistMm / [double]$Straight))
        Finish "FAIL - real distance error $([Math]::Round($realErr)) mm exceeds $TolMm mm." 1
    }
    Finish "PASS - travelled $DistMm mm for a commanded $Straight mm (error $([Math]::Round($realErr)) mm), stopped cleanly." 0
}

# turn
$doneDeg = if ($f.ContainsKey('done_deg*10')) { $f['done_deg*10'] / 10.0 } else { 0 }
$errDeg  = if ($f.ContainsKey('err_deg*10'))  { $f['err_deg*10'] / 10.0 }  else { 0 }
L ""
L ("gyro says turned        : {0:N1} deg   (residual err {1:N1} deg)" -f $doneDeg, $errDeg)
if ($MeasDeg -eq 0) {
    L ""
    L "No -MeasDeg given, so the angle above is the GYRO grading its own homework - and a wrong"
    L "yaw axis / sign / scale would still report success. Measure with the protractor (mark the"
    L "floor before the turn) and re-run with -MeasDeg <deg>."
    Finish "INCONCLUSIVE - turn completed and stopped cleanly, but the angle is unverified." 2
}
$realErr = $MeasDeg - $Turn
L ("protractor says turned  : {0:N1} deg  => REAL error {1:N1} deg (tolerance {2:N1})" -f $MeasDeg, $realErr, $TolDeg)
if ([Math]::Abs($MeasDeg - $doneDeg) -gt 2.0 * $TolDeg) {
    L ""
    L "The gyro and the protractor disagree well beyond tolerance. That is a SCALE problem, not a"
    L "tuning problem: check the yaw axis / sign (imu_axis.ps1), and that `k` was done in the car's"
    L "real driving pose. Tuning gains will not fix a scale error."
}
if ([Math]::Abs($realErr) -gt $TolDeg) { Finish "FAIL - real angle error $([Math]::Round($realErr,1)) deg exceeds $TolDeg deg." 1 }
Finish "PASS - turned $MeasDeg deg for a commanded $Turn deg (error $([Math]::Round($realErr,1)) deg), stopped cleanly." 0
