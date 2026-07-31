# ball_signs.ps1 - determine CFG_BALL_AX_SIGN and CFG_BALL_PITCH_SIGN from the machine, empirically.
#
# WHY THESE TWO GET THEIR OWN SCRIPT
#   config.h 7.12 says it outright: the convention does not count, only the S1/S2 measurement does.
#   Getting a sign wrong does not make
#   the loop perform badly - it makes it DIVERGE, because the feed-forward then pushes the ball the way
#   the disturbance already was. That failure looks exactly like "no PID setting works", which is the most
#   expensive way to lose an evening. So both signs are settled BEFORE any gain is touched.
#
#   And they are settled by WATCHING THE BALL, never by reasoning about geometry. Which end of the beam
#   is "+x", which way the camera counts, and which way the car calls "forward" are three independent
#   mounting facts; reasoning through all three at a bench at 3am is how you get a confident wrong answer.
#   The ball is the ground truth for all of them at once.
#
# THE TRICK THAT MAKES THE a_x TEST TRUSTWORTHY (this is the design, not an optimisation)
#   A single "accelerate and see which way the ball rolls" run cannot tell the a_x effect apart from a
#   beam that simply is not perfectly level - both give a steady ball acceleration. So we run the car
#   FORWARD and BACKWARD alternately:
#       a(+v) = a_bias + a_ax
#       a(-v) = a_bias - a_ax
#   =>  a_ax  = (a(+v) - a(-v)) / 2      <- the part that reverses with travel direction
#       a_bias = (a(+v) + a(-v)) / 2     <- the part that does not: residual beam tilt
#   The confound is not merely tolerated, it is measured and removed. Two bonuses fall out:
#     * the car ends up roughly back where it started (no need for a long runway)
#     * a_bias is an INDEPENDENT cross-check of the us_center that ball_ident produced
#
# REQUIREMENTS
#   * firmware with the BALL: telemetry field and the `g` pitch/roll print (build >= 2026-07-31)
#   * -UsCenter from `ball_ident.ps1 -Step Sweep`. Without it the beam is not level and gravity swamps
#     everything - the script refuses rather than producing a confident wrong sign.
#   * the camera seeing the ball; end stops fitted
#
# SAFETY
#   Step Ax DRIVES THE CAR (short alternating bursts, it stays within ~150 mm of where it started).
#   Wheels on the ground, ~0.5 m clear each way, nothing trailing that can be dragged.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_signs.ps1 -Port COM4 -UsCenter 1140
#
# The example used to read 1487, which is now ABOVE the mechanism's top stop (measured 2026-07-31:
# the assembled beam travels 940..1340 us, duty 4.7%..6.7% at 50 Hz). Copy-pasting the old figure would
# have driven the beam into its limit. 1140 is the middle of the measured travel; the firmware clamps to
# 960..1320 anyway, so an out-of-range request is refused rather than executed.
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_signs.ps1 -Port COM4 -Step Pitch
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose.
param(
    [string]$Port      = "COM4",
    [int]$Baud         = 115200,
    [ValidateSet("Ax","Pitch","All")]
    [string]$Step      = "All",

    [int]$UsCenter     = 0,        # from ball_ident -Step Sweep. 0 = refuse the Ax step.
    [double]$CxPerMm   = 100.0,    # fixed unit conversion, see config.h 7.12 (cx carries x_mm*100)

    # --- Ax step ---
    [int]$Rpm          = 100,      # burst speed for m7; 100 RPM is ~310 mm/s on this car
    [int]$CaptureMs    = 800,      # per burst; long enough for a clear parabola, short enough to stay put
    [int]$Pairs        = 2,        # forward/backward PAIRS. 2 pairs = 4 bursts = 2 independent estimates
    [double]$GuardMm   = 100.0,    # abort a capture if |x| exceeds this (end stops sit near 125)
    [double]$SettleMm  = 35.0,     # ball must start within this of centre
    [double]$SettleStd = 3.0,      # ...and be this still (mm) before a burst counts

    # --- Pitch step ---
    [int]$StaticN      = 12,       # samples for the A7 static-noise verdict
    [double]$TiltMinDeg = 0.4,     # a tilt smaller than this is not a usable stimulus
    [double]$TiltMinMm  = 5.0,     # ...and the ball must move at least this far to call a direction

    [switch]$Yes,
    [string]$Out       = "_logs\ball_signs_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = $null
function Finish([string]$v, [int]$c) {
    L ""; L "RESULT: $v"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding ASCII
    } catch { Write-Host "(could not write $Out)" }
    if ($sp) {
        # Always leave the board safe and as found: stop, park the beam level, restore dual-send.
        try {
            $cmds = @("z")
            if ($UsCenter -gt 0) { $cmds += ("U" + $UsCenter) }
            $cmds += @("l3","f100")
            foreach ($cmd in $cmds) {
                foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
                Start-Sleep -Milliseconds 120
            }
            $sp.Close(); $sp.Dispose()
        } catch {}
    }
    exit $c
}

# Dependencies FIRST, hardware second. If _fit.ps1 or _serial_ball.ps1 is missing or broken, we want to
# find out now - not after the board is wired up and someone is standing there waiting.
. (Join-Path $PSScriptRoot "_fit.ps1")
. (Join-Path $PSScriptRoot "_serial_ball.ps1")

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

$scale   = 1.0 / $CxPerMm          # cx -> mm
$guardCx = $GuardMm * $CxPerMm
$stopCx  = 0                       # never stop a burst early: the full parabola is the measurement

L "================ ball_signs  step=$Step  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ================"
L "port $Port"

$sink = SetupTelemetry
if (-not $sink) { Finish "INCONCLUSIVE - no [ctl] telemetry on $Port. Wrong port, or nothing running." 2 }
L ("telemetry: f25 (40 Hz), sink locked to {0}" -f $sink)

$pre = Collect 1500 0 0
if ($pre.Count -eq 0) {
    L ""
    L ("telemetry flowing ({0} lines) but no usable ball sample; {1} frame(s) said id=-1." -f $script:lines, $script:notSeen)
    L "Fix detection first - every verdict below is computed from frames where the ball WAS seen:"
    L "  no BALL: field at all -> firmware predates it, or the vision link never delivered a frame"
    L "  id=-1 only            -> link fine, camera is not finding the ball (ROI / exposure / background)"
    Finish "INCONCLUSIVE - cannot read ball position." 2
}
$ages = @($pre | ForEach-Object { $_.age })
L ("camera: {0} frames with a ball in 1.5 s (~{1:N1} fps), age median {2} ms; {3} frame(s) id=-1" -f `
    $pre.Count, ($pre.Count/1.5), (Median $ages), $script:notSeen)

# ---- shared helper: wait for the ball to be still near centre -------------------------
function WaitSettled([int]$timeoutMs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalMilliseconds -lt $timeoutMs) {
        $s = Collect 900 0 0
        if ($s.Count -ge 8) {
            $xs  = @($s | ForEach-Object { [double]$_.cx * $scale })
            $std = Stdev $xs
            $mid = Median $xs
            if ([Math]::Abs($mid) -le $SettleMm -and $std -le $SettleStd) {
                return [pscustomobject]@{ ok=$true; x=$mid; std=$std }
            }
        }
    }
    $s = Collect 900 0 0
    $xs = if ($s.Count -gt 0) { @($s | ForEach-Object { [double]$_.cx * $scale }) } else { @(0.0) }
    return [pscustomobject]@{ ok=$false; x=(Median $xs); std=(Stdev $xs) }
}

$verdicts = New-Object System.Collections.Generic.List[string]
$fails = 0

try {

# =====================================================================================
if ($Step -eq "Ax" -or $Step -eq "All") {
    L ""
    L "---- Step Ax : CFG_BALL_AX_SIGN ----"
    if ($UsCenter -le 0) {
        L "REFUSED: -UsCenter not given."
        L "  Without a level beam, gravity produces a far larger ball acceleration than the car ever will,"
        L "  and the test would return a confident WRONG sign. Run `ball_ident.ps1 -Step Sweep` first and"
        L "  pass its us_center here. (Refusing beats guessing - see the INCONCLUSIVE discipline in the repo.)"
        $fails++
    } else {
        if (-not $Yes) {
            Write-Host ""
            Write-Host "THE CAR WILL DRIVE - short alternating bursts, staying within ~150 mm of the start." -ForegroundColor Yellow
            Write-Host "Wheels on the ground, ~0.5 m clear each way, no trailing cables." -ForegroundColor Yellow
            Write-Host "Type YES to continue:" -ForegroundColor Yellow
            if ((Read-Host) -ne "YES") { Finish "aborted by operator" 2 }
        }
        Send ("U" + $UsCenter)     # lock the beam level; m7 never touches the servo so it stays put
        Start-Sleep -Milliseconds 500

        $runs = New-Object System.Collections.Generic.List[object]
        for ($p = 1; $p -le $Pairs; $p++) {
            foreach ($dir in @(1, -1)) {
                $vv = $dir * $Rpm
                L ""
                L ("== burst {0} : v{1} ==" -f $p, $vv)
                $st = WaitSettled 8000
                if (-not $st.ok) {
                    L ("  ball not settled near centre (x={0:N1} mm, std={1:N2}) - place it at 0 and it will retry next burst" -f $st.x, $st.std)
                    continue
                }
                L ("  start: x={0:N1} mm, std={1:N2} mm" -f $st.x, $st.std)
                Send "m7"; Start-Sleep -Milliseconds 200
                Send ("v" + $vv)
                $s = Collect $CaptureMs $stopCx $guardCx
                Send "z"                       # brake; stop_all does NOT touch the servo, beam stays level
                if ($script:lastGuardHit) { L ("  guard hit (|x| >= {0:N0} mm) - burst cut short" -f $GuardMm) }
                Wait 1.0
                if ($s.Count -lt 6) { L ("  only {0} usable frames - burst discarded" -f $s.Count); continue }
                $t0 = $s[0].t
                $f = FitQuad @($s | ForEach-Object { $_.t - $t0 }) @($s | ForEach-Object { [double]$_.cx * $scale })
                if (-not $f) { L "  fit failed"; continue }
                L ("  n={0} dur={1:N2}s travel={2:N1}mm  a={3:N1} mm/s^2  rms={4:N2}mm" -f $f.n, $f.dur, $f.span, $f.a, $f.rms)
                $runs.Add([pscustomobject]@{ dir=$dir; a=$f.a; rms=$f.rms; dur=$f.dur; travel=$f.span })
            }
        }

        $fwd = @($runs | Where-Object { $_.dir -gt 0 })
        $bwd = @($runs | Where-Object { $_.dir -lt 0 })
        if ($fwd.Count -lt 1 -or $bwd.Count -lt 1) {
            L ""
            L ("INCONCLUSIVE: need at least one forward AND one backward burst (got {0}/{1})." -f $fwd.Count, $bwd.Count)
            L "  Both directions are what separates the a_x effect from a beam that is not quite level."
            $fails++
        } else {
            $aF = 0.0; foreach ($r in $fwd) { $aF += $r.a }; $aF /= $fwd.Count
            $aB = 0.0; foreach ($r in $bwd) { $aB += $r.a }; $aB /= $bwd.Count
            $aAx   = ($aF - $aB) / 2.0
            $aBias = ($aF + $aB) / 2.0
            L ""
            L "---- decomposition ----"
            L ("  a(+v) mean = {0,8:N1} mm/s^2   (n={1})" -f $aF, $fwd.Count)
            L ("  a(-v) mean = {0,8:N1} mm/s^2   (n={1})" -f $aB, $bwd.Count)
            L ("  a_ax       = {0,8:N1} mm/s^2   <- reverses with travel direction: the real a_x effect" -f $aAx)
            L ("  a_bias     = {0,8:N1} mm/s^2   <- does NOT reverse: residual beam tilt" -f $aBias)
            $tiltDeg = [Math]::Asin([Math]::Max(-1.0,[Math]::Min(1.0, $aBias / 7007.0))) * 180.0 / [Math]::PI
            L ("  residual tilt = {0:N3} deg  (a_bias / ((5/7)g)) - independent cross-check of us_center" -f $tiltDeg)

            # Consistency: every forward burst should agree in sign, likewise every backward one. If they
            # do not, something changed between bursts (ball hit a stop, camera lost it, beam moved).
            $sgnF = @($fwd | ForEach-Object { [Math]::Sign($_.a - $aBias) } | Sort-Object -Unique)
            $sgnB = @($bwd | ForEach-Object { [Math]::Sign($_.a - $aBias) } | Sort-Object -Unique)
            $consistent = ($sgnF.Count -le 1 -and $sgnB.Count -le 1)

            if ([Math]::Abs($aAx) -lt 15.0) {
                L ""
                L ("INCONCLUSIVE: |a_ax| = {0:N1} mm/s^2 is too small to call a sign." -f [Math]::Abs($aAx))
                L "  Either the car barely accelerated (raise -Rpm, or check the wheels are on the ground),"
                L "  or the beam is so far off level that a_bias swamps everything (re-check us_center)."
                $fails++
            } elseif (-not $consistent) {
                L ""
                L "INCONCLUSIVE: bursts in the same direction disagreed in sign."
                L "  Something changed between bursts - ball reached an end stop, camera lost it, or the beam"
                L "  shifted. Re-run; if it repeats, watch the ball during a burst before trusting anything."
                $fails++
            } else {
                # Physics: the car accelerates toward its nose; in the car frame the ball is pushed toward
                # the REAR. ball.h's convention is that positive a_x must be the car's forward acceleration
                # expressed in +x, and x'' = -(5/7)*a_x, i.e. positive a_x drives the ball toward -x.
                #   ball accelerated toward -x  => +x points at the nose  => convention holds  => +1
                #   ball accelerated toward +x  => +x points at the rear  => flip it           => -1
                # NOTE the car also pitches nose-up while accelerating, which rolls the ball the SAME way,
                # so the SIGN is robust - but for that same reason the MAGNITUDE above must NOT be used to
                # estimate a_x. It is a sign test only.
                $axSign = if ($aAx -lt 0) { 1 } else { -1 }
                L ""
                L ("  forward travel drove the ball toward {0}x" -f $(if ($aAx -lt 0) { "-" } else { "+" }))
                L ("  => +x points toward the car's {0}" -f $(if ($aAx -lt 0) { "NOSE" } else { "REAR" }))
                L ""
                L ("  config.h  ->  #define CFG_BALL_AX_SIGN   {0}" -f $axSign)
                $verdicts.Add(("CFG_BALL_AX_SIGN = {0}" -f $axSign))
                if ([Math]::Abs($tiltDeg) -gt 0.5) {
                    L ""
                    L ("  WARNING residual tilt {0:N2} deg is large. It costs a steady ball offset of about" -f $tiltDeg)
                    L ("  {0:N1} mm at CFG_BALL_KP=9 - worth re-doing the Sweep centre before tuning gains." -f ([Math]::Abs($aBias)/9.0))
                }
            }
        }
    }
}

# =====================================================================================
if ($Step -eq "Pitch" -or $Step -eq "All") {
    L ""
    L "---- Step Pitch : A7 noise verdict + CFG_BALL_PITCH_SIGN ----"

    # --- A7: is pitch quiet enough to feed forward at all? -------------------------------
    # This is the war-map assumption A7, still unverified, and it gates requirements 5+6 (40 points).
    # Until 2026-07-31 it could not even be measured: pitch was never printed anywhere.
    $ps = New-Object System.Collections.Generic.List[double]
    for ($i = 0; $i -lt $StaticN; $i++) {
        $r = ReadPitch 2.0
        if ($r) { $ps.Add($r.pitch) }
        Start-Sleep -Milliseconds 120
    }
    if ($ps.Count -lt 5) {
        L ("INCONCLUSIVE: only {0} pitch reading(s) came back. Does the firmware print 'pitch0.1deg='? (build >= 2026-07-31)" -f $ps.Count)
        $fails++
    } else {
        $pStd = Stdev $ps
        $pMed = Median $ps
        $pMin = ($ps | Measure-Object -Minimum).Minimum
        $pMax = ($ps | Measure-Object -Maximum).Maximum
        L ("  static pitch: median {0:N2} deg, std {1:N3} deg, range {2:N2}..{3:N2}, n={4}" -f $pMed, $pStd, $pMin, $pMax, $ps.Count)
        if ($pStd -lt 0.1) {
            L "  A7 VERDICT: PASS - noise < 0.1 deg, pitch is clean enough to feed forward."
            $verdicts.Add("A7 (pitch usable) = PASS, std {0:N3} deg" -f $pStd)
        } else {
            L ("  A7 VERDICT: FAIL - std {0:N3} deg exceeds the 0.1 deg budget." -f $pStd)
            L "  Feeding this forward would inject noise straight into the beam angle. Options: low-pass the"
            L "  pitch estimate, or accept that requirement 5/6 loses the pitch term (the war map priced this"
            L "  as the +-1cm margin dropping from 2.4x to 1.3x - tight but not fatal)."
            $fails++
        }
        L ("  NOTE the car must be STILL for this number to mean anything. Vibration from a running motor is a")
        L ("  different measurement - worth repeating during a line-following run before trusting requirement 5.")

        # --- sign, determined by watching the ball, not by reasoning about geometry --------
        if ($UsCenter -le 0) {
            L ""
            L "  sign step SKIPPED: needs -UsCenter so the beam starts level (otherwise the ball is already"
            L "  rolling and there is no clean 'before' to compare against)."
        } else {
            Send ("U" + $UsCenter); Start-Sleep -Milliseconds 400
            $st = WaitSettled 8000
            if (-not $st.ok) {
                L ("  ball will not settle near centre (x={0:N1}, std={1:N2}) - cannot run the sign test" -f $st.x, $st.std)
                $fails++
            } else {
                $base = ReadPitch 2.0
                L ("  baseline: pitch {0:N2} deg, ball x {1:N1} mm" -f $base.pitch, $st.x)
                if (-not $Yes) {
                    Write-Host ""
                    Write-Host "  RAISE THE CAR'S NOSE slightly (about 5 mm under the front wheels) and HOLD it." -ForegroundColor Cyan
                    Write-Host "  Keep it small - a big tilt just parks the ball against an end stop." -ForegroundColor Cyan
                    Write-Host "  Press Enter once it is lifted and held:" -ForegroundColor Cyan
                    [void](Read-Host)
                }
                $tilt = ReadPitch 2.0
                $s2 = Collect 1200 0 $guardCx
                if (-not $tilt -or $s2.Count -lt 5) {
                    L "  INCONCLUSIVE: lost pitch or ball readings while tilted."
                    $fails++
                } else {
                    $xs2 = @($s2 | ForEach-Object { [double]$_.cx * $scale })
                    $dx  = (Median $xs2) - $st.x
                    $dp  = $tilt.pitch - $base.pitch
                    L ("  tilted:   pitch {0:N2} deg (delta {1:+0.00;-0.00}), ball x {2:N1} mm (delta {3:+0.0;-0.0})" -f `
                        $tilt.pitch, $dp, (Median $xs2), $dx)
                    if ([Math]::Abs($dp) -lt $TiltMinDeg) {
                        L ("  INCONCLUSIVE: pitch only changed {0:N2} deg (need >= {1:N2}). Lift a little more." -f [Math]::Abs($dp), $TiltMinDeg)
                        $fails++
                    } elseif ([Math]::Abs($dx) -lt $TiltMinMm) {
                        L ("  INCONCLUSIVE: ball only moved {0:N1} mm (need >= {1:N1}). Either it is stuck, or the beam" -f [Math]::Abs($dx), $TiltMinMm)
                        L "  is not actually following the car body - check the beam mount is rigid."
                        $fails++
                    } else {
                        # Derivation (kept here because it is easy to get backwards):
                        #   car.c:  bin.pitch_deg  = SIGN * g_att.pitch
                        #   ball.c: th_pitch_deg   = -bin.pitch_deg
                        #   ball.h: theta > 0 accelerates the ball toward +x
                        # The feed-forward must push the ball OPPOSITE to the way the tilt rolled it:
                        #   rolled toward +x -> need theta<0 -> -SIGN*dp < 0 -> SIGN*dp > 0 -> SIGN =  sign(dp)
                        #   rolled toward -x -> need theta>0 -> -SIGN*dp > 0 -> SIGN*dp < 0 -> SIGN = -sign(dp)
                        $sgnDp = if ($dp -gt 0) { 1 } else { -1 }
                        $pitchSign = if ($dx -gt 0) { $sgnDp } else { -$sgnDp }
                        L ""
                        L ("  tilt rolled the ball toward {0}x, while pitch moved {1}" -f `
                            $(if ($dx -gt 0) { "+" } else { "-" }), $(if ($dp -gt 0) { "positive" } else { "negative" }))
                        L ("  config.h  ->  #define CFG_BALL_PITCH_SIGN   {0}" -f $pitchSign)
                        $verdicts.Add(("CFG_BALL_PITCH_SIGN = {0}" -f $pitchSign))
                        L ""
                        L "  Lower the car back down before running anything else."
                    }
                }
            }
        }
    }
}

} finally {
    # Whatever happened above, do not leave the car driving or the beam tilted.
    try { Send "z" } catch {}
    try { if ($UsCenter -gt 0) { Send ("U" + $UsCenter) } } catch {}
}

# =====================================================================================
L ""
L "---- summary ----"
if ($verdicts.Count -eq 0) { L "  (nothing determined)" }
foreach ($v in $verdicts) { L ("  " + $v) }
L ""
L "Reminder: these are RAM-nothing values - they live only in this log until you paste them into"
L "config.h and reflash. The repo rule is 'meet spec -> write it down immediately' (school-contest B"
L "lost its worst evening to a fix that existed but was never enabled on the day)."
L ""
L "NOT proven here:"
L "  * the magnitude of the a_x effect - body pitch during acceleration rolls the ball the same way, so"
L "    only the SIGN is separable by this test"
L "  * that pitch stays quiet while the motors run - the A7 number above is a stationary measurement"
L "  * CFG_BALL_SERVO_SIGN - that one comes out of ball_ident.ps1 -Step Sweep, not here"

if ($fails -gt 0) { Finish ("FAIL - {0} check(s) unresolved (see above)." -f $fails) 1 }
Finish ("PASS - {0} value(s) determined." -f $verdicts.Count) 0
