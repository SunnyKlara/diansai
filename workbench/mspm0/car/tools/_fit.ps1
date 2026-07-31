# _fit.ps1 - least-squares helpers for the plant-identification scripts. Dot-source it.
#
# WHY THIS IS A SEPARATE FILE
#   ball_ident.ps1 opens a serial port at the top, so it cannot be dot-sourced to test the maths.
#   Splitting the maths out lets _fit_test.ps1 check it against synthetic data with a KNOWN answer,
#   BEFORE anyone is standing at the bench. The failure this guards against is nasty: a wrong fit
#   yields a wrong K_total, which yields wrong loop gains, and the symptom looks exactly like a bad
#   mechanism. We would then spend hours on the linkage while the bug was in a regression.
#
# ASCII only on purpose.

# Solve a 3x3 system by Gauss-Jordan with partial pivoting. Written out rather than pulled from a
# library so the scripts have zero dependencies on the competition floor.
function Solve3($A, $b) {
    $M = @(); for ($i=0; $i -lt 3; $i++) { $M += ,@([double]$A[$i][0], [double]$A[$i][1], [double]$A[$i][2], [double]$b[$i]) }
    for ($col = 0; $col -lt 3; $col++) {
        $piv = $col
        for ($r = $col+1; $r -lt 3; $r++) { if ([Math]::Abs($M[$r][$col]) -gt [Math]::Abs($M[$piv][$col])) { $piv = $r } }
        if ([Math]::Abs($M[$piv][$col]) -lt 1e-12) { return $null }
        if ($piv -ne $col) { $t = $M[$col]; $M[$col] = $M[$piv]; $M[$piv] = $t }
        for ($r = 0; $r -lt 3; $r++) {
            if ($r -eq $col) { continue }
            $f = $M[$r][$col] / $M[$col][$col]
            for ($k = $col; $k -lt 4; $k++) { $M[$r][$k] = $M[$r][$k] - $f * $M[$col][$k] }
        }
    }
    # IMPORTANT: EVERY division must stay parenthesised. PowerShell gives the comma operator HIGHER precedence
    # than arithmetic, so `@(a/b, c/d, e/f)` parses as `a / (b,c) / (d,e) / f` - it divides by an ARRAY
    # and dies with "[System.Object[]] does not contain a method named 'op_Division'". Caught by
    # _fit_test.ps1; without that test this function returned nothing and the whole identification
    # would have silently produced no result at the bench.
    return @( ($M[0][3]/$M[0][0]), ($M[1][3]/$M[1][1]), ($M[2][3]/$M[2][2]) )
}

# Least-squares fit x(t) = x0 + v0*t + 0.5*a*t^2 .
# Returns a, v0, x0, the RMS residual (the honest error bar - a flat or noisy segment shows up as a
# large rms), plus n / span / dur which the caller uses to decide whether the ball really moved.
# NOTE the 0.5: the returned 'a' is a physical acceleration, i.e. 2x the fitted t^2 coefficient.
function FitQuad($ts, $xs) {
    $n = $ts.Count
    if ($n -lt 6) { return $null }
    # Normal equations for the basis [1, t, t^2] need sums of t^0 .. t^4.
    $S = @(0.0,0.0,0.0,0.0,0.0)
    foreach ($t in $ts) { $p = 1.0; for ($k = 0; $k -lt 5; $k++) { $S[$k] += $p; $p *= [double]$t } }
    $Sy = 0.0; $Sty = 0.0; $Stty = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $t = [double]$ts[$i]; $y = [double]$xs[$i]
        $Sy += $y; $Sty += $t*$y; $Stty += $t*$t*$y
    }
    $A = @(@($S[0],$S[1],$S[2]), @($S[1],$S[2],$S[3]), @($S[2],$S[3],$S[4]))
    $sol = Solve3 $A @($Sy, $Sty, $Stty)
    if (-not $sol) { return $null }
    $c0 = $sol[0]; $c1 = $sol[1]; $c2 = $sol[2]
    $ss = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $t = [double]$ts[$i]
        $d = [double]$xs[$i] - ($c0 + $c1*$t + $c2*$t*$t)
        $ss += $d*$d
    }
    return [pscustomobject]@{ a = 2.0*$c2; v0 = $c1; x0 = $c0; rms = [Math]::Sqrt($ss/$n); n = $n
                              span = ([double]$xs[$xs.Count-1] - [double]$xs[0])
                              dur  = ([double]$ts[$ts.Count-1] - [double]$ts[0]) }
}

# Ordinary least squares y = m*x + b .
function FitLine($xs, $ys) {
    $n = $xs.Count
    if ($n -lt 3) { return $null }
    $sx=0.0; $sy=0.0; $sxx=0.0; $sxy=0.0
    for ($i=0; $i -lt $n; $i++) {
        $x = [double]$xs[$i]; $y = [double]$ys[$i]
        $sx+=$x; $sy+=$y; $sxx+=$x*$x; $sxy+=$x*$y
    }
    $den = $n*$sxx - $sx*$sx
    if ([Math]::Abs($den) -lt 1e-12) { return $null }
    $m = ($n*$sxy - $sx*$sy) / $den
    $b = ($sy - $m*$sx) / $n
    $ss=0.0; for ($i=0; $i -lt $n; $i++) { $d = [double]$ys[$i] - ($m*[double]$xs[$i] + $b); $ss += $d*$d }
    return [pscustomobject]@{ m=$m; b=$b; rms=[Math]::Sqrt($ss/$n); n=$n }
}

# IMPORTANT: [Math]::Floor, not [int]. PowerShell's [int] cast uses banker's rounding, so [int](3/2) == 2,
# which made Median(5,1,3) return 5 instead of 3 - a silent off-by-one that would have skewed every
# median in the scale step. Also caught by _fit_test.ps1.
function Median($a) { if ($a.Count -eq 0) { return $null }; $s = @($a | Sort-Object); return $s[[int][Math]::Floor($s.Count/2)] }
function Stdev($a) {
    if ($a.Count -lt 2) { return 0.0 }
    $m = 0.0; foreach ($v in $a) { $m += [double]$v }; $m /= $a.Count
    $s = 0.0; foreach ($v in $a) { $s += ([double]$v-$m)*([double]$v-$m) }
    return [Math]::Sqrt($s/($a.Count-1))
}

# Fit a dead-zone characteristic  a = K * (d - dead*sign(d)),  d = us - us_center,  to (us, a) pairs.
#
# WHY NOT ONE GLOBAL LINE (this is the important bit)
#   With a deadband the moving data lies on TWO PARALLEL branches offset toward each other by
#   +-K*dead. A single straight line through both branches therefore comes out SHALLOWER than the
#   real K - and not by a little: for dead=25 us and K=0.42 the global slope is 0.29, i.e. K is
#   underestimated by 31%. Loop gains scale on K, so that error would silently detune the whole
#   ball loop. _fit_test.ps1 caught exactly this.
#
# HOW IT IS FITTED (and why not two separate branch fits)
#   Fitting each branch on its own and averaging the slopes looks natural but is far too noisy: with
#   3 points spanning ~70 us the slope standard error works out at ~26% of K, and the two branches
#   then disagree by ~30% on perfectly clean synthetic data. Measured, not guessed - _fit_test.ps1
#   failed exactly that way before this rewrite.
#
#   Instead note that the dead-zone law is LINEAR IN THREE PARAMETERS if you carry the branch sign as
#   its own regressor:
#       a = K*us + (-K*us_center) + (-K*dead)*sign(a)
#         = b1*us + b2*1 + b3*s
#   so one ordinary 3x3 least squares over ALL moving points on BOTH sides yields
#       K = b1     us_center = -b2/b1     dead = -b3/b1
#   K is now determined by the full sweep span (~260 us) instead of one branch (~70 us), which cuts
#   its standard error by roughly 5x. Same data, same model, much better estimate - purely by writing
#   the model in a form that lets both branches share the slope.
#
# The per-branch slopes are still computed, but only as a DIAGNOSTIC (asymPct): a genuinely asymmetric
# linkage or direction-dependent friction shows up there without being allowed to corrupt K.
#
# Returns $null if either side has fewer than 2 usable points - a one-sided sweep genuinely cannot
# separate K from the deadband (the sign column becomes collinear with the constant column).
function FitDeadzone($us, $a) {
    # Drop weakly-excited points before fitting. A point just outside the deadband has an |a| barely
    # above its own noise floor, so it carries almost no slope information - but because it sits near
    # the branch's zero crossing it drags that crossing around badly. Leaving them in produced a
    # NEGATIVE deadband (branches overlapping, physically impossible) on clean synthetic data.
    # The deadband is recovered by EXTRAPOLATING the well-excited part of each branch to zero, so
    # discarding the weak points is not losing information - it is removing noise from the fit.
    $amax = 0.0
    foreach ($v in $a) { if ([Math]::Abs([double]$v) -gt $amax) { $amax = [Math]::Abs([double]$v) } }
    if ($amax -le 0) { return $null }
    $floor = 0.15 * $amax
    $pu = @(); $pa = @(); $nu = @(); $na = @(); $weak = 0
    for ($i = 0; $i -lt $us.Count; $i++) {
        $av = [double]$a[$i]
        if ([Math]::Abs($av) -lt $floor) { $weak++; continue }
        if ($av -gt 0) { $pu += [double]$us[$i]; $pa += $av }
        else           { $nu += [double]$us[$i]; $na += $av }
    }
    if ($pu.Count -lt 2 -or $nu.Count -lt 2) { return $null }

    # ---- joint 3-parameter least squares over both branches: a = b1*us + b2 + b3*s ----
    $S11=0.0; $S12=0.0; $S13=0.0; $S22=0.0; $S23=0.0; $S33=0.0
    $r1=0.0;  $r2=0.0;  $r3=0.0
    $all = @()
    for ($i=0; $i -lt $pu.Count; $i++) { $all += ,@($pu[$i], $pa[$i],  1.0) }
    for ($i=0; $i -lt $nu.Count; $i++) { $all += ,@($nu[$i], $na[$i], -1.0) }
    foreach ($row in $all) {
        $u = $row[0]; $y = $row[1]; $s = $row[2]
        $S11 += $u*$u; $S12 += $u;   $S13 += $u*$s
        $S22 += 1.0;   $S23 += $s;   $S33 += $s*$s
        $r1  += $u*$y; $r2  += $y;   $r3  += $s*$y
    }
    $A = @(@($S11,$S12,$S13), @($S12,$S22,$S23), @($S13,$S23,$S33))
    $sol = Solve3 $A @($r1,$r2,$r3)
    if (-not $sol) { return $null }
    $K = $sol[0]
    if ([Math]::Abs($K) -lt 1e-9) { return $null }
    $uc   = -$sol[1] / $K
    $dRaw = -$sol[2] / $K
    $ss = 0.0
    foreach ($row in $all) { $d = $row[1] - ($sol[0]*$row[0] + $sol[1] + $sol[2]*$row[2]); $ss += $d*$d }

    # Per-branch slopes: diagnostic only, never allowed to set K (see the header note).
    $kp = $null; $kn = $null
    if ($pu.Count -ge 2) { $kp = if ($pu.Count -eq 2) { ($pa[1]-$pa[0])/($pu[1]-$pu[0]) } else { (FitLine $pu $pa).m } }
    if ($nu.Count -ge 2) { $kn = if ($nu.Count -eq 2) { ($na[1]-$na[0])/($nu[1]-$nu[0]) } else { (FitLine $nu $na).m } }

    return [pscustomobject]@{
        K        = $K
        usCenter = $uc
        # A negative raw deadband is unphysical (it would mean the two branches overlap); it just means
        # the sweep cannot resolve a deadband smaller than its own noise. Report 0 and keep the raw
        # value so the caller can say "below the resolution of this sweep" instead of inventing a number.
        deadUs   = [Math]::Max(0.0, $dRaw)
        deadRaw  = $dRaw
        weak     = $weak
        rms      = [Math]::Sqrt($ss / $all.Count)
        n        = $all.Count
        kPos     = $kp ; kNeg = $kn
        nPos     = $pu.Count ; nNeg = $nu.Count
        # How far apart the two branch slopes are. Large asymmetry means the linkage is not symmetric
        # about centre (crank-slider geometry) or the ball sees different friction each way. With few
        # points per branch this number is itself noisy - read it as a flag, not a measurement.
        asymPct  = if ($kp -ne $null -and $kn -ne $null) { 100.0 * [Math]::Abs($kp - $kn) / [Math]::Abs($K) } else { -1.0 }
    }
}

# Compute the pulse width that herds the ball toward a target, PC-side. Used by the shuttle sweep to
# reposition the ball between measurements WITHOUT a human putting it back by hand.
#
# WHY THIS IS A SEPARATE, TESTED FUNCTION
#   It is the only place in the identification flow that CLOSES A LOOP around the real ball, so a sign
#   error here does not produce a wrong number - it flings the ball into the end stop. And the sign is
#   genuinely easy to get wrong, because K can be either polarity depending on how the horn is fitted.
#   Isolating it means the dangerous part is provable on a PC before any hardware is involved.
#
#   Control law: a_des = -kp*(x - target) - kd*v   then   us = uc + a_des / K
#   kp [1/s^2], kd [1/s], x/target [mm], v [mm/s]. Deliberately soft (herding, not performance):
#   the real loop runs at ~3 rad/s while this wants ~2 rad/s, because commands go out at only ~7 Hz
#   (uart_send pacing is 25 ms/char to protect the MCU's 4-byte RX FIFO, so `U1523\n` costs 150 ms).
#
#   Note it handles K<0 for free: the division flips the offset, so a mirrored linkage herds correctly
#   with no extra branch. That property is asserted in _fit_test.ps1 rather than left to trust.
function FitHerdUs($x, $v, $target, $K, $uc, $kp, $kd, $minUs, $maxUs, $maxOffsetUs) {
    if ([Math]::Abs($K) -lt 1e-9) { return $null }
    $aDes = -$kp * ([double]$x - [double]$target) - $kd * [double]$v
    $off  = $aDes / $K
    # Offset clamp first: it bounds how hard we ever push, independent of where centre happens to be.
    if ($off -gt  $maxOffsetUs) { $off =  $maxOffsetUs }
    if ($off -lt -$maxOffsetUs) { $off = -$maxOffsetUs }
    $us = [int][Math]::Round([double]$uc + $off)
    # Absolute clamp last: never leave the pulse range the firmware itself would accept.
    if ($us -lt $minUs) { $us = $minUs }
    if ($us -gt $maxUs) { $us = $maxUs }
    return $us
}

# Shared "did the ball actually accelerate" gate, so ball_ident.ps1 and its test agree by construction.
# Both tests are noise-relative on purpose, so they work whether xs are in mm or in raw pixels:
#   travel > 5*rms          the ball moved well clear of the measurement scatter
#   |a| > 8*rms/dur^2       the quadratic term is resolvable. For a parabola fit over a window of
#                           length T the standard error on a scales as rms/T^2; 8x is roughly 3 sigma.
#                           Short captures therefore demand a bigger a, which is exactly right - hence
#                           dur is per point, not a global constant.
function FitIsMoving($p) {
    if ($p.dur -le 0.05) { return $false }
    $aFloor = 8.0 * $p.rms / ($p.dur * $p.dur)
    return ([Math]::Abs($p.travel) -gt (5.0 * $p.rms)) -and ([Math]::Abs($p.a) -gt $aFloor)
}
function FitAccelFloor($p) {
    if ($p.dur -le 0.05) { return 0.0 }
    return 8.0 * $p.rms / ($p.dur * $p.dur)
}
