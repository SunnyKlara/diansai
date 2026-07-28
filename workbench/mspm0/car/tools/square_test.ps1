# square_test.ps1 - closed-loop ODOMETRY CLOSURE test: drive a polygon and come back to the start.
#
# WHY THIS SCRIPT EXISTS (read before "improving" it)
# ---------------------------------------------------
# Every primitive has been proven ALONE on this car: n300 lands within ~1 mm, j90 lands within
# 0.2 deg, the heading hold keeps peak error under 1.2 deg. What has never been done is CHAINING
# them, and chaining is where the interesting failures live:
#   * a systematic turn bias delta shows up 4x over a square, invisible in one j90
#   * the heading reference is RELATIVE (nav_start_* latches yaw0 on entry), so every segment
#     starts from wherever the previous one ended => errors ACCUMULATE, they are not reset
#   * repeatability: almost every "PASS" on this car so far is n=1
# One 4-segment square gives 4 straights + 4 turns + the accumulation, and collapses the whole
# foundation into ONE number a tape measure can check: how far from the start did it end up.
#
# THE DIAGNOSTIC THAT MATTERS: this script computes the closure TWICE.
#   internal  = dead reckoning from the car's own sensors (encoder deltas + gyro yaw)
#   external  = what you measure with a tape (-MeasCloseMm)
#   internal small + external big  => the SCALE is wrong (sensors lie: counts/mm or gyro gain)
#   internal big                   => the CONTROL never got there (STALL / tolerance too loose)
# Without both numbers you cannot tell those two apart, and they need opposite fixes.
#
# !! KNOWN BLIND SPOT - DO NOT SKIP -MeasSideMm !!
# Closure is BLIND to a uniform odometry scale error. Verified offline with the same dead-reckoning
# maths this script uses: if ENC_COUNTS_PER_MM is 5% off, every commanded 1000 mm leg physically
# travels 1050 mm, the square still closes perfectly, and BOTH the internal and the external closure
# read 0 mm. The car just drives a bigger square. (For contrast, a mere +2 deg bias on each turn
# blows the closure out to 97 mm - closure is very sensitive to angle, totally insensitive to scale.)
# So "closure PASS" alone would happily certify a car that overshoots every distance by 5% - which
# is exactly what breaks a "stop at the marked spot" requirement. Always measure one side too:
# use -PauseForMark to stop at each corner, mark the floor, then pass -MeasSideMm <tape mm>.
#
# SAFETY / PRE-CONDITIONS
#   * wheels ON THE FLOOR, nothing tethered. Take the RST wire OFF the board before going
#     untethered - a powered-down DAP clamps nRESET and holds the MCU in reset while the LCD
#     stays lit and the ESP keeps broadcasting (looks exactly like "firmware crashed").
#   * clear a square of (Side + 400) mm on each edge.
#   * mark the START position AND the start heading on the floor (tape a corner, not just a dot -
#     you need the heading to score -MeasHeadDeg).
#   * one segment must finish inside CFG_RUN_MS_HARDCAP (15 s, cannot be bypassed). At the
#     120 rpm cruise this car does ~368 mm/s => stay at or under 3000 mm per side.
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE (includes "did not drive at all").
#
# Examples
#   1) dry run, nobody measures anything - tells you if it can even chain 8 commands cleanly:
#      powershell -File square_test.ps1 -Port COM4 -Side 1000
#        -> INCONCLUSIVE by design, but prints the internal closure and the per-leg repeatability
#   2) the real acceptance run (mark the floor at each corner during the pauses):
#      powershell -File square_test.ps1 -Port COM4 -Side 1000 -PauseForMark `
#           -MeasSideMm 1010 -MeasCloseMm 38 -MeasHeadDeg 4 -Out _logs\sq1.txt -Csv _logs\sq1.csv
#   3) out-and-back instead of a square - isolates straight+180 from the 4x turn accumulation:
#      powershell -File square_test.ps1 -Port COM4 -Side 1500 -Sides 2 -TurnDeg 180

param(
    [string]$Port         = "COM4",
    [int]$Baud            = 115200,
    [int]$Side            = 1000,      # mm per straight segment
    [int]$Sides           = 4,         # number of straight segments (4 = square)
    [int]$TurnDeg         = 90,        # magnitude of each turn, deg
    [ValidateSet("L","R")]
    [string]$Dir          = "L",       # L = turn left (j positive), R = turn right
    [double]$MeasCloseMm  = -1,        # TAPE: distance from start to final position. Needed for PASS.
    [double]$MeasHeadDeg  = -1,        # PROTRACTOR/eyeball: final heading error vs start heading.
    [double]$MeasSideMm   = -1,        # TAPE: ONE actual side length. The only check on absolute
                                       # scale - closure cannot see a uniform scale error (see header).
    [double]$TolCloseMm   = 50,        # acceptance: closure error (default 5% of a 1 m side)
    [double]$TolHeadDeg   = 5,
    [double]$TolSidePct   = 2,         # acceptance: |MeasSideMm - Side| / Side
    [switch]$PauseForMark,             # stop after every leg and wait for Enter, so you can mark the
                                       # floor at each corner. Makes -MeasSideMm actually measurable
                                       # and leaves you the traced path to look at.
    [int]$TelemMs         = 50,
    [switch]$NoCal,                    # skip the gyro bias calibration `k` (you almost never want this)
    [double]$CalTimeout   = 15.0,
    [int]$SettleMs        = 1200,      # pause between segments: let the chassis stop rocking
    [double]$SegTimeout   = 20.0,      # per-segment wait for the [nav] scorecard
    [string]$Csv          = "",
    # Default follows the existing tools convention (*_out.txt next to wherever you ran it); pass
    # -Out _logs\xxx.txt -Csv _logs\xxx.csv when you want it filed properly. Both are gitignored.
    [string]$Out          = "square_test_out.txt"
)

# ---------------------------------------------------------------- guards (before opening the port)
if ($Side -le 0)   { Write-Host "-Side must be > 0" -ForegroundColor Red; exit 2 }
if ($Sides -lt 2)  { Write-Host "-Sides must be >= 2" -ForegroundColor Red; exit 2 }
if ($TurnDeg -eq 0){ Write-Host "-TurnDeg must be non-zero" -ForegroundColor Red; exit 2 }

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }
function Save() {
    $dir = Split-Path -Parent $Out
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    Set-Content $Out $log.ToString() -Encoding ASCII
}
function Finish([string]$verdict, [int]$code) {
    # Always try to stop the car, even on the error paths. `z` clears the queued drive command too
    # (the old bug was that re-entering a mode resumed the previous speed).
    try { if ($sp -and $sp.IsOpen) { Send "z" } } catch {}
    L ""
    L "RESULT: $verdict"
    Save
    try { $sp.Close(); $sp.Dispose() } catch {}
    exit $code
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

# One char at a time with a gap: a single burst write overruns the MCU RX FIFO and drops bytes.
function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

# ---------------------------------------------------------------- line assembly
# ReadExisting() cuts wherever the driver buffered, so a raw chunk normally ends mid-line. Keep a
# rolling buffer and only inspect newline-terminated lines. Splitting each chunk on its own is the
# bug that once manufactured a 20.4% "packet loss" on a link that was actually clean.
$script:rxbuf   = ''
$script:calDone = $false
$script:calSeen = $false
$script:calFlag = $false
$script:report  = $null
$script:calLine = $null
$script:statLine = $null
$rows = New-Object System.Collections.Generic.List[object]

$reTelem  = [regex]'\[ctl\]\s+(?<mode>\S+).*?\|\s*V:(?<v1>-?\d+),(?<v2>-?\d+).*?\|\s*PWM:(?<p1>-?\d+),(?<p2>-?\d+).*?\|\s*C:(?<c1>-?\d+),(?<c2>-?\d+).*?\|\s*D:(?<dv>-?\d+),(?<dw>-?\d+).*?\|\s*Y:(?<y>-?\d+)\s+W:(?<w>-?\d+)'
$reReport = [regex]'\[nav\]\s+(?<verdict>DONE|STOP)\s+(?<kind>STRAIGHT|TURN)'

function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i  = $script:rxbuf.IndexOf("`n")
        $ln = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)

        if ($ln -match '\[imu\]\s+cal done')      { $script:calDone = $true }
        if ($ln -match 't\d+\s+CAL\s*$')          { $script:calSeen = $true; $script:calFlag = $true }
        elseif ($ln -match 't\d+\s*$')            { $script:calFlag = $false }
        if ($ln -match '\[nav\]\s+counts/mm')     { $script:calLine  = $ln }
        if ($ln -match 'dz_drv=')                 { $script:statLine = $ln }
        if ($reReport.IsMatch($ln))               { $script:report   = $ln }

        $m = $reTelem.Match($ln)
        if ($m.Success) {
            $g = $m.Groups
            $rows.Add([pscustomobject]@{
                mode = $g['mode'].Value
                pwm1 = [int]$g['p1'].Value; pwm2 = [int]$g['p2'].Value
                c1   = [int]$g['c1'].Value; c2   = [int]$g['c2'].Value
                yaw  = [double]$g['y'].Value / 10.0
                wz   = [double]$g['w'].Value / 100.0 })
        }
    }
}
function Wait([double]$sec) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $sec) { Drain; Start-Sleep -Milliseconds 20 } }

# Wait for the gyro bias calibration to ACTUALLY finish rather than guessing a delay. While
# g_cal_left > 0 the firmware skips the integration branch: yaw never accumulates and wz freezes.
# A too-short fixed sleep produces a run whose heading data is silently frozen and then reads as a
# suspiciously perfect 0.0 deg of drift (measured: 8.55 s at f20 vs 2.41 s at f200).
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

# Confirm a mode change from the TELEMETRY, not from the fact that Send() returned. The wireless
# bridge is UDP with no retransmit and the DOWNLINK drops too. A lost `n1000` means the car never
# moves, and the capture then reads like "the motor is broken" instead of "the command was lost".
function SendConfirm([string]$cmd, [string]$wantMode, [int]$tries = 4) {
    for ($t = 1; $t -le $tries; $t++) {
        Send $cmd
        Wait 0.7
        if (($rows | Where-Object { $_.mode -eq $wantMode }).Count -gt 0) { return $true }
        L ("    attempt {0}: mode never became {1} - resending" -f $t, $wantMode)
    }
    return $false
}

# Run one nav segment and return everything measured about it, or $null on a hard link failure.
function RunSegment([string]$cmd, [string]$wantMode, [string]$kind) {
    Drain; $rows.Clear(); $script:report = $null
    Wait 0.5
    if ($rows.Count -lt 2) { return $null }        # no telemetry at all => link died
    $b = $rows[$rows.Count-1]
    $c1a = $b.c1; $c2a = $b.c2; $ya = $b.yaw

    if (-not (SendConfirm $cmd $wantMode)) { return "LINK" }

    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $SegTimeout -and -not $script:report) { Drain; Start-Sleep -Milliseconds 20 }
    $rep = $script:report
    Wait ($SettleMs / 1000.0)                      # let it stop rocking; also catches trailing telemetry

    # If the link died during the leg there is nothing left to measure against. Returning $null here
    # (rather than indexing an empty list) is what makes the caller say INCONCLUSIVE instead of
    # blowing up with a stack trace while the car is possibly still rolling.
    if ($rows.Count -lt 1) { return $null }
    $last = $rows[$rows.Count-1]
    $o = [pscustomobject]@{
        kind = $kind; cmd = $cmd; report = $rep
        verdict = "TIMEOUT"; fail = ""; hdg = ""
        dC1 = $last.c1 - $c1a; dC2 = $last.c2 - $c2a
        dYaw = $last.yaw - $ya
        peakPwm = ($rows | ForEach-Object { [Math]::Max([Math]::Abs($_.pwm1), [Math]::Abs($_.pwm2)) } | Measure-Object -Maximum).Maximum
        doneMm = [double]::NaN; errMm = [double]::NaN
        peakHdg = [double]::NaN; doneDeg = [double]::NaN; errDeg = [double]::NaN
        secs = [Math]::Round($sw.Elapsed.TotalSeconds, 2)
    }
    if ($rep) {
        $m = $reReport.Match($rep); if ($m.Success) { $o.verdict = $m.Groups['verdict'].Value }
        if ($rep -match 'FAIL=(\S+)')             { $o.fail    = $Matches[1] }
        if ($rep -match 'hdg=(\S+)')              { $o.hdg     = $Matches[1] }
        if ($rep -match 'done_mm=(-?\d+)')        { $o.doneMm  = [double]$Matches[1] }
        if ($rep -match 'err_mm=(-?\d+)')         { $o.errMm   = [double]$Matches[1] }
        if ($rep -match 'peak_hdg_deg\*10=(-?\d+)'){ $o.peakHdg = [double]$Matches[1]/10.0 }
        if ($rep -match 'done_deg\*10=(-?\d+)')   { $o.doneDeg = [double]$Matches[1]/10.0 }
        if ($rep -match 'err_deg\*10=(-?\d+)')    { $o.errDeg  = [double]$Matches[1]/10.0 }
    }
    return $o
}

# ================================================================ preflight
$turn = if ($Dir -eq "L") { [Math]::Abs($TurnDeg) } else { -[Math]::Abs($TurnDeg) }

L "================ square_test  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ================"
L ("port {0}  telemetry f{1}" -f $Port, $TelemMs)
L ("path : {0} x {1} mm straights, {2} turns of {3} deg ({4})  => total turn {5} deg" -f `
    $Sides, $Side, $Sides, $turn, $(if($Dir -eq 'L'){'left'}else{'right'}), ($Sides * $turn))
L ("tolerance: closure <= {0} mm, final heading <= {1} deg" -f $TolCloseMm, $TolHeadDeg)
L ""
L "CHECKLIST  - wheels on the floor, nothing tethered, RST wire OFF the board"
L ("           - clear {0} x {0} mm plus margin" -f ($Side + 400))
L "           - START corner AND start heading marked on the floor"
L ""

if ($Side -gt 3000) {
    L ("WARNING: a {0} mm segment takes about {1:N1}s at the 120 rpm cruise, and CFG_RUN_MS_HARDCAP" -f $Side, (0.4 + ($Side - 224) / 368.0 + 0.63))
    L "         is 15 s and cannot be bypassed. Expect the segment to be cut short."
}

Send "z"; Start-Sleep -Milliseconds 400
Send "f$TelemMs"; Start-Sleep -Milliseconds 300

# odometry calibration: `n<mm>` is refused by the firmware when counts/mm is 0, by design.
$script:calLine = $null
Send "c0"; Wait 0.8
if ($script:calLine) { L ("odometry in firmware : {0}" -f $script:calLine) }
else { L "odometry in firmware : NO READBACK (link problem?)" }
if ($script:calLine -and $script:calLine -match 'counts/mm\*100=(\d+)') {
    if ([int]$Matches[1] -le 0) {
        L ""
        L "ENC_COUNTS_PER_MM is 0 on the chip => 'go N mm' cannot work. Calibrate first:"
        L "  run_straight.ps1 -Port $Port -Rpm 80 -RunSec 2 -DistMm <tape mm>   then  c<value*100>"
        Finish "INCONCLUSIVE - odometry not calibrated, did NOT drive." 2
    }
}

# Low-speed floor check. Read the history right or you will re-run a dead end:
#   * 2026-07-28 believed the end-of-turn STALL was caused by dz_drv=0 and "fixed" it with W10.
#     That whole batch (8 W x Kd runs, plus the single "W10 -> j90 = 90.2 deg") was RETRACTED on
#     07-29: the runs were mechanically contaminated and, more importantly, they were tuning the
#     WRONG LAYER.
#   * the real cause was the SPEED loop not delivering at low command: measured spin-in-place
#     delivery ratio r25 -> 55%, r40 -> 72%, r60 -> 86%, r90 -> 91%. Below ~55 rpm the PWM bounces
#     between the W floor and the breakaway floor, so the wheel crawls instead of turning.
#   * the fix was CFG_TURN_W_MIN 30 -> 55 (keep the turn loop inside the range the layer below can
#     actually honour). After that: j90 0.3 deg, j-90 1.1 deg, j360 x4 +/-0.3 deg per lap, gyro
#     scale verified against an external reference to <0.35%. NOT ONE PID GAIN WAS CHANGED.
# So dz_drv is still worth reporting (it is the low-speed PWM floor) but it is NOT the turn gate,
# and this script must not claim it is. CFG_TURN_W_MIN is compile-time and `?` does not read it
# back, so it cannot be checked from here - if turns stall, check that first, not the gains.
$script:statLine = $null
Send "?"; Wait 0.8
if ($script:statLine) {
    L ("firmware status      : {0}" -f $script:statLine)
    if ($script:statLine -match 'dz_drv=(\d+)') {
        $dzdrv = [int]$Matches[1]
        if ($dzdrv -le 0) {
            # A warning, NOT a stop: the chip that passed the whole turn acceptance run had
            # CFG_DRV_FF_DZ = 10, so 0 means you are on an older image than the verified one.
            L "  WARNING: dz_drv = 0 => the low-speed PWM floor is off, and this is NOT the image"
            L "           that passed the turn acceptance (that one had dz_drv=10)."
            L "           Set it for this session with:  uart_send.ps1 -Port $Port -Cmd W10"
            L "           Permanent: CFG_DRV_FF_DZ is already 10 in config.h - reflash."
            L "           Beware: 'wrote N bytes' cannot tell these builds apart when only a"
            L "           constant changed - confirm with `?` reporting dz_drv=10."
        }
    }
} else { L "firmware status      : NO READBACK" }

# ---------------------------------------------------------------- gyro bias
$headingTrusted = $true
if ($NoCal) {
    $headingTrusted = $false
    L "-- gyro bias calibration SKIPPED (-NoCal): heading will drift ~9.6 deg/min, yaw data is not trustworthy --"
} else {
    # Calibrate at a slow telemetry rate then speed back up: the 400 samples are taken one per main
    # loop pass and the telemetry print is a blocking UART write.
    Send "f200"; Start-Sleep -Milliseconds 300
    L "-- gyro bias calibration (k): car STILL, IN ITS DRIVING POSE, hands OFF --"
    L "   (do not hold it: hand tremor gets baked into the bias and then yaw creeps with the car parked)"
    Send "k"
    $calSec = WaitCal $CalTimeout
    if ($calSec -lt 0) {
        $headingTrusted = $false
        L ("  WARNING: no 'cal done' within {0:N1}s - calibration still running, heading data is dead." -f $CalTimeout)
    } else {
        L ("  cal done after {0:N2}s" -f $calSec)
    }
    Send "f$TelemMs"; Start-Sleep -Milliseconds 300
    Send "o"; Start-Sleep -Milliseconds 300      # yaw := 0
}

Drain; $rows.Clear(); Wait 0.8
if ($rows.Count -lt 3) { Finish "INCONCLUSIVE - no telemetry before the run; aborted WITHOUT driving." 2 }
$c1_0 = $rows[$rows.Count-1].c1; $c2_0 = $rows[$rows.Count-1].c2; $yaw_0 = $rows[$rows.Count-1].yaw
L ("baseline: C={0},{1}  yaw={2:N1}" -f $c1_0, $c2_0, $yaw_0)

# read counts/mm back as a number for the dead reckoning
$cpmm = 0.0
if ($script:calLine -and $script:calLine -match 'counts/mm\*100=(\d+)') { $cpmm = [int]$Matches[1] / 100.0 }
if ($cpmm -le 0) { $cpmm = 5.109 ; L "  (using ENC_COUNTS_PER_MM = 5.109 from config.h for the dead reckoning)" }

# ================================================================ drive the polygon
$segs = New-Object System.Collections.Generic.List[object]
$hardFail = ""
L ""
for ($k = 1; $k -le $Sides; $k++) {
    L ("---- leg {0}/{1} : straight n{2} ----" -f $k, $Sides, $Side)
    $s = RunSegment ("n" + $Side) "NAVS" "STRAIGHT"
    if ($null -eq $s) { Finish "INCONCLUSIVE - telemetry stopped mid-run; car may still be moving, sent z." 2 }
    if ($s -is [string]) { Finish "INCONCLUSIVE - command 'n$Side' never took (link dropping downlink)." 2 }
    $segs.Add($s)
    $ratio = if ($s.dC2 -ne 0) { [Math]::Round($s.dC1 / [double]$s.dC2, 4) } else { [double]::NaN }
    L ("  {0}  dC={1},{2} (L/R {3})  believed {4:N1} mm  dYaw {5:N1} deg  peakPWM {6}  {7:N1}s {8}" -f `
        $s.verdict, $s.dC1, $s.dC2, $ratio, (($s.dC1 + $s.dC2) / 2.0 / $cpmm), $s.dYaw, $s.peakPwm, $s.secs, $s.fail)
    if ($s.fail) { $hardFail = "leg $k straight: FAIL=$($s.fail)" ; break }
    if ($s.verdict -eq "TIMEOUT") { $hardFail = "leg $k straight: no scorecard within ${SegTimeout}s" ; break }
    if ($PauseForMark) {
        Send "z"                                   # belt and braces: it is already IDLE, but the car
        L  "    >> MARK THE FLOOR at this corner, then press Enter (car is stopped)"
        [void](Read-Host)
    }

    L ("---- leg {0}/{1} : turn j{2} ----" -f $k, $Sides, $turn)
    $t = RunSegment ("j" + $turn) "NAVT" "TURN"
    if ($null -eq $t) { Finish "INCONCLUSIVE - telemetry stopped mid-run; car may still be moving, sent z." 2 }
    if ($t -is [string]) { Finish "INCONCLUSIVE - command 'j$turn' never took (link dropping downlink)." 2 }
    $segs.Add($t)
    L ("  {0}  done {1:N1} deg  err {2:N1} deg  dYaw(telem) {3:N1}  dC={4},{5}  {6:N1}s {7}" -f `
        $t.verdict, $t.doneDeg, $t.errDeg, $t.dYaw, $t.dC1, $t.dC2, $t.secs, $t.fail)
    if ($t.fail) { $hardFail = "leg $k turn: FAIL=$($t.fail)" ; break }
    if ($t.verdict -eq "TIMEOUT") { $hardFail = "leg $k turn: no scorecard within ${SegTimeout}s" ; break }
}
Send "z"; Wait 0.5

# ================================================================ dead reckoning (internal closure)
# Integrate the car's OWN measurements: straight length from the encoder delta, heading change from
# the gyro yaw delta. This is deliberately NOT the firmware's done_mm - done_mm is the cut-throttle
# point, roughly CFG_NAV_COAST_MM short of where the car actually stops.
$x = 0.0; $y = 0.0; $th = 0.0
foreach ($s in $segs) {
    if ($s.kind -eq "STRAIGHT") {
        $d = ($s.dC1 + $s.dC2) / 2.0 / $cpmm
        $x += $d * [Math]::Cos($th * [Math]::PI / 180.0)
        $y += $d * [Math]::Sin($th * [Math]::PI / 180.0)
    } else {
        $th += $s.dYaw
    }
}
$internalClose = [Math]::Sqrt($x*$x + $y*$y)
$thWrapped = $th - [Math]::Round($th / 360.0) * 360.0     # heading error vs the start heading

$straights = @($segs | Where-Object { $_.kind -eq "STRAIGHT" })
$turns     = @($segs | Where-Object { $_.kind -eq "TURN" })

L ""
L "---- per-leg summary ----"
L ("{0,-4} {1,-9} {2,-8} {3,8} {4,8} {5,9} {6,8}" -f "#","kind","verdict","dC1","dC2","L/R","dYaw")
$i = 0
foreach ($s in $segs) {
    $i++
    $r = if ($s.dC2 -ne 0) { "{0:N4}" -f ($s.dC1 / [double]$s.dC2) } else { "-" }
    L ("{0,-4} {1,-9} {2,-8} {3,8} {4,8} {5,9} {6,8:N1}" -f $i, $s.kind, $s.verdict, $s.dC1, $s.dC2, $r, $s.dYaw)
}

L ""
L "---- straights ----"
if ($straights.Count -gt 0) {
    $bel = @($straights | ForEach-Object { ($_.dC1 + $_.dC2) / 2.0 / $cpmm })
    $st  = $bel | Measure-Object -Average -Maximum -Minimum
    $sd  = if ($bel.Count -gt 1) { [Math]::Sqrt((($bel | ForEach-Object { ($_ - $st.Average) * ($_ - $st.Average) } | Measure-Object -Sum).Sum) / ($bel.Count - 1)) } else { 0 }
    L ("  target {0} mm  |  believed avg {1:N1}  std {2:N1}  min {3:N1}  max {4:N1}  (n={5})" -f $Side, $st.Average, $sd, $st.Minimum, $st.Maximum, $bel.Count)
    L ("  <- std is the REPEATABILITY number this car has never had (every previous PASS was n=1)")
    $ratios = @($straights | Where-Object { $_.dC2 -ne 0 } | ForEach-Object { $_.dC1 / [double]$_.dC2 })
    if ($ratios.Count) {
        $rs = $ratios | Measure-Object -Minimum -Maximum
        L ("  L/R ratio range {0:N4} .. {1:N4}   (1.00 +/- 0.02 = the two wheels are tracking)" -f $rs.Minimum, $rs.Maximum)
    }
    $ph = @($straights | Where-Object { -not [double]::IsNaN($_.peakHdg) } | ForEach-Object { $_.peakHdg })
    if ($ph.Count) { L ("  peak heading error during the straights: max {0:N1} deg" -f (($ph | Measure-Object -Maximum).Maximum)) }
}

L ""
L "---- turns ----"
if ($turns.Count -gt 0) {
    $te = @($turns | Where-Object { -not [double]::IsNaN($_.errDeg) } | ForEach-Object { $_.errDeg })
    if ($te.Count) {
        $ts = $te | Measure-Object -Average -Minimum -Maximum
        L ("  internal err per turn: avg {0:N2}  min {1:N2}  max {2:N2} deg  (n={3})" -f $ts.Average, $ts.Minimum, $ts.Maximum, $te.Count)
        L ("  systematic bias x {0} turns = {1:N1} deg of accumulated heading error" -f $turns.Count, ($ts.Average * $turns.Count))
        L ("  <- this is the multiplier a single j90 cannot show you")
    }
    $sumYaw = ($turns | Measure-Object -Property dYaw -Sum).Sum
    L ("  sum of gyro dYaw over all turns: {0:N1} deg   (ideal {1})" -f $sumYaw, ($Sides * $turn))
}

L ""
L "---- absolute scale (the thing closure CANNOT see) ----"
if ($MeasSideMm -ge 0) {
    $sidePct = ($MeasSideMm - $Side) / [double]$Side * 100.0
    L ("  commanded {0} mm, tape says {1:N0} mm  =>  {2:+0.00;-0.00;0.00}%" -f $Side, $MeasSideMm, $sidePct)
    L ("  implied ENC_COUNTS_PER_MM = {0:N4}  (chip currently {1:N4})" -f ($cpmm * $MeasSideMm / $Side), $cpmm)
    L  "  NOTE the tape reading has its own uncertainty: +/-5 mm on a 1 m side is already +/-0.5%."
    L  "  Do NOT chase a difference smaller than your tape can resolve - that is how a correct value"
    L  "  gets replaced by noise (this car already settled that argument once at 0.11%)."
} else {
    L  "  NOT MEASURED. Closure cannot substitute: a uniform scale error closes the square perfectly."
    L  "  Re-run with -PauseForMark, mark the floor at each corner, then pass -MeasSideMm <tape mm>."
}

L ""
L "---- closure ----"
L ("  internal (dead reckoning from the car's own sensors): {0:N1} mm off the start, heading {1:N1} deg off" -f $internalClose, $thWrapped)
L ("    net displacement x={0:N1} y={1:N1} mm   (ideal 0,0)" -f $x, $y)
if ($MeasCloseMm -ge 0) { L ("  external (tape): {0:N1} mm" -f $MeasCloseMm) }
else                    { L  "  external (tape): NOT MEASURED  <- without it this run cannot PASS" }
if ($MeasHeadDeg -ge 0) { L ("  external heading error: {0:N1} deg" -f $MeasHeadDeg) }

# The diagnostic split. Only meaningful when both numbers exist.
if ($MeasCloseMm -ge 0) {
    L ""
    if ($internalClose -le $TolCloseMm -and $MeasCloseMm -gt $TolCloseMm) {
        L "  DIAGNOSIS: the car believes it closed but it did not => SCALE error."
        L "    the sensors are lying, not the controller. Suspects in order:"
        L "      1) ENC_COUNTS_PER_MM  (re-measure: it is wheel/tyre/floor specific)"
        L "      2) gyro scale         (verify with j360: a scale error shows up 4x there)"
        L "    tuning the PD gains will NOT help."
    } elseif ($internalClose -gt $TolCloseMm) {
        L "  DIAGNOSIS: the car itself says it did not close => CONTROL did not get there."
        L "    look for FAIL= on the legs above, then at CFG_NAV_TOL_MM / CFG_TURN_TOL_DEG"
        L "    (a loose tolerance banks error into every leg) and at dz_drv."
    } else {
        L "  both internal and external closure are inside tolerance => the scale AND the control agree."
    }
}

if ($Csv) {
    $dir = Split-Path -Parent $Csv
    if ($dir -and -not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    $segs | Select-Object kind, cmd, verdict, fail, hdg, dC1, dC2, dYaw, doneMm, errMm, peakHdg, doneDeg, errDeg, peakPwm, secs |
        Export-Csv -Path $Csv -NoTypeInformation -Encoding ASCII
    L ""
    L "csv -> $Csv"
}

# ================================================================ verdict
if ($hardFail) { Finish "FAIL - $hardFail (the polygon was cut short; nothing to score)" 1 }
if (-not $headingTrusted) { Finish "INCONCLUSIVE - gyro bias not calibrated, heading data untrustworthy." 2 }

# FAIL beats INCONCLUSIVE: if something we DID measure is out of tolerance, say so even when other
# readings are missing. The reverse order would let a missing tape reading hide a real failure.
$bad = @()
if ($MeasCloseMm -ge 0 -and $MeasCloseMm -gt $TolCloseMm) { $bad += ("closure {0:N1} mm > {1} mm" -f $MeasCloseMm, $TolCloseMm) }
if ($MeasHeadDeg -ge 0 -and $MeasHeadDeg -gt $TolHeadDeg) { $bad += ("final heading {0:N1} deg > {1} deg" -f $MeasHeadDeg, $TolHeadDeg) }
if ($MeasSideMm  -ge 0) {
    $pct = [Math]::Abs(($MeasSideMm - $Side) / [double]$Side * 100.0)
    if ($pct -gt $TolSidePct) { $bad += ("side length off by {0:N2}% > {1}%" -f $pct, $TolSidePct) }
}
if ($bad.Count) { Finish ("FAIL - " + ($bad -join "; ")) 1 }

# Three separate things have to be measured for a real PASS, and each covers a hole the other two
# cannot: closure = turn accuracy + accumulation, heading = "closed by luck while pointing wrong",
# side = absolute scale (invisible to closure - see the blind-spot note in the header).
$missing = @()
if ($MeasCloseMm -lt 0) { $missing += "-MeasCloseMm (tape: start to final position)" }
if ($MeasHeadDeg -lt 0) { $missing += "-MeasHeadDeg (final heading error vs the start mark)" }
if ($MeasSideMm  -lt 0) { $missing += "-MeasSideMm (tape: one actual side; the only absolute-scale check)" }
if ($missing.Count) {
    L ""
    L ("drove the whole polygon cleanly - internal closure {0:N1} mm, internal heading {1:N1} deg." -f $internalClose, $thWrapped)
    L  "Still missing, so this run cannot be scored:"
    foreach ($m in $missing) { L "  $m" }
    Finish ("INCONCLUSIVE - {0} of 3 external measurements missing; the car's own numbers cannot grade the car." -f $missing.Count) 2
}
Finish ("PASS - closure {0:N1} mm (<= {1}), heading {2:N1} deg (<= {3}), side {4:N0} mm vs {5} cmd, {6} legs no FAIL" -f `
    $MeasCloseMm, $TolCloseMm, $MeasHeadDeg, $TolHeadDeg, $MeasSideMm, $Side, $segs.Count) 0
