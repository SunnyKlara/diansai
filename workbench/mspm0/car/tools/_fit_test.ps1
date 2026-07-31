# _fit_test.ps1 - synthetic-data self test for _fit.ps1 . No hardware, no serial port.
#
# WHAT IT PROVES
#   That the identification maths recovers a KNOWN plant. We build fake camera data from
#       a = K_total * (us - us_center)      x(t) = x0 + v0*t + 0.5*a*t^2 + noise
#   sample it at a camera-like rate, push it through FitQuad + FitLine exactly the way
#   ball_ident.ps1 does, and assert the recovered K_total / us_center / sign match the truth.
#
# WHY IT MATTERS
#   A bug here produces a plausible-looking but wrong K_total -> wrong CFG_BALL_KP/KD -> a loop that
#   will not settle. The symptom is indistinguishable from a sloppy linkage, so we would go and
#   re-machine the mechanism while the fault sat in a regression. Ten minutes here buys that away.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File _fit_test.ps1
# EXIT CODES: 0 = all pass, 1 = a test failed.
# ASCII only on purpose.

. (Join-Path $PSScriptRoot "_fit.ps1")

$script:fails = 0
$script:total = 0
function Check([string]$name, [bool]$ok, [string]$detail) {
    $script:total++
    if ($ok) { Write-Host ("  [PASS] {0,-46} {1}" -f $name, $detail) }
    else     { Write-Host ("  [FAIL] {0,-46} {1}" -f $name, $detail) -ForegroundColor Red; $script:fails++ }
}
function Near([double]$a, [double]$b, [double]$tol) { return ([Math]::Abs($a-$b) -le $tol) }

# Deterministic pseudo-random so a failure is reproducible. A fixed LCG beats Get-Random here:
# a test that fails only on some runs is worse than no test.
#
# IMPORTANT: [int64] EVERYWHERE, on purpose. seed*1103515245 overflows Int32, and PowerShell promotes an Int32
# overflow to DOUBLE, not Int64 - Double has a 53-bit mantissa while the product needs ~61 bits, so
# the low bits are lost and the LCG degenerates into a correlated sequence. Correlated "noise" acts
# like a systematic trend in x(t), which biased the recovered K_total by ~7% and made two otherwise
# correct tests fail. The product stays exact in Int64 (2.4e18 < 9.2e18).
[int64]$script:seed = 12345
function Rnd() {
    $script:seed = ([int64]$script:seed * [int64]1103515245 + [int64]12345) % [int64]2147483648
    if ($script:seed -lt 0) { $script:seed += [int64]2147483648 }
    return ([double]$script:seed / 2147483648.0)
}
function Noise([double]$amp) { return ($amp * (2.0*(Rnd) - 1.0)) }

# Generate one capture: ball released from x0 with v0, beam held at $us.
# fps/dur mimic a 30 fps camera over a ~0.7 s window, i.e. about 21 samples - deliberately as few
# as the real thing, so the test exercises the sparse case rather than an easy dense one.
function MakeCapture([double]$K, [double]$uc, [double]$us, [double]$x0, [double]$v0,
                     [double]$dur, [double]$fps, [double]$noiseAmp, [double]$deadUs) {
    $d = $us - $uc
    # deadband: inside +-deadUs the beam angle change never breaks the ball loose
    $a = if ([Math]::Abs($d) -le $deadUs) { 0.0 } else { $K * ($d - [Math]::Sign($d)*$deadUs) }
    $ts = New-Object System.Collections.Generic.List[double]
    $xs = New-Object System.Collections.Generic.List[double]
    $n  = [int]($dur * $fps)
    for ($i = 0; $i -lt $n; $i++) {
        $t = $i / $fps
        $ts.Add($t)
        $xs.Add($x0 + $v0*$t + 0.5*$a*$t*$t + (Noise $noiseAmp))
    }
    return [pscustomobject]@{ ts=@($ts.ToArray()); xs=@($xs.ToArray()); aTrue=$a }
}

Write-Host "================ _fit_test  $(Get-Date -Format 'HH:mm:ss') ================"

# ---- 1. FitQuad recovers a clean parabola exactly -------------------------------------
$c = MakeCapture 0.5 1480 1600 0 0 0.7 30 0.0 0
$f = FitQuad $c.ts $c.xs
Check "FitQuad: noiseless a" (Near $f.a $c.aTrue 1e-6) ("got {0:N4}, want {1:N4}" -f $f.a, $c.aTrue)
Check "FitQuad: noiseless rms == 0" (Near $f.rms 0 1e-6) ("rms {0:E2}" -f $f.rms)

# ---- 2. a nonzero initial velocity must NOT leak into a -------------------------------
# This is the one that bites in practice: the ball is usually still creeping when capture starts.
# If the fit had no v0 term, that creep would be absorbed as fake acceleration.
$c2 = MakeCapture 0.5 1480 1600 5.0 40.0 0.7 30 0.0 0
$f2 = FitQuad $c2.ts $c2.xs
Check "FitQuad: v0 does not leak into a" (Near $f2.a $c2.aTrue 1e-6) ("a {0:N4} with v0=40 mm/s" -f $f2.a)
Check "FitQuad: recovers v0" (Near $f2.v0 40.0 1e-6) ("v0 {0:N3}" -f $f2.v0)
Check "FitQuad: recovers x0" (Near $f2.x0 5.0 1e-6) ("x0 {0:N3}" -f $f2.x0)

# ---- 3. with camera-grade noise, a is still good enough -------------------------------
$c3 = MakeCapture 0.5 1480 1600 0 0 0.7 30 0.5 0
$f3 = FitQuad $c3.ts $c3.xs
Check "FitQuad: +-0.5 mm noise, a within 5%" (Near $f3.a $c3.aTrue ([Math]::Abs($c3.aTrue)*0.05)) `
      ("got {0:N1}, want {1:N1}, rms {2:N2}" -f $f3.a, $c3.aTrue, $f3.rms)
Check "FitQuad: rms reflects the noise" ($f3.rms -gt 0.1 -and $f3.rms -lt 1.0) ("rms {0:N3} mm" -f $f3.rms)

# ---- 4. too few samples must be refused, not guessed ---------------------------------
$short = FitQuad @(0.0,0.1,0.2) @(0.0,1.0,4.0)
Check "FitQuad: refuses n<6 (returns null)" ($short -eq $null) "returned null as required"

# ---- 5. the full pipeline recovers K_total and us_center ------------------------------
# THE headline test: this is exactly what -Step Sweep does.
$Ktrue  = 0.42
$UCtrue = 1487.0
$usList = @(1400,1440,1470,1500,1530,1560,1600)
$pts = New-Object System.Collections.Generic.List[object]
foreach ($us in $usList) {
    $cc = MakeCapture $Ktrue $UCtrue $us 0 0 0.7 30 0.4 0
    $ff = FitQuad $cc.ts $cc.xs
    $pts.Add([pscustomobject]@{ us=[double]$us; a=$ff.a; rms=$ff.rms; travel=$ff.span; dur=$ff.dur })
}
$mv  = @($pts | Where-Object { FitIsMoving $_ })
$lin = FitLine @($mv | ForEach-Object { $_.us }) @($mv | ForEach-Object { $_.a })
$Kfit  = $lin.m
$UCfit = -$lin.b / $lin.m
Check "pipeline: all 7 points judged MOVING" ($mv.Count -eq 7) ("moving {0}/7" -f $mv.Count)
Check "pipeline: K_total within 2%" (Near $Kfit $Ktrue ($Ktrue*0.02)) ("got {0:N4}, want {1:N4}" -f $Kfit, $Ktrue)
Check "pipeline: us_center within 2 us" (Near $UCfit $UCtrue 2.0) ("got {0:N2}, want {1:N2}" -f $UCfit, $UCtrue)

# ---- 6. sign is recovered when the linkage is mirrored --------------------------------
$pts6 = New-Object System.Collections.Generic.List[object]
foreach ($us in $usList) {
    $cc = MakeCapture (-$Ktrue) $UCtrue $us 0 0 0.7 30 0.4 0
    $ff = FitQuad $cc.ts $cc.xs
    $pts6.Add([pscustomobject]@{ us=[double]$us; a=$ff.a; rms=$ff.rms; travel=$ff.span; dur=$ff.dur })
}
$mv6  = @($pts6 | Where-Object { FitIsMoving $_ })
$lin6 = FitLine @($mv6 | ForEach-Object { $_.us }) @($mv6 | ForEach-Object { $_.a })
Check "pipeline: negative K recovered" ($lin6.m -lt 0 -and (Near $lin6.m (-$Ktrue) ($Ktrue*0.02))) ("K {0:N4}" -f $lin6.m)
# us_center via the GLOBAL line is the weaker estimator (it is the fallback path); 5 us is its honest
# accuracy at this noise level. FitDeadzone is the primary path and is asserted tightly in 7a.
Check "pipeline: us_center unaffected by sign (global fallback, 5 us)" (Near (-$lin6.b/$lin6.m) $UCtrue 5.0) ("uc {0:N2}" -f (-$lin6.b/$lin6.m))

# ---- 7. a deadband shows up as stuck points, and does NOT corrupt K -------------------
# The point of separating stuck points: if they were fed to the regression they would pull the slope
# down and shift the zero. Assert both that they are detected AND that K survives.
$dead = 25.0
# Symmetric list with >=4 moving points per side. Branch fitting on only 2 points per side is an exact
# line through two noisy values and amplifies the noise (observed: 26% slope asymmetry) - so a real
# sweep should keep both sides populated. That is a usage rule, recorded here as a test.
$pts7 = New-Object System.Collections.Generic.List[object]
foreach ($us in @(1350,1390,1420,1450,1470,1487,1500,1520,1550,1580,1610)) {
    $cc = MakeCapture $Ktrue $UCtrue $us 0 0 0.7 30 0.4 $dead
    $ff = FitQuad $cc.ts $cc.xs
    $pts7.Add([pscustomobject]@{ us=[double]$us; a=$ff.a; rms=$ff.rms; travel=$ff.span; dur=$ff.dur })
}
$mv7 = @($pts7 | Where-Object { FitIsMoving $_ })
$st7 = @($pts7 | Where-Object { -not (FitIsMoving $_) })
$stUs = @($st7 | ForEach-Object { $_.us } | Sort-Object)
Check "deadband: the inside-band points are flagged stuck" ($st7.Count -ge 3) ("stuck at us = {0}" -f ($stUs -join ','))
Check "deadband: no point outside the band is flagged stuck" `
      (($st7 | Where-Object { [Math]::Abs($_.us - $UCtrue) -gt ($dead + 15) }).Count -eq 0) `
      ("widest stuck offset {0:N0} us vs band {1:N0}" -f (($st7 | ForEach-Object { [Math]::Abs($_.us-$UCtrue) } | Measure-Object -Maximum).Maximum), $dead)
# A single global line through BOTH branches is provably shallow: the branches are offset toward each
# other by +-K*dead. For K=0.42 / dead=25 / this us list the exact global slope is 0.291, i.e. K would
# be underestimated by 31%. Assert that known-bad number so the test documents WHY branch fitting
# exists, and would fail loudly if someone "simplified" it back to one line.
$lin7 = FitLine @($mv7 | ForEach-Object { $_.us }) @($mv7 | ForEach-Object { $_.a })
Check "deadband: a single global line IS biased low (this is why FitDeadzone exists)" `
      ($lin7.m -lt ($Ktrue*0.80)) ("global slope {0:N4} vs true {1:N4} = {2:N0}% low" -f $lin7.m, $Ktrue, (100*(1-$lin7.m/$Ktrue)))

# Branch fitting must recover all three unknowns despite the deadband.
# --- 7a. the estimator itself must be UNBIASED: same plant, negligible noise ---------
# This is the test that says "the maths is right". Separating it from the noisy case matters: if only
# the noisy case existed, a real bias and ordinary scatter would look the same, and tightening the
# tolerance would just make the test flaky instead of finding the bug.
$ptsQ = New-Object System.Collections.Generic.List[object]
foreach ($us in @(1350,1390,1420,1450,1470,1487,1500,1520,1550,1580,1610)) {
    $cc = MakeCapture $Ktrue $UCtrue $us 0 0 0.7 30 0.002 $dead
    $ff = FitQuad $cc.ts $cc.xs
    $ptsQ.Add([pscustomobject]@{ us=[double]$us; a=$ff.a; rms=$ff.rms; travel=$ff.span; dur=$ff.dur })
}
$mvQ = @($ptsQ | Where-Object { FitIsMoving $_ })
$dzQ = FitDeadzone @($mvQ | ForEach-Object { $_.us }) @($mvQ | ForEach-Object { $_.a })
Check "FitDeadzone[clean]: K exact to 0.5%" ($dzQ -ne $null -and (Near $dzQ.K $Ktrue ($Ktrue*0.005))) `
      $(if ($dzQ) { "K {0:N5}, want {1:N5}" -f $dzQ.K, $Ktrue } else { "null" })
Check "FitDeadzone[clean]: us_center exact to 0.5 us" ($dzQ -ne $null -and (Near $dzQ.usCenter $UCtrue 0.5)) `
      $(if ($dzQ) { "uc {0:N3}" -f $dzQ.usCenter } else { "null" })
Check "FitDeadzone[clean]: deadband exact to 1 us" ($dzQ -ne $null -and (Near $dzQ.deadUs $dead 1.0)) `
      $(if ($dzQ) { "dead {0:N3}" -f $dzQ.deadUs } else { "null" })

# --- 7b. with realistic camera noise, assert the ACHIEVABLE accuracy -----------------
# These tolerances are the honest information content of 6 usable points at +-0.4 mm noise over a
# 0.7 s capture, not a wish. They are also the numbers to quote when deciding how far to trust the
# identified values: K to ~10% is harmless (a PD loop tolerates 2x), us_center to ~6 us costs about
# 0.9 mm of steady-state ball offset, and the deadband figure is only ever used as a go/no-go against
# the 0.025 m/s^2 limit-cycle threshold. Want better? Lengthen the capture - se(a) falls as 1/T^2.
$dz = FitDeadzone @($mv7 | ForEach-Object { $_.us }) @($mv7 | ForEach-Object { $_.a })
Check "FitDeadzone: returns a result" ($dz -ne $null) $(if ($dz) { "nPos=$($dz.nPos) nNeg=$($dz.nNeg) weak=$($dz.weak)" } else { "null" })
if ($dz) {
    Check "FitDeadzone: K within 10% (noise-limited)" (Near $dz.K $Ktrue ($Ktrue*0.10)) ("K {0:N4}, want {1:N4}" -f $dz.K, $Ktrue)
    Check "FitDeadzone: us_center within 6 us (noise-limited)" (Near $dz.usCenter $UCtrue 6.0) ("uc {0:N2}, want {1:N2}" -f $dz.usCenter, $UCtrue)
    Check "FitDeadzone: deadband within 8 us (noise-limited)" (Near $dz.deadUs $dead 8.0) ("dead {0:N1}, want {1:N1}" -f $dz.deadUs, $dead)
    # asymPct is a diagnostic computed from per-branch slopes, which are noisy with few points per
    # branch. Assert only that it is finite and not absurd - NOT that it is small. Asserting a tight
    # bound here would be asserting the noise level, which is how you get a test that fails randomly.
    Check "FitDeadzone: asymmetry diagnostic is reported" ($dz.asymPct -ge 0 -and $dz.asymPct -lt 100.0) ("asym {0:N1}% (diagnostic only)" -f $dz.asymPct)
    Check "FitDeadzone: joint fit residual is noise-sized" ($dz.rms -lt 12.0) ("rms {0:N2} mm/s^2 over n={1}" -f $dz.rms, $dz.n)
}

# And with NO deadband it must still work, reporting dead ~ 0 rather than inventing one.
$dz0 = FitDeadzone @($mv | ForEach-Object { $_.us }) @($mv | ForEach-Object { $_.a })
Check "FitDeadzone: no-deadband case reports dead ~ 0" ($dz0 -ne $null -and [Math]::Abs($dz0.deadUs) -lt 6.0) `
      $(if ($dz0) { "dead {0:N2} us, K {1:N4}" -f $dz0.deadUs, $dz0.K } else { "null" })

# One-sided sweep must be refused, not guessed: it genuinely cannot separate K from the deadband.
$oneSide = FitDeadzone @(1550.0,1560.0,1580.0,1600.0) @(10.0,14.0,22.0,30.0)
Check "FitDeadzone: refuses a one-sided sweep" ($oneSide -eq $null) "returned null as required"

# ---- 8. Solve3 rejects a singular system instead of returning garbage ------------------
$sing = Solve3 @(@(1,2,3),@(2,4,6),@(1,1,1)) @(1,2,3)
Check "Solve3: singular matrix returns null" ($sing -eq $null) "returned null as required"

# ---- 9. Median / Stdev sanity ----------------------------------------------------------
Check "Median odd" ((Median @(5,1,3)) -eq 3) "median(5,1,3)=3"
Check "Stdev known" (Near (Stdev @(2,4,4,4,5,5,7,9)) 2.1381 1e-3) ("{0:N4}" -f (Stdev @(2,4,4,4,5,5,7,9)))

# ---- 10. FitHerdUs: the only closed loop in the flow, so the sign must be provable -----
# A sign error here does not give a wrong number, it throws the ball at the end stop. Hence tests for
# both polarities of K, for braking, and for both clamps.
Write-Host "-- FitHerdUs (shuttle repositioning) --"
$uc = 1480.0; $Kp = 4.0; $Kd = 4.0; $mn = 1000; $mx = 2000; $mo = 90.0
# K > 0 : larger us pushes the ball toward +x
$u = FitHerdUs 40.0 0.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "K>0: ball at +40 -> push toward -x (us < uc)" ($u -lt $uc) ("us {0} vs uc {1}" -f $u, $uc)
$u = FitHerdUs -40.0 0.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "K>0: ball at -40 -> push toward +x (us > uc)" ($u -gt $uc) ("us {0}" -f $u)
# K < 0 : mirrored linkage must herd correctly with no extra branch
$u = FitHerdUs 40.0 0.0 0.0 (-0.42) $uc $Kp $Kd $mn $mx $mo
Check "K<0: ball at +40 -> us > uc (mirrored)" ($u -gt $uc) ("us {0}" -f $u)
$u = FitHerdUs -40.0 0.0 0.0 (-0.42) $uc $Kp $Kd $mn $mx $mo
Check "K<0: ball at -40 -> us < uc (mirrored)" ($u -lt $uc) ("us {0}" -f $u)
# at target but coasting fast -> must brake, i.e. push against the motion
$u = FitHerdUs 0.0 60.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "at target moving +60 mm/s -> brakes (us < uc)" ($u -lt $uc) ("us {0}" -f $u)
$u = FitHerdUs 0.0 -60.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "at target moving -60 mm/s -> brakes (us > uc)" ($u -gt $uc) ("us {0}" -f $u)
# settled -> command centre
$u = FitHerdUs 0.0 0.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "settled at target -> commands uc" ($u -eq [int]$uc) ("us {0}" -f $u)
# offset clamp: a huge error must not ask for a huge pulse
$u = FitHerdUs 500.0 0.0 0.0 0.42 $uc $Kp $Kd $mn $mx $mo
Check "offset clamp respected (|us-uc| <= maxOffset)" ([Math]::Abs($u - $uc) -le ($mo + 0.5)) ("us {0}, offset {1:N0}" -f $u, ($u-$uc))
# absolute clamp: never leave what the firmware accepts
$u = FitHerdUs 500.0 0.0 0.0 0.42 1980.0 $Kp $Kd $mn $mx 400.0
Check "absolute clamp respected (us within min..max)" ($u -ge $mn -and $u -le $mx) ("us {0}" -f $u)
# degenerate K must be refused, not divided by
$u = FitHerdUs 40.0 0.0 0.0 0.0 $uc $Kp $Kd $mn $mx $mo
Check "K=0 refused (returns null)" ($u -eq $null) "returned null as required"
# closed-loop sanity: simulate the herd and require it to actually converge, not just point the right way
{
    $xs = 60.0; $vs = 0.0; $Kt = 0.42; $ucT = 1487.0; $dt = 0.15   # 0.15 s ~ the real ~7 Hz command rate
    $ok = $false
    for ($i = 0; $i -lt 200; $i++) {
        $cmd = FitHerdUs $xs $vs 0.0 $Kt $ucT $Kp $Kd $mn $mx $mo
        $a = $Kt * ($cmd - $ucT)
        $vs += $a * $dt
        $xs += $vs * $dt
        if ([Math]::Abs($xs) -lt 5.0 -and [Math]::Abs($vs) -lt 25.0) { $ok = $true; break }
        if ([Math]::Abs($xs) -gt 130.0) { break }                    # would have hit the end stop
    }
    Check "herd loop converges from +60 mm without hitting the stop" $ok ("ended x={0:N1} v={1:N1} after {2} steps" -f $xs, $vs, $i)
}.Invoke()

# ---- 11. frame builders vs the K230 delivery doc's LITERAL checksums --------------------
# These two strings are copied verbatim out of the delivery's protocol doc, checksum included. Using
# them as a fixture - rather than recomputing and comparing against ourselves - is what makes this a
# real cross-check: it catches both a wrong XOR rule AND a wrong number formatting. It already earned
# its keep once, catching '+0.00' being sent where the device sends '0.00' (different bytes, different
# checksum, so the injected frame would not have been a faithful rehearsal of the real camera).
Write-Host "-- frame builders vs delivery doc literals --"
$spSaved = $sp; $sp = $null          # the builders are pure string functions; no port needed
. (Join-Path $PSScriptRoot "_serial_ball.ps1")
Check "FrameBP(1, +5.23) matches doc *12" ((FrameBP 1 5.23) -eq '$BP,1,+5.23*12') ("got " + (FrameBP 1 5.23))
Check "FrameBP(0,  0.00) matches doc *3C" ((FrameBP 0 0.0)  -eq '$BP,0,0.00*3C')  ("got " + (FrameBP 0 0.0))
Check "zero is unsigned (not +0.00)" (((FrameBP 0 0.0) -notmatch '\+0\.00')) "device leaves zero bare"
Check "negative keeps its sign" ((FrameBP 1 -12.0) -match '^\$BP,1,-12\.00\*') ("got " + (FrameBP 1 -12.0))
Check "corrupt flag really changes the checksum" ((FrameBP 1 5.23 -Corrupt) -ne (FrameBP 1 5.23)) "needed for the reject-path tests"
Check "UfChecksum matches the doc on the V example" (("{0:X2}" -f (UfChecksum "V,-1,0,0,0")) -eq "7A") ("got " + ("{0:X2}" -f (UfChecksum "V,-1,0,0,0")))
$sp = $spSaved

Write-Host ""
if ($script:fails -gt 0) { Write-Host ("RESULT: FAIL - {0}/{1} checks failed" -f $script:fails, $script:total) -ForegroundColor Red; exit 1 }
Write-Host ("RESULT: PASS - {0}/{0} checks passed" -f $script:total) -ForegroundColor Green
exit 0
