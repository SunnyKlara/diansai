# ball_ident.ps1 - identify the beam/ball plant FROM THE MACHINE, instead of measuring geometry by hand.
#
# WHY THIS EXISTS
#   config.h ships with guessed numbers for the beam: CFG_SERVO_US_PER_DEG (a leftover from the
#   Ackermann steering, wrong by ~9x for a beam), CFG_SERVO_CENTER_US (1500 = the servo's own travel
#   midpoint, NOT "beam level"), and CFG_BALL_CX_PER_MM (a placeholder 100.0). Every ball-loop gain
#   sits on top of those. Chasing them with a ruler needs the linkage geometry r / L_arm / L_rod and
#   still leaves the crank-slider nonlinearity unknown.
#
#   The controller does not actually need that geometry. It needs one composite gain:
#
#       ball acceleration  a = K_total * (us - us_center)          [mm/s^2 per us]
#       K_total = (5/7)*g * (d theta_beam / d us)
#
#   r, L_arm, L_rod and the crank-slider curve are ALL folded into that slope, and the slope is
#   directly measurable: tilt the beam by a fixed pulse width, let the ball roll from rest, watch
#   x(t) with the camera, fit a parabola -> a. Repeat for a few us and fit a straight line through
#   (us, a):
#       slope     -> K_total
#       zero      -> us_center   (the real "beam level" pulse width)
#       sign      -> CFG_BALL_SERVO_SIGN
#       flat part -> deadband + backlash, in us, hence in mm/s^2
#   One experiment, four results, zero geometry input.
#
# WHAT STILL NEEDS A HUMAN (physics, not laziness)
#   A camera cannot know how big what it sees is. Turning pixels into mm needs ONE external length.
#   Use the scale tape on the beam - that is also what the judges read. That is -Step Scale.
#
# REQUIREMENTS
#   * firmware with the BALL: telemetry field (boot banner build >= 2026-07-31). Older builds do not
#     report ball position outside m12 and this script cannot work.
#   * the vision link delivering frames (real camera, or tools/vision_test.ps1 style fake frames)
#   * END STOPS FITTED on both ends of the slot, or the ball will roll off the beam.
#
# SAFETY
#   Motors are never driven: this only writes servo pulse widths with U<us> from IDLE. The moving
#   part is the beam. Keep fingers clear of the linkage and fit the end stops first.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_ident.ps1 -Port COM4 -Step Scale
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_ident.ps1 -Port COM4 -Step Sweep -CxPerMm 12.34
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose.
param(
    [string]$Port      = "COM4",
    [int]$Baud         = 115200,
    [ValidateSet("Scale","Sweep")]
    [string]$Step      = "Sweep",

    # --- Scale step ---
    [double]$RefMm     = 100.0,   # distance between the two scale marks you will use (0 -> +RefMm)

    # --- Sweep step ---
    # cx units per mm. NOT something this script calibrates - it is a FIXED unit conversion, because
    # both vision implementations put x_mm*100 into the cx field (pi_vision/ball_vision.py line 19,
    # k230_vision/main.py) and CFG_BALL_CX_PER_MM is 100.0f to match. The real px->mm calibration
    # lives on the camera side (its `calib` block), on purpose: re-calibrating there is a json edit
    # instead of a reflash. -Step Scale VERIFIES that calibration; it does not replace it.
    [double]$CxPerMm   = 100.0,
    # Sweep points, rebuilt 2026-07-31 around the MEASURED mechanism travel.
    # The user drove the assembled beam from a signal generator: duty 6.7% = highest point, 4.7% = lowest.
    # At 50 Hz (20000 us period) that is 1340 us and 940 us, so the whole mechanism lives in 940..1340 and
    # its midpoint is 1140. The old default list (1400..1600, centred on 1500) sat ENTIRELY above the top
    # stop - every point would have been clamped to MAX_US and the fit would have been a flat line of
    # saturated samples, i.e. K_total ~ 0 and a meaningless us_center.
    # Points are spaced tighter near the middle: that is where the dead-zone joint fit needs resolution,
    # and it is also where the ball moves slowly enough to measure a clean acceleration.
    [string]$UsList    = "1000,1060,1100,1140,1180,1220,1280",
    # Longer capture is the single biggest accuracy lever: the standard error on the fitted
    # acceleration falls as 1/T^2, so 1400 ms is ~2.4x better than 900 ms. It is capped in practice by
    # -StopMm (the ball reaching the end of its run), so raising this costs nothing when the ball is slow
    # and is simply ignored when it is fast.
    [int]$CaptureMs    = 1400,    # per point; the ball only needs to travel 30..60 mm
    [double]$StopMm    = 60.0,    # stop capturing early once the ball has moved this far
    # Trajectory shape, mirrored from config.h 7.12 (CFG_BALL_TRAJ_*). Only used to work out how much
    # beam angle the TASK actually demands, so the authority verdict is derived rather than hardcoded.
    [double]$TrajAmpMm = 50.0,    # CFG_BALL_TRAJ_AMP_MM
    [double]$TrajTOut  = 1.2,     # CFG_BALL_TRAJ_T_OUT
    [double]$TrajTBack = 1.8,     # CFG_BALL_TRAJ_T_BACK
    [double]$BallKp    = 9.0,     # CFG_BALL_KP - only for the informational PD-saturation note
    # Seed the shuttle with values a previous run already identified, skipping the bootstrap. See the
    # chicken-and-egg note where these are used: without them a ball resting against an end stop makes
    # the run unrecoverable. 0 = not given, do the normal bootstrap.
    #
    # ⚠ Named SeedK/SeedUc on purpose, NOT KEst/UcEst. PowerShell variable names are case-INSENSITIVE, so
    # a parameter $KEst and the local $Kest further down are the SAME variable - and because the parameter
    # is [double]-constrained, the local's `$Kest = $null` silently became 0.0 rather than $null, which
    # made `if ($KEst -ne 0.0)` always false and sent the run down the bootstrap path with no warning.
    # Cost one full real-machine run on 2026-07-31 before the collision was spotted.
    [double]$SeedK     = 0.0,     # mm/s^2 per us, SIGN MATTERS (a wrong sign herds into the stop)
    [int]$SeedUc       = 0,       # us, the level pulse width
    [int]$SettleMs     = 250,     # after U<us>, ignore this long (servo slew + the ball breaking away)

    # --- Shuttle (default): reposition the ball by control, not by hand ---
    # Without this, every pulse width in -UsList needs a human to pick the ball up and put it back at
    # the centre - 7 points, 7 interruptions. The shuttle instead alternates sides of centre so each
    # measurement throws the ball back across the slot, ready for the next (opposite-sign) one, and
    # closes a soft PC-side loop (FitHerdUs, unit-tested in _fit_test.ps1) to park it upstream first.
    # It works because FitQuad fits x0 + v0*t + 0.5*a*t^2 : the ball does NOT have to start at rest or
    # at the centre, it only needs room to accelerate. Verified by test "v0 does not leak into a".
    [switch]$Manual,                # fall back to the old prompt-per-point flow
    [double]$GuardMm   = 100.0,     # abort a capture / brake if |x| exceeds this (end stops sit near 125)
    [double]$StartMm   = 45.0,      # park the ball this far UPSTREAM before a capture
    [int]$HerdMs       = 9000,      # give up herding after this long
    [double]$HerdKp    = 4.0,       # herd loop gains [1/s^2] / [1/s]; soft on purpose, see FitHerdUs
    [double]$HerdKd    = 4.0,
    [double]$HerdMaxUs = 90.0,      # never push harder than this many us off centre while herding

    [switch]$Yes,
    [string]$Out       = "_logs\ball\ball_ident_out.txt",
    [string]$Csv       = "",
    [string]$Json      = "_logs\ball\ball_ident_result.json",
    [string]$RunId     = ""
)

$ErrorActionPreference = "Continue"
$runSw = [System.Diagnostics.Stopwatch]::StartNew()
if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = "ident-" + (Get-Date -Format "yyyyMMdd-HHmmss") }
$carRoot = Split-Path $PSScriptRoot -Parent
function ResolveResultPath([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return "" }
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path $carRoot $p))
}
$Out = ResolveResultPath $Out
$Csv = ResolveResultPath $Csv
$Json = ResolveResultPath $Json
$result = [ordered]@{
    run_id = $RunId
    tool = "ball_ident"
    elapsed_ms = 0
    port = $Port
    parameters = [ordered]@{ step=$Step; us_list=$UsList; seed_k=$SeedK; seed_uc=$SeedUc; capture_ms=$CaptureMs; guard_mm=$GuardMm }
    health = [ordered]@{ telemetry_sink=$null; camera_frames=0; camera_age_median_ms=$null; guard_abort_count=0 }
    metrics = [ordered]@{}
    verdict = "INCONCLUSIVE"
    reason = "script did not reach Finish"
    next_action = "inspect raw_log and rerun"
    raw_log = $Out
}

# Mirror of CFG_SERVO_MIN_US / CFG_SERVO_MAX_US in config.h. The firmware clamps to these anyway
# (servo_us_clamp), so this copy is belt-and-braces for the PC-side herd loop - it keeps us from ever
# ASKING for something illegal, which in turn keeps the reported "firmware clamped X -> Y" notes
# meaningful instead of routine noise.
# Keep these in step with config.h. 2026-07-31: measured mechanism travel is 940..1340 us (signal
# generator on the assembled beam: 4.7% / 6.7% duty at 50 Hz), so config.h now clamps to 960..1320 with
# 20 us of margin - 20 us being the resolution of a 0.1% duty reading.
$CFG_MIN_US    = 960
$CFG_MAX_US    = 1320
$CFG_CENTER_US = 1140          # geometric middle of the measured travel; NOT proven to be beam-level

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = $null
function Finish([string]$v, [int]$c) {
    $runSw.Stop()
    $result.elapsed_ms = [int][Math]::Round($runSw.Elapsed.TotalMilliseconds)
    $result.verdict = $(if ($c -eq 0) { "PASS" } elseif ($c -eq 1) { "FAIL" } else { "INCONCLUSIVE" })
    $result.reason = $v
    $result.next_action = $(if ($c -eq 0) { "review metrics; only verified values may be copied to config.h" } else { "follow reason, correct the failed gate, then rerun" })
    L ""; L ("elapsed: {0:N1} s" -f $runSw.Elapsed.TotalSeconds); L "RESULT: $v"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding ASCII
        if ($Json) {
            $jd = Split-Path $Json -Parent
            if ($jd -and -not (Test-Path $jd)) { New-Item -ItemType Directory -Path $jd -Force | Out-Null }
            $result | ConvertTo-Json -Depth 7 | Set-Content $Json -Encoding UTF8
            Write-Host ("RESULT_JSON: {0}" -f $Json)
        }
    } catch { Write-Host "(could not write $Out / $Json)" }
    # Leave the board in a sane state: stop, restore dual-send telemetry, restore the normal rate.
    # Not restoring l3 is a nasty trap - the next script would read the other port, see silence, and
    # get blamed on hardware. Note `z` deliberately does NOT release the servo (stop_all leaves the
    # pulse width alone), so the beam keeps holding its last angle and the ball does not roll off.
    if ($sp) {
        try {
            foreach ($cmd in @("z", "l3", "f100")) {
                foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
                Start-Sleep -Milliseconds 120
            }
            $sp.Close(); $sp.Dispose()
        } catch {}
    }
    exit $c
}

# ---------------------------------------------------------------- math helpers
# Single implementation, in _fit.ps1, so it can be unit-tested without a serial port (_fit_test.ps1).
# It was originally inlined here; that copy carried two bugs the test then found in the shared version
# (PowerShell comma precedence in Solve3, and banker's rounding in Median). Keeping one copy is the
# whole point - a second copy means fixing every bug twice and finding it once.
. (Join-Path $PSScriptRoot "_fit.ps1")

# ---------------------------------------------------------------- serial plumbing
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL ({0}): {1}" -f $Port, $_.Exception.Message); Finish "INCONCLUSIVE - serial port could not be opened" 2 }

# Serial plumbing + BALL: parsing live in _serial_ball.ps1, shared with ball_signs.ps1.
# They are NOT boilerplate: the per-frame de-dupe, the id=-1 filter and the 25 ms/char pacing are each
# a correctness bug waiting to happen, and a second copy is how two scripts quietly disagree.
. (Join-Path $PSScriptRoot "_serial_ball.ps1")
# Soft PC-side loop that parks the ball near $targetMm and lets it settle.
# Returns PASS / SLOW / LOST. Never throws the ball: FitHerdUs clamps the push, and we bail on guard.
function HerdTo([double]$targetMm, [double]$K, [double]$uc, [double]$scaleMm, [int]$timeoutMs) {
    if ($null -eq $K -or [Math]::Abs($K) -lt 1e-9) { return "LOST" }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $settled = 0
    ClearSamples
    while ($sw.Elapsed.TotalMilliseconds -lt $timeoutMs) {
        $st = LatestXV $scaleMm
        if ($null -eq $st) { Start-Sleep -Milliseconds 60; continue }
        if ([Math]::Abs($st.x) -gt ($GuardMm + 25.0)) {
            # Past the guard by a margin: park level and give up rather than fight it.
            Send ("U" + [int]$uc); return "LOST"
        }
        # "Settled" needs BOTH position and speed. Position alone would pass a ball flying through the
        # target, and the next capture would then start with a big v0 heading for the end stop.
        if ([Math]::Abs($st.x - $targetMm) -lt 12.0 -and [Math]::Abs($st.v) -lt 25.0) {
            $settled++
            if ($settled -ge 3) { Send ("U" + [int]$uc); return "PASS" }
        } else { $settled = 0 }
        $cmd = FitHerdUs $st.x $st.v $targetMm $K $uc $HerdKp $HerdKd $CFG_MIN_US $CFG_MAX_US $HerdMaxUs
        if ($null -eq $cmd) { return "LOST" }
        Send ("U" + $cmd)          # ~150 ms per command (25 ms/char pacing) => loop runs at ~7 Hz
    }
    Send ("U" + [int]$uc)
    return "SLOW"
}

L "================ ball_ident  step=$Step  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ================"
L "port $Port"

# --- preflight: is the firmware new enough and is the camera alive? ------------------
# SetupTelemetry (in _serial_ball.ps1) sets f25 and locks to a single sink, and explains why both
# matter. Shared with ball_signs.ps1 so the two cannot end up sampling under different conditions -
# which would make their numbers quietly incomparable.
$sinkUsed = SetupTelemetry
if (-not $sinkUsed) { Finish "INCONCLUSIVE - no [ctl] telemetry at all on $Port. Wrong port, or nothing running." 2 }
$result.health.telemetry_sink = $sinkUsed
L ("telemetry: f25 (40 Hz), sink locked to {0}" -f $sinkUsed)
L "  (restore both sinks with l3 when you are done - boot default is dual-send; Finish does it for you)"

$script:lines = 0
$pre = Collect 1500 0 0
if ($script:lines -eq 0) { Finish "INCONCLUSIVE - no [ctl] telemetry at all on $Port. Wrong port, or nothing running." 2 }
if ($pre.Count -eq 0) {
    L ""
    L "Telemetry is flowing ($($script:lines) lines) but no BALL: field appeared."
    L "Two possible causes, check in this order:"
    L "  1) the firmware predates the BALL: field -> reflash (build banner must be 2026-07-31 or later)"
    L "  2) the vision link has never delivered a frame -> the field is only printed once have_frame"
    L "     is set. Verify with the V command, or feed a fake frame:  `$V,1,1234,0,0*<xor>"
    Finish "INCONCLUSIVE - cannot read ball position, so nothing can be identified." 2
}
$ages = @($pre | ForEach-Object { $_.age })
$result.health.camera_frames = $pre.Count
$result.health.camera_age_median_ms = Median $ages
L ("camera alive: {0} frames WITH a ball in 1.5 s (~{1:N1} fps), age median {2} ms, max {3} ms" -f `
    $pre.Count, ($pre.Count/1.5), (Median $ages), (($ages | Measure-Object -Maximum).Maximum))
L ("             {0} frame(s) reported id=-1 (link fine, no ball seen) - these are excluded from every fit" -f $script:notSeen)
if ((Median $ages) -gt 200) {
    L "WARNING: frame age is high - the camera is updating slowly or has stalled. The fits below will be poor."
}
if ($script:notSeen -gt $pre.Count) {
    L ""
    L "WARNING: the camera is missing the ball more often than it finds it. Fix that FIRST - every"
    L "  number below is computed from the frames where it did see the ball, so a low detection rate"
    L "  quietly turns into sparse, gappy fits. Usual causes: ROI not on the slot, auto-exposure still"
    L "  on (the ball is specular), or no background frame captured."
}
if ($pre.Count -eq 0) {
    Finish "INCONCLUSIVE - the link works but the camera never once located the ball. Fix detection before identifying anything (camera-side: ROI, fixed exposure, background frame)." 2
}

# =====================================================================================
if ($Step -eq "Scale") {
    L ""
    L "---- Step Scale : pixels -> mm, using the scale tape on the beam ----"
    L "This is the one number a camera cannot work out by itself."
    L "Use the SAME tape the judges will read. Beam roughly level. Ball resting, not rolling."
    L ""
    if (-not $Yes) { Write-Host "Put the ball at the 0 mark, let it settle, then press Enter." -ForegroundColor Yellow; [void](Read-Host) }
    $s0 = Collect 1500 0 0
    if ($s0.Count -lt 5) { Finish "INCONCLUSIVE - too few frames at the 0 mark ($($s0.Count))." 2 }
    $cx0arr = @($s0 | ForEach-Object { [double]$_.cx })
    $cx0 = Median $cx0arr; $n0 = Stdev $cx0arr
    L ("  at    0 mm : cx median {0}   noise std {1:N2} px   n={2}" -f $cx0, $n0, $s0.Count)

    if (-not $Yes) { Write-Host "Now put the ball at the +$RefMm mm mark (the POSITIVE / driven-end side), then press Enter." -ForegroundColor Yellow; [void](Read-Host) }
    $s1 = Collect 1500 0 0
    if ($s1.Count -lt 5) { Finish "INCONCLUSIVE - too few frames at the +$RefMm mm mark ($($s1.Count))." 2 }
    $cx1arr = @($s1 | ForEach-Object { [double]$_.cx })
    $cx1 = Median $cx1arr; $n1 = Stdev $cx1arr
    L ("  at {0,4:N0} mm : cx median {1}   noise std {2:N2} px   n={3}" -f $RefMm, $cx1, $n1, $s1.Count)

    # cx carries x_mm*100, so the two readings ALREADY are millimetres. This step therefore checks the
    # camera-side calibration rather than producing a new constant:
    #   zero  : ball at the 0 mark must read cx ~ 0
    #   span  : moving RefMm must change cx by RefMm*100
    #   sign  : cx must grow toward the positive (driven-end) side, matching figure 3(a)
    $zeroMm  = $cx0 / 100.0
    $spanMm  = ($cx1 - $cx0) / 100.0
    $spanErr = $spanMm - $RefMm
    $noiseCx = [Math]::Max($n0, $n1)
    $noiseMm = $noiseCx / 100.0
    L ""
    L "---- verdicts (cx already carries x_mm*100, so these are millimetres) ----"
    L ("  zero offset  : {0,8:N2} mm   (ball at the 0 mark should read 0)" -f $zeroMm)
    L ("  span         : {0,8:N2} mm   for a true {1:N0} mm move  -> error {2:+0.00;-0.00} mm ({3:+0.0;-0.0}%)" -f `
        $spanMm, $RefMm, $spanErr, (100.0*$spanErr/$RefMm))
    L ("  sign         : {0}" -f $(if ($spanMm -gt 0) { "OK - cx grows toward +x" } else { "WRONG - cx shrinks toward +x" }))
    L ("  static noise : {0,8:N3} mm   (M4 criterion: < 1 mm)" -f $noiseMm)

    $bad = 0
    if ([Math]::Abs($spanMm) -lt ($RefMm * 0.5)) {
        L ""
        L "FAIL: span is less than half of what it should be. Before touching calibration, check that the"
        L "  ball actually moved, that the ROI covers the whole slot, and that the camera sees the ball at"
        L "  both ends - a truncated ROI looks exactly like a scale error."
        $bad++
    }
    if ($spanMm -lt 0) {
        L ""
        L "FAIL: sign is inverted. Fix it ON THE CAMERA SIDE by swapping p1_mm / p2_mm in its calib block."
        L "  Do NOT try to fix it with a negative CFG_BALL_CX_PER_MM - the firmware assumes it is positive."
        $bad++
    }
    L ""
    L "---- what to change, and WHERE ----"
    L "  Nothing in config.h. CFG_BALL_CX_PER_MM = 100.0f is a fixed unit conversion, not a calibration."
    L "  The px->mm calibration lives on the camera:  calib = { p1_px, p1_mm, p2_px, p2_mm }"
    if ([Math]::Abs($spanErr) -gt 2.0 -and [Math]::Abs($spanMm) -gt 1.0) {
        $k = $RefMm / $spanMm
        L ("  Span is off by {0:N1}%. Scale the camera's mm range by {1:N4}: keep p1_px/p2_px, and multiply" -f (100.0*$spanErr/$RefMm), $k)
        L ("  (p1_mm, p2_mm) by that factor. Then re-run this step - it should come back within a few tenths.")
        $bad++
    }
    if ([Math]::Abs($zeroMm) -gt 3.0) {
        L ("  Zero is off by {0:N2} mm. Shift both p1_mm and p2_mm by {1:+0.00;-0.00} mm to move the origin onto" -f $zeroMm, (-$zeroMm))
        L ("  the scale's 0 mark. The origin MUST be the centre point O - requirement 6 has the judges call")
        L ("  out positions on that tape, so an offset origin loses those 20 points even with a perfect loop.")
        $bad++
    }
    if ($bad -eq 0) { L "  Both zero and span are within tolerance - the camera calibration is good as it stands." }
    L ""
    L "Not proven here: linearity across the whole slot. Repeat at -10/-5/0/+5/+10 cm and check the worst"
    L "deviation stays under 1.5 mm before trusting the far ends (M4 criterion). Two points cannot see a bend."
    if ($noiseMm -gt 1.0) { Finish ("FAIL - static noise {0:N3} mm exceeds the 1 mm budget; fix lighting / fix the exposure / narrow the ROI before any tuning." -f $noiseMm) 1 }
    if ($bad -gt 0) { Finish ("FAIL - camera calibration needs the edits listed above ({0} issue(s))." -f $bad) 1 }
    Finish ("PASS - camera calibration verified: zero {0:N2} mm, span error {1:+0.00;-0.00} mm, noise {2:N3} mm." -f $zeroMm, $spanErr, $noiseMm) 0
}

# =====================================================================================
L ""
L "---- Step Sweep : identify K_total, us_center, sign and deadband ----"
$usArr = @($UsList -split ',' | ForEach-Object { [int]($_.Trim()) } | Where-Object { $_ -gt 0 })
if ($usArr.Count -lt 3) { Finish "INCONCLUSIVE - need at least 3 pulse widths in -UsList." 2 }
$usMid = [int](Median @($usArr | ForEach-Object { [double]$_ }))
$unit  = if ($CxPerMm -gt 0) { "mm" } else { "px" }
$scale = if ($CxPerMm -gt 0) { 1.0/$CxPerMm } else { 1.0 }
L ("pulse widths : {0}" -f ($usArr -join ', '))
L ("units        : {0}   (CxPerMm={1})" -f $unit, $(if ($CxPerMm -gt 0) { "{0:N4}" -f $CxPerMm } else { "not given - run -Step Scale first for real units" }))
L ("stop-early   : ball travels {0:N0} mm  (or {1} ms, whichever first)" -f $StopMm, $CaptureMs)
$stopCx = if ($CxPerMm -gt 0) { $StopMm * $CxPerMm } else { 0 }

if (-not $Yes) {
    Write-Host ""
    Write-Host "END STOPS must be fitted on both ends of the slot - the ball will accelerate freely." -ForegroundColor Yellow
    Write-Host "The beam will move. Keep fingers clear of the linkage." -ForegroundColor Yellow
    Write-Host "Type YES to continue:" -ForegroundColor Yellow
    if ((Read-Host) -ne "YES") { Finish "aborted by operator" 2 }
}

$pts = New-Object System.Collections.Generic.List[object]
$guardCx = if ($CxPerMm -gt 0) { $GuardMm * $CxPerMm } else { 0 }
$script:identGuardStreak = 0

# One measurement at one pulse width. Shared by both flows so they cannot drift apart.
# Command the pulse width and CONFIRM the board actually took it, retrying a couple of times.
#
# WHY: on 2026-07-31 the sweep's bootstrap point silently measured nothing - the board reported
# servo_us()=0 for the whole 1.4 s window, so the beam never moved, the fit got a=0, and that bogus
# point then poisoned the bootstrap estimate of the centre (it came out ~0 us), which in turn broke
# every subsequent herd. One lost command cost the entire run.
# The root cause was NOT reproducible afterwards: U1280 took effect first try from limp, from a parked
# state, and after `z`. Three attempts to reproduce, three failures - so per the repo's own debugging
# rule, stop guessing and measure instead. Verifying the read-back makes the run self-healing whatever
# the cause was, and costs ~1 s per point.
# The expected value is the CLAMPED one: the firmware limits to CFG_SERVO_MIN_US..MAX_US, so asking for
# something outside that legitimately reads back as the limit, which is not a failure. U itself emits
# [srv], so wait for that acknowledgement directly; asking `?` afterwards used to burn the full 1.2 s
# diagnostic window at every point even when the board had already answered.
function SetUsVerified([int]$us) {
    $want = $us
    if ($want -ne 0) {
        if ($want -lt $CFG_MIN_US) { $want = $CFG_MIN_US }
        if ($want -gt $CFG_MAX_US) { $want = $CFG_MAX_US }
    }
    for ($k = 1; $k -le 3; $k++) {
        $reply = SendAndWaitLine ("U" + $us) '^\[srv\]' 0.8
        $got = $null
        if ($null -ne $reply -and $reply -match 'us=(-?\d+)') { $got = [int]$Matches[1] }
        if ($null -ne $got -and $got -eq $want) { return $want }
        L ("  U{0}: board reports us={1}, expected {2} - resending (attempt {3}/3)" -f `
            $us, $(if ($null -eq $got) { "?" } else { $got }), $want, $k)
    }
    L  "  the board never confirmed the pulse width. This point would measure a beam that is not moving,"
    L  "  which is worse than no point at all (it fits as a=0 and drags the centre estimate), so skip it."
    return $null
}

function CaptureAt([int]$us, [double]$parkUs) {
    # A failed acknowledgement returns before Collect(), so clear the previous capture's guard flag here;
    # otherwise one old guard hit can be counted again as a new failure.
    $script:lastGuardHit = $false
    if ($null -eq (SetUsVerified $us)) { return $null }
    Start-Sleep -Milliseconds $SettleMs      # skip the servo slew and the ball breaking away from rest
    $s = Collect $CaptureMs $stopCx $guardCx
    Send ("U" + [int]$parkUs)                # park level: stops the ball accelerating any further
    if ($script:lastGuardHit) {
        $script:identGuardStreak++
        $result.health.guard_abort_count = [int]$result.health.guard_abort_count + 1
        L ("  guard hit (|x| >= {0:N0} mm) - capture cut short, parked level ({1}/2 consecutive)" -f `
            $GuardMm, $script:identGuardStreak)
        if ($script:identGuardStreak -ge 2) {
            Finish "INCONCLUSIVE - two consecutive captures hit an end guard; stop and recover/reposition before measuring more points." 2
        }
    } else {
        $script:identGuardStreak = 0
    }
    if ($s.Count -lt 6) {
        L ("  only {0} fresh frames - point skipped (camera too slow, ball out of view, or id=-1)" -f $s.Count)
        return $null
    }
    $t0 = $s[0].t
    $ts = @($s | ForEach-Object { $_.t - $t0 })
    $xs = @($s | ForEach-Object { [double]$_.cx * $scale })
    $f  = FitQuad $ts $xs
    if (-not $f) { L "  fit failed"; return $null }
    # us actually written by the firmware (it clamps to CFG_SERVO_MIN_US/MAX_US) - trust the board, not us
    $usAct = Median @($s | ForEach-Object { [double]$_.us })
    if ([int]$usAct -ne $us) { L ("  NOTE: firmware clamped {0} -> {1} us (CFG_SERVO_MIN_US/MAX_US). Using the clamped value." -f $us, [int]$usAct) }
    L ("  n={0}  dur={1:N2}s  x0={2:N1}  v0={3:N1}  travel={4:N1}{5}  a={6:N1} {5}/s^2  rms={7:N2}{5}" -f `
        $f.n, $f.dur, $f.x0, $f.v0, $f.span, $unit, $f.a, $f.rms)
    return [pscustomobject]@{ us=[double]$usAct; a=$f.a; rms=$f.rms; n=$f.n; travel=$f.span; dur=$f.dur }
}

# try/finally around everything that DRIVES THE BEAM.
# WHY: the beam is driven by `U` from IDLE, and run_limit_ms() returns 0 for IDLE - i.e. the firmware
# has NO timeout that would flatten the beam if we stopped talking to it. So if this script throws, or
# you hit Ctrl-C mid-capture, the beam keeps its last tilt and the ball runs to the end stop and stays
# pressed against it. Not damaging (the herd push is clamped to ~1 deg, the servo will not stall), but
# it costs a re-placement and it looks like the rig failed. The finally block always parks it level.
# Residual limitation, stated honestly: a hard kill (process termination) still bypasses this. The real
# fix would be a firmware-side "no command for N s -> centre the beam" gate, which needs the centre to
# be known - and the centre is exactly what we are here to measure. So PC-side is the right layer now.
try {
if ($Manual) {
    L ""
    L "flow: MANUAL - one prompt per point."
    foreach ($us in $usArr) {
        L ""; L ("== us = {0} ==" -f $us)
        Send ("U" + $usMid); Start-Sleep -Milliseconds 400
        if (-not $Yes) { Write-Host "  Place the ball at the 0 mark, hold it, release it, then press Enter." -ForegroundColor Cyan; [void](Read-Host) }
        $p = CaptureAt $us $usMid
        if ($p) { $pts.Add($p) }
    }
} else {
    # ---------------- SHUTTLE ----------------
    # Phase 0 bootstrap. We cannot herd before we know K and centre, so start with the two EXTREME
    # pulse widths: they are the most likely to move the ball at all, and each one throws it back
    # toward the other end, so the pair needs no repositioning between them.
    L ""
    L "flow: SHUTTLE - the ball is repositioned by control, no hand needed after the first placement."
    L ("  guard {0:N0} mm | park upstream at {1:N0} mm | herd gains kp={2:N1} kd={3:N1}, push <= {4:N0} us" -f `
        $GuardMm, $StartMm, $HerdKp, $HerdKd, $HerdMaxUs)
    if ($guardCx -le 0) { Finish "INCONCLUSIVE - shuttle needs -CxPerMm (mm units) for its guard. Use -Manual, or pass -CxPerMm 100." 2 }
    if (-not $Yes) { Write-Host "  Put the ball anywhere in the slot (centre is fine), then press Enter - this is the ONLY placement." -ForegroundColor Cyan; [void](Read-Host) }

    $usLo = ($usArr | Measure-Object -Minimum).Minimum
    $usHi = ($usArr | Measure-Object -Maximum).Maximum
    $Kest = $null; $ucEst = $usMid

    # Seeded start: skip the bootstrap when a previous run already identified K and the centre.
    #
    # WHY: the shuttle has a chicken-and-egg. It cannot herd the ball without K and the centre, and it
    # cannot measure those if the ball starts pressed against an end stop - every capture then aborts on
    # the guard with 3 frames. That is exactly what happened on 2026-07-31 after some manual probing left
    # the ball at one end: run 2 died in the bootstrap having learnt nothing, even though run 1 had
    # already produced perfectly good values.
    # With -SeedK/-SeedUc the run opens by herding the ball back to the middle instead, which also removes
    # the need for a human to reposition it between runs.
    if ($SeedK -ne 0.0) {
        $Kest  = $SeedK
        $ucEst = if ($SeedUc -gt 0) { [double]$SeedUc } else { [double]$usMid }
        L ""
        L ("seeded: K={0:N3} {1}/s^2/us, centre={2:N0} us (given, bootstrap skipped)" -f $Kest, $unit, $ucEst)
        L  "  recovering the ball to the middle before the first capture - it may be against an end stop"
        $r = HerdTo 0.0 $Kest $ucEst $scale ($HerdMs * 2)
        L ("  recover -> {0}" -f $r)
        if ($r -eq "LOST") {
            L "  the ball could not be recovered under control. It is probably wedged past the guard, or"
            L "  the seeded sign is wrong (a wrong sign pushes it HARDER into the stop). Check -SeedK's sign."
            Finish "INCONCLUSIVE - could not recover the ball to the middle with the seeded K/centre." 2
        }
        if ($r -eq "SLOW") {
            L "  recovery did not settle inside the doubled herd window. Continuing would start every point"
            L "  from an unknown moving state and usually turns the rest of the sweep into guard hits."
            Finish "INCONCLUSIVE - seeded recovery timed out; stop now instead of spending a full sweep on invalid points." 2
        }
    } else {
        L ""; L ("== bootstrap: us = {0} (extreme) ==" -f $usHi)
        $b1 = CaptureAt $usHi $usMid
        if ($b1) { $pts.Add($b1) }
        L ""; L ("== bootstrap: us = {0} (extreme, sends the ball back) ==" -f $usLo)
        $b2 = CaptureAt $usLo $usMid
        if ($b2) { $pts.Add($b2) }
        if ($b1 -and $b2 -and [Math]::Abs($b1.us - $b2.us) -gt 1.0) {
            $Kest  = ($b1.a - $b2.a) / ($b1.us - $b2.us)
            if ([Math]::Abs($Kest) -gt 1e-9) { $ucEst = $b1.us - $b1.a / $Kest }
        }
    }
    if ($null -eq $Kest -or [Math]::Abs($Kest) -lt 1e-9) {
        L ""
        L "bootstrap failed: the two extreme pulse widths did not produce a usable slope."
        L "  Most likely the beam never reached an angle that moves the ball. Widen -UsList, or check"
        L "  friction / that the beam is actually being driven (`?` should show a non-zero [srv] us)."
        L "  Falling back is not automatic - re-run with -Manual if you want to place the ball by hand."
        Finish "INCONCLUSIVE - could not bootstrap K/centre for the shuttle." 2
    }
    L ""
    L ("bootstrap: K~{0:N3} {1}/s^2/us, centre~{2:N0} us, sign {3}  (rough - refined by the full fit later)" -f `
        $Kest, $unit, $ucEst, $(if ($Kest -gt 0) { "+" } else { "-" }))

    # Phase 1: alternate sides of centre. Alternating is what makes the shuttle work - each capture
    # pushes the ball back toward the side the NEXT capture needs it on, so herding only has to correct,
    # never to traverse the whole slot.
    # In seeded mode the two extremes were never captured (there was no bootstrap), so they must stay in
    # the list - dropping them would silently lose the two most informative points, which are exactly the
    # ones that measure maximum authority on each side.
    $rest = if ($SeedK -ne 0.0) { @($usArr) } else { @($usArr | Where-Object { $_ -ne $usLo -and $_ -ne $usHi }) }
    $above = @($rest | Where-Object { $_ -gt $ucEst } | Sort-Object -Descending)
    $below = @($rest | Where-Object { $_ -le $ucEst } | Sort-Object)
    $order = New-Object System.Collections.Generic.List[int]
    for ($i = 0; $i -lt [Math]::Max($above.Count, $below.Count); $i++) {
        if ($i -lt $above.Count) { $order.Add([int]$above[$i]) }
        if ($i -lt $below.Count) { $order.Add([int]$below[$i]) }
    }
    L ("remaining points, alternating sides: {0}" -f ($order -join ', '))

    foreach ($us in $order) {
        L ""; L ("== us = {0} ==" -f $us)
        # Which way will this pulse width push the ball? Park it upstream of that.
        $push  = if (($Kest * ($us - $ucEst)) -ge 0) { 1.0 } else { -1.0 }
        $start = -$push * $StartMm
        $h = HerdTo $start $Kest $ucEst $scale $HerdMs
        L ("  herd to {0:+0;-0} mm -> {1}" -f $start, $h)
        if ($h -eq "LOST") { L "  cannot position the ball (out of view or past the guard) - point skipped"; continue }
        if ($h -eq "SLOW") { L "  herd timed out; capturing from wherever it ended up (the fit tolerates x0/v0)" }
        $p = CaptureAt $us $ucEst
        if ($p) { $pts.Add($p) }
    }
    # Leave the ball parked near centre rather than against a stop.
    [void](HerdTo 0.0 $Kest $ucEst $scale 4000)
}
} finally {
    # Park the beam level no matter how we leave the block above. $usMid is always defined and is the
    # median of -UsList, so it is the safest available guess at "level" even if the sweep never got far
    # enough to estimate a real centre.
    try { Send ("U" + $usMid) } catch {}
}

Send "z"; Wait 0.5

if ($pts.Count -lt 3) { Finish "INCONCLUSIVE - only $($pts.Count) usable point(s); need >= 3 to fit a line." 2 }

L ""
L "---- points ----"
L ("  {0,7} {1,12} {2,9} {3,8}" -f "us", "a ($unit/s^2)", "rms", "travel")
foreach ($p in $pts) { L ("  {0,7:N0} {1,12:N1} {2,9:N2} {3,8:N1}" -f $p.us, $p.a, $p.rms, $p.travel) }

# Split the points into "the ball really accelerated" and "the ball sat there".
# WHY this matters: a stuck point carries no slope information, and feeding it to the regression
# drags the fitted K toward zero AND biases us_center. Stuck points are not waste though - they ARE
# the deadband+backlash measurement, so they get reported separately below.
#
# Both tests are noise-relative on purpose, so they work whether xs are in mm or raw pixels:
#   travel > 5*rms                 the ball moved well clear of the measurement scatter
#   |a|    > 8*rms/dur^2           the quadratic term is resolvable; for a parabola fit over a window
#                                  of length T the standard error on a scales as rms/T^2, and 8x is
#                                  roughly a 3-sigma bar. Short captures therefore need a bigger a,
#                                  which is exactly right - that is why dur is per point, not global.
$moving = @($pts | Where-Object { FitIsMoving $_ })
$stuck  = @($pts | Where-Object { -not (FitIsMoving $_) })
L ""
L ("noise-relative gate : {0} moving point(s), {1} stuck" -f $moving.Count, $stuck.Count)
foreach ($p in $pts) {
    L ("    us={0,6:N0}  |a|={1,8:N1}  floor={2,8:N1}  travel={3,7:N1}  5*rms={4,6:N1}  -> {5}" -f `
        $p.us, [Math]::Abs($p.a), (FitAccelFloor $p), [Math]::Abs($p.travel), (5.0*$p.rms), `
        $(if (FitIsMoving $p) { "MOVING" } else { "stuck" }))
}
if ($moving.Count -lt 3) { Finish "INCONCLUSIVE - fewer than 3 points where the ball actually accelerated. Widen -UsList (the beam may not be reaching a useful angle) or check the end stops / friction." 2 }

# Primary estimator: joint dead-zone fit (see _fit.ps1 for why, and _fit_test.ps1 for the proof that
# a single global line under-reports K by ~25-30% whenever a deadband exists).
$dz  = FitDeadzone @($moving | ForEach-Object { $_.us }) @($moving | ForEach-Object { $_.a })
$fit = FitLine     @($moving | ForEach-Object { $_.us }) @($moving | ForEach-Object { $_.a })
if (-not $dz -and -not $fit) { Finish "INCONCLUSIVE - both fits failed." 2 }

if ($dz) {
    $K = $dz.K; $uc = $dz.usCenter; $method = "dead-zone joint fit"
} else {
    $K = $fit.m; $uc = -$fit.b / $fit.m; $method = "GLOBAL LINE (fallback)"
}

L ""
L "---- identified plant ----"
L ("  model      : a = K_total * (us - us_center),  with a deadband of +-dead us around centre")
L ("  method     : {0}" -f $method)
L ("  K_total    = {0:N3} {1}/s^2 per us" -f $K, $unit)
L ("  us_center  = {0:N1} us                  <- the REAL 'beam level', vs {1} in config.h (= middle of the measured 940..1340 travel)" -f $uc, $CFG_CENTER_US)
L ("  sign       = {0}  (us above centre pushes the ball toward {1}x)" -f $(if ($K -gt 0) { "+1" } else { "-1" }), $(if ($K -gt 0) { "+" } else { "-" }))

if (-not $dz) {
    L ""
    L "  WARNING: only one side of centre produced motion, so K and the deadband cannot be separated."
    L "  The K above is therefore a LOWER BOUND (a deadband always makes a global line look shallow)."
    L ("  Re-run with points on both sides, e.g. -UsList `"{0},{1},{2},{3},{4},{5}`"" -f `
        [int]($uc-150), [int]($uc-100), [int]($uc-60), [int]($uc+60), [int]($uc+100), [int]($uc+150))
} else {
    L ""
    L "---- deadband + backlash (the two are inseparable to the ball, and it is the ball that matters) ----"
    L ("  dead       = {0:N1} us   (branch zeros at {1:N0} / {2:N0}; raw {3:N1})" -f `
        $dz.deadUs, ($uc + $dz.deadRaw), ($uc - $dz.deadRaw), $dz.deadRaw)
    L ("  fit        : n={0} used, {1} weak point(s) dropped, residual rms {2:N2} {3}/s^2" -f $dz.n, $dz.weak, $dz.rms, $unit)
    L ("  branch K   : + {0:N3}   - {1:N3}   asymmetry {2:N1}%  (diagnostic; noisy with few points)" -f `
        $dz.kPos, $dz.kNeg, $dz.asymPct)
    if ($dz.deadRaw -le 0) {
        L "  raw deadband came out <= 0, which is unphysical => it is below what this sweep can resolve."
        L "  That is good news, but if you need the number, re-run with a finer list near centre."
    }
    if ($CxPerMm -gt 0) {
        $dbA = [Math]::Abs($K) * $dz.deadUs / 1000.0        # mm/s^2 -> m/s^2
        L ("  -> ball acceleration deadband {0:N4} m/s^2" -f $dbA)
        L ("     Limit-cycle threshold from the analysis is 0.025 m/s^2 : {0}" -f `
            $(if ($dbA -lt 0.025) { "OK, {0:N0}% of budget" -f (100*$dbA/0.025) } else { "OVER - expect a small persistent oscillation that no PID setting can remove" }))
        if ($dbA -ge 0.025) {
            L "     Fixes, cheapest first: tighten the linkage joints / shorten the horn (raises the"
            L "     reduction ratio, which divides the deadband further) / add the error-direction dead"
            L "     zone feed-forward this repo already has real-machine experience with. Do NOT tune it away."
        }
    }
    if ([Math]::Abs($dz.asymPct) -gt 25.0) {
        L ""
        L "  NOTE asymmetry > 25%: either the linkage is genuinely lopsided about centre (crank-slider"
        L "  geometry does this at large angles), the ball sees different friction each way, or there are"
        L "  simply too few points per branch. Add points before believing it is mechanical."
    }
}

# Nonlinearity: the crank-slider makes K fall off at large angles. Per-point K vs the global fit
# shows it without ever knowing r / L_arm / L_rod.
L ""
L "---- transmission linearity (crank-slider check) ----"
L ("  {0,7} {1,12} {2,10}" -f "us", "local K", "vs global")
foreach ($p in $moving) {
    $d = $p.us - $uc
    if ([Math]::Abs($d) -lt 5) { continue }
    $kl = $p.a / $d
    L ("  {0,7:N0} {1,12:N3} {2,9:N1}%" -f $p.us, $kl, (100.0 * ($kl/$K - 1.0)))
}
L "  A monotone fall-off at the extremes is the expected crank-slider geometry, not a fault."
L "  Under ~5% across the working range means the linear us-per-degree model is fine."

if ($CxPerMm -gt 0) {
    # theta_beam = a / ((5/7)*g) ; K_BALL = (5/7)*9810 mm/s^2 per rad = 7007
    $usPerDeg = 7007.0 / [Math]::Abs($K) * [Math]::PI / 180.0
    # Authority available inside the MEASURED mechanism travel, not inside some nominal servo range.
    #
    # Report BOTH sides separately, because they are not equal and the difference is the whole story:
    # the identified level pulse (us_center) is generally NOT the middle of the mechanism's travel, so one
    # direction runs out of beam before the other. A symmetric task (hold at 0, then +-50 mm) can only use
    # the SMALLER of the two, so that is what gets compared against the requirement.
    $upUs     = [double]($CFG_MAX_US - $uc)      # how far the beam can still tilt one way
    $dnUs     = [double]($uc - $CFG_MIN_US)      # ... and the other
    $halfUs   = [Math]::Min($upUs, $dnUs)
    $maxDeg   = $halfUs / $usPerDeg
    $upDeg    = $upUs / $usPerDeg
    $dnDeg    = $dnUs / $usPerDeg
    L ""
    L "---- derived, for config.h ----"
    L ("  CFG_SERVO_US_PER_DEG  = {0:N1}   (per BEAM degree; config.h ships 80.0f, a paper estimate)" -f $usPerDeg)
    L ("  CFG_SERVO_CENTER_US   = {0:N0}" -f $uc)
    L ("  CFG_BALL_SERVO_SIGN   = {0}" -f $(if ($K -gt 0) { "1" } else { "-1" }))
    L ("  beam authority inside the measured {0}..{1} us clamp:" -f $CFG_MIN_US, $CFG_MAX_US)
    L ("    one way {0:N0} us = {1:N2} deg   |   other way {2:N0} us = {3:N2} deg" -f $upUs, $upDeg, $dnUs, $dnDeg)
    L ("    usable SYMMETRIC authority = +-{0:N2} deg  (the smaller side; a symmetric task cannot use more)" -f $maxDeg)
    $asym = if ($maxDeg -gt 0.01) { [Math]::Abs($upDeg - $dnDeg) / $maxDeg } else { 9.99 }
    if ($asym -gt 0.25) {
        L ("    ASYMMETRIC by {0:N0}%: the level pulse {1:N0} us is not the middle of the travel ({2:N0} us)." -f `
            (100.0*$asym), $uc, (($CFG_MIN_US + $CFG_MAX_US)/2.0))
        L  "    That is a MECHANICAL mounting offset, not something gains can fix. Moving the servo horn one"
        L  "    tooth (or trimming the pushrod) so that level lands mid-travel converts the wasted side into"
        L ("    usable authority - worth {0:N2} deg instead of {1:N2} deg symmetric." -f `
            ((($CFG_MAX_US - $CFG_MIN_US)/2.0)/$usPerDeg), $maxDeg)
    }
    # What the TASK actually needs, computed from the trajectory timing - not a hardcoded angle.
    #
    # This check used to compare against a hardcoded 3.68 deg, and that number misled a whole round of
    # analysis on 2026-07-31: 3.68 deg is from ball.c:74, "theta = kp*e/K = 9*50/7007 at e=50 mm", i.e.
    # the PD term's TRANSIENT output when the ball is 50 mm off target. It is not what the task demands.
    # Comparing available authority against it produced a false "2.2x shortfall, physical wall" alarm.
    # The real feedforward demand is the trajectory's own acceleration, which is a triangular profile:
    #   |a| = 4 * distance / time^2 , taken over both legs (out, and the longer return across 2*amp).
    # A 50 mm error only happens at start-up or after a disturbance; saturating there merely makes large-
    # error recovery less aggressive than kp would like. It does not affect the scored trajectory, where
    # the tracking error is millimetres and the PD term is correspondingly small.
    $aOut     = 4.0 * $TrajAmpMm / ($TrajTOut * $TrajTOut)
    $aBack    = 4.0 * (2.0 * $TrajAmpMm) / ($TrajTBack * $TrajTBack)
    $aNeed    = [Math]::Max($aOut, $aBack)
    $degNeed  = $aNeed / 7007.0 * 180.0 / [Math]::PI
    L ""
    L ("  what the TASK needs (feedforward for the +-{0:N0} mm trajectory, from its own timing):" -f $TrajAmpMm)
    L ("    out leg  {0:N1}s over {1:N0} mm -> {2:N0} mm/s^2 ;  return leg {3:N1}s over {4:N0} mm -> {5:N0} mm/s^2" -f `
        $TrajTOut, $TrajAmpMm, $aOut, $TrajTBack, (2.0*$TrajAmpMm), $aBack)
    L ("    worst case {0:N0} mm/s^2 = {1:N2} deg of beam angle" -f $aNeed, $degNeed)
    if ($maxDeg -lt $degNeed) {
        L ("  WARNING: the trajectory needs {0:N2} deg and only {1:N2} deg is available ({2:N1}x short)." -f `
            $degNeed, $maxDeg, ($degNeed/[Math]::Max($maxDeg,0.01)))
        L  "           Cheapest first: (a) re-centre the horn so level sits mid-travel, (b) lengthen the horn"
        L  "           to raise deg-per-us, (c) slow the trajectory down - |a| falls as 1/t^2, so adding 20%"
        L  "           to the leg times cuts the angle demand by 30%."
        L  "           Do NOT widen CFG_SERVO_MIN_US/MAX_US - those are the measured mechanical stops."
    } else {
        L ("  OK: the trajectory needs {0:N2} deg, so there is {1:N0}% headroom on the weaker side." -f `
            $degNeed, (100.0*($maxDeg/$degNeed - 1.0)))
    }
    # Reported separately, because it is a transient limit and not a pass/fail on the task.
    $degPd = $BallKp * 50.0 / 7007.0 * 180.0 / [Math]::PI
    if ($degPd -gt $maxDeg) {
        L ("  note: with kp={0:N1}, a 50 mm error alone asks for {1:N2} deg, so the PD term saturates for" -f $BallKp, $degPd)
        L ("        large errors. Effective large-error gain is capped at {0:N1} 1/s^2 (omega_n {1:N1} rad/s instead of {2:N1})." -f `
            ($maxDeg*[Math]::PI/180.0*7007.0/50.0), [Math]::Sqrt($maxDeg*[Math]::PI/180.0*7007.0/50.0), [Math]::Sqrt($BallKp))
        L  "        That is a start-up / disturbance-recovery limit, NOT a limit on the scored trajectory."
    }
    L ""
    L "  These are RAM-only until you paste them into config.h and reflash (SERVO_CENTER_US can also be"
    L "  set live with C0, but it dies at power-off - the repo rule is 'meet spec -> write it down')."
}

if ($Csv) {
    try {
        $d = Split-Path $Csv -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        $pts | Select-Object us, a, rms, n, travel | Export-Csv $Csv -NoTypeInformation -Encoding ASCII
        L ""; L "points written to $Csv"
    } catch { L "(could not write $Csv)" }
}

$result.metrics = [ordered]@{
    k_total = [Math]::Round($K, 6)
    us_center = [Math]::Round($uc, 3)
    servo_sign = $(if ($K -gt 0) { 1 } else { -1 })
    usable_points = $moving.Count
    stuck_points = $stuck.Count
    fit_method = $method
    deadband_us = $(if ($dz) { [Math]::Round($dz.deadUs, 3) } else { $null })
}

L ""
L "NOT proven by this script:"
L "  * that (5/7) is right - it identifies the COMPOSITE gain and cannot separate (5/7) from d(theta)/d(us)."
L "    That is fine for control (the loop only needs K_total); cite CTMS/literature in the report instead."
L "  * the closed loop. Gains CFG_BALL_KP/KD assume K_BALL=7007 mm/s^2 per rad; if the identified"
L "    us-per-degree differs a lot from what config.h had, re-check the loop bandwidth before m12."
L "  * backlash as a mechanical quantity - the plateau above is deadband AND backlash together, which"
L "    is what actually matters to the ball."
Finish ("PASS - K_total={0:N3} {1}/s^2/us, us_center={2:N1}, sign={3}, {4} points." -f `
    $K, $unit, $uc, $(if ($K -gt 0) { "+1" } else { "-1" }), $moving.Count) 0
