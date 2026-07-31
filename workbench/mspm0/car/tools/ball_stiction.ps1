# ball_stiction.ps1 - measure the BREAKAWAY beam angle: how far must the beam tilt before a ball that is
# standing still actually starts to roll?
#
# WHY THIS EXISTS
#   2026-07-31, m12 clamped to 1.00 deg: the beam sat at the correct side (srv_us 1161 = centre + 75 us,
#   i.e. -122 mm/s^2 toward the target) while the camera reported cx = 1710 for 62 consecutive FRESH
#   frames. The ball simply did not move. Meanwhile the unclamped run commanded 2.40 deg rms and the ball
#   did move, badly (std 14 mm). So breakaway sits somewhere between 1.0 and 2.4 deg, and it is the
#   dominant term in this system - it explains the 14 mm scatter as a stick-slip limit cycle rather than
#   as a tuning problem, and no gain change can remove it.
#
#   The number matters because it eats the authority budget directly:
#     usable control authority = available authority - breakaway
#   With 1.67 deg available on the weaker side, a 1.3 deg breakaway leaves 0.37 deg to actually control
#   with. That is the difference between "tune the gains" and "fix the mechanics", so it must be measured
#   rather than assumed.
#
# METHOD
#   Park level, wait until the ball is genuinely at rest (several frames with an unchanged position - the
#   camera rounds, so a stationary ball reports a bit-identical value, which is a cleaner rest detector
#   than a velocity threshold). Then tilt away from centre in small steps, dwelling at each, and record
#   the first step at which the ball has moved more than -MoveMm. Repeat for the other direction, because
#   the two are NOT the same: the level pulse is off-centre in the travel, and the tube may not be
#   symmetric either.
#
# WHY STEP-AND-DWELL RATHER THAN A SLOW RAMP
#   A ramp confounds breakaway with rate: the ball can creep under a slowly rising angle and there is no
#   single moment to call the threshold. Discrete steps with a dwell give an unambiguous answer per step,
#   and the step size is the stated resolution.
#
# SAFETY
#   Every step is clamped to CFG_SERVO_MIN_US..MAX_US, the beam is parked level after each direction, and
#   a position guard aborts before the ball can reach an end stop. Nothing drives the motors.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_stiction.ps1 -Port COM30
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_stiction.ps1 -Port COM30 -StepUs 5 -DwellMs 1500
#
# EXIT CODES: 0 = measured both directions, 1 = a direction never broke away, 2 = could not measure
# ASCII only in the code.
param(
    [string]$Port     = "COM30",
    [int]$Baud        = 115200,
    [int]$CenterUs    = 1086,     # config.h CFG_SERVO_CENTER_US (identified 2026-07-31)
    [double]$UsPerDeg = 75.4,     # config.h CFG_SERVO_US_PER_DEG
    [double]$KTotal   = 1.622,    # |K| mm/s^2 per us, from ball_ident -Step Sweep
    [int]$MinUs       = 960,
    [int]$MaxUs       = 1320,
    [int]$StepUs      = 10,       # tilt resolution; 10 us = 0.13 deg
    [int]$DwellMs     = 1200,     # how long each step gets to prove itself
    [double]$MoveMm   = 3.0,      # movement that counts as "it broke away"
    [double]$GuardMm  = 100.0,    # abort before the end stops (they sit near 125 mm)
    [double]$CxPerMm  = 100.0,
    [double]$TrajNeed = 139.0,    # mm/s^2 the +-50 mm trajectory needs (4*50/1.2^2), for the verdict
    [string]$Out      = "_logs\ball_stiction_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = $null
function Finish([string]$verdict, [int]$code) {
    L ""; L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        Write-Host "(log -> $Out)"
    } catch {}
    if ($sp -and $sp.IsOpen) { try { $sp.Write("U$CenterUs`n") } catch {}; try { $sp.Close(); $sp.Dispose() } catch {} }
    exit $code
}

L ("================ ball_stiction  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
L ("port {0}   centre {1} us   step {2} us ({3:N3} deg)   dwell {4} ms   move threshold {5:N1} mm" -f `
    $Port, $CenterUs, $StepUs, ($StepUs/$UsPerDeg), $DwellMs, $MoveMm)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

function Send([string]$c) { foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

$reBALL = [regex]'BALL:(?<cx>-?\d+),(?<us>-?\d+),(?<age>\d+),(?<st>\d+),(?<id>-?\d+)'
$rx = ''
$script:lastStamp = -1
$script:frames = New-Object System.Collections.Generic.List[object]
function Drain() {
    $t = ""
    try { $t = $sp.ReadExisting() } catch {}
    if ($t) { $script:rx += $t }
    while ($script:rx.Contains("`n")) {
        $i = $script:rx.IndexOf("`n")
        $ln = $script:rx.Substring(0, $i); $script:rx = $script:rx.Substring($i + 1)
        $m = $reBALL.Match($ln)
        if (-not $m.Success) { continue }
        $st = [int]$m.Groups['st'].Value
        if ($st -eq $script:lastStamp) { continue }      # same frame re-reported; not a new measurement
        $script:lastStamp = $st
        if ([int]$m.Groups['id'].Value -eq -1) { continue }
        $script:frames.Add([pscustomobject]@{ cx = [int]$m.Groups['cx'].Value; st = $st })
    }
}
function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 15 } }
function Latest() { Drain; if ($script:frames.Count -eq 0) { return $null }; return $script:frames[$script:frames.Count-1] }

# Rest detector: N consecutive frames with a bit-identical cx. The camera rounds its output, so a truly
# stationary ball repeats exactly - which is a sharper test than any velocity threshold, and it also
# avoids trusting the firmware's observer (whose v_est is the thing under suspicion).
function WaitAtRest([double]$timeoutS) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $same = 0; $prev = $null
    while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
        Drain
        $f = Latest
        if ($null -ne $f) {
            if ($null -ne $prev -and $f.cx -eq $prev) { $same++ } else { $same = 0 }
            $prev = $f.cx
            if ($same -ge 6) { return $f.cx }
        }
        Start-Sleep -Milliseconds 25
    }
    return $null
}

try {
    try { $sp.DiscardInBuffer() } catch {}
    Send "z";   Start-Sleep -Milliseconds 300
    Send "l1";  Start-Sleep -Milliseconds 250
    Send "f25"; Start-Sleep -Milliseconds 250
    Send ("U" + $CenterUs); Start-Sleep -Milliseconds 400     # level, open loop; m12 stays OUT of this
    Wait 1.0

    # Bring the ball back toward the middle, open loop, before a direction that needs the room.
    #
    # Needed because a breakaway event does not stop politely: the first run broke away toward -x and the
    # ball ended up at +104 mm, which left no room to measure the other direction and the run came back
    # INCONCLUSIVE for half the answer. Recovering here keeps the tool self-sufficient.
    # The push used is deliberately generous (breakaway is ~70 us, so 110 us is comfortably above it) and
    # it is released as soon as the ball is inside the target window - the ball then coasts, which is fine
    # because the next step waits for rest anyway.
    function RecoverToMiddle([double]$withinMm, [double]$timeoutS) {
        $f = Latest
        if ($null -eq $f) { Wait 0.8; $f = Latest }
        if ($null -eq $f) { return $false }
        $x = $f.cx / $CxPerMm
        if ([Math]::Abs($x) -le $withinMm) { return $true }
        # us above centre pushes toward -x; so to move toward -x (positive x now) we go above centre.
        $push = if ($x -gt 0) { $CenterUs + 110 } else { $CenterUs - 110 }
        if ($push -gt $MaxUs) { $push = $MaxUs }
        if ($push -lt $MinUs) { $push = $MinUs }
        L ("  recovering: ball at {0:N1} mm, tilting to {1} us until it is inside +-{2:N0} mm" -f $x, $push, $withinMm)
        Send ("U" + $push)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
            Wait 0.15
            $f = Latest
            if ($null -eq $f) { continue }
            $x = $f.cx / $CxPerMm
            if ([Math]::Abs($x) -le $withinMm) { break }
            # Overshot to the far side: reverse rather than keep pushing it into the other stop.
            $wantPush = if ($x -gt 0) { $CenterUs + 110 } else { $CenterUs - 110 }
            if ($wantPush -ne $push) { $push = $wantPush; Send ("U" + $push) }
        }
        Send ("U" + $CenterUs)
        $f = Latest
        $ok = ($null -ne $f) -and ([Math]::Abs($f.cx / $CxPerMm) -le ($withinMm + 25.0))
        L ("  recovered to {0:N1} mm -> {1}" -f ($(if ($null -ne $f) { $f.cx / $CxPerMm } else { 0 })), $(if ($ok) { "ok" } else { "still too far" }))
        return $ok
    }

    $results = @{}
    foreach ($dir in @(-1, 1)) {
        $dirName = if ($dir -lt 0) { "toward -x (us above centre)" } else { "toward +x (us below centre)" }
        # us above centre pushes the ball toward -x (K is negative), so the sign mapping is: dir -1 => +us.
        $usSign = if ($dir -lt 0) { 1 } else { -1 }
        $limit  = if ($usSign -gt 0) { $MaxUs - $CenterUs } else { $CenterUs - $MinUs }

        L ""
        L ("---- direction: {0} ; available {1} us = {2:N2} deg ----" -f $dirName, $limit, ($limit/$UsPerDeg))
        Send ("U" + $CenterUs); Start-Sleep -Milliseconds 300
        Wait 0.6
        # Each direction needs room to run into, so start from the middle rather than wherever the
        # previous breakaway threw the ball.
        [void](RecoverToMiddle 35.0 12.0)
        $rest = WaitAtRest 8.0
        if ($null -eq $rest) {
            L "  the ball never came to rest with the beam level."
            L "  Either the level pulse is wrong (it should hold a stationary ball), or the tube is not"
            L "  level in the other axis, or the car is being knocked. Cannot measure breakaway from a"
            L "  moving start, so this direction is skipped."
            $results[$dir] = $null
            continue
        }
        $x0 = $rest / $CxPerMm
        L ("  at rest at {0:N2} mm; stepping the tilt up in {1} us increments" -f $x0, $StepUs)
        if ([Math]::Abs($x0) -gt ($GuardMm - 20.0)) {
            L ("  ball is at {0:N1} mm, too close to the end stop to run this direction safely." -f $x0)
            $results[$dir] = $null
            continue
        }

        $broke = $null
        for ($d = $StepUs; $d -le $limit; $d += $StepUs) {
            $us = $CenterUs + $usSign * $d
            Send ("U" + $us)
            $script:frames.Clear()
            Wait ($DwellMs / 1000.0)
            $f = Latest
            if ($null -eq $f) { L ("    {0,5} us  (no frames)" -f $us); continue }
            $x = $f.cx / $CxPerMm
            $moved = $x - $x0
            L ("    {0,5} us  = {1,5:N2} deg from level -> moved {2,7:N2} mm" -f $us, ($d/$UsPerDeg), $moved)
            if ([Math]::Abs($moved) -ge $MoveMm) { $broke = $d; break }
            if ([Math]::Abs($x) -ge $GuardMm) { L "    guard reached, stopping this direction"; break }
        }
        Send ("U" + $CenterUs); Start-Sleep -Milliseconds 300
        $results[$dir] = $broke
        if ($null -eq $broke) {
            L ("  never broke away within the {0} us available on this side ({1:N2} deg)." -f $limit, ($limit/$UsPerDeg))
        } else {
            L ("  BREAKAWAY at {0} us = {1:N2} deg = {2:N0} mm/s^2 equivalent" -f `
                $broke, ($broke/$UsPerDeg), ($broke*$KTotal))
        }
        # Let the ball settle again before the other direction, and recover it toward the middle a little.
        Wait 1.5
    }
} finally {
    try { Send ("U" + $CenterUs) } catch {}
    try { Send "z" } catch {}
}

L ""
L "---- summary ----"
$bNeg = $results[-1]; $bPos = $results[1]
$degUp = ($MaxUs - $CenterUs) / $UsPerDeg
$degDn = ($CenterUs - $MinUs) / $UsPerDeg
foreach ($kv in @(@(-1, $bNeg, $degUp, "toward -x"), @(1, $bPos, $degDn, "toward +x"))) {
    $b = $kv[1]
    if ($null -eq $b) { L ("  {0,-10} : no breakaway found" -f $kv[3]); continue }
    $bd = $b / $UsPerDeg
    L ("  {0,-10} : breakaway {1,5:N2} deg   available {2,5:N2} deg   =>  usable {3,5:N2} deg ({4:N0}%)" -f `
        $kv[3], $bd, $kv[2], ($kv[2] - $bd), (100.0 * ($kv[2] - $bd) / $kv[2]))
}
if ($null -ne $bNeg -and $null -ne $bPos) {
    $worstB = [Math]::Max($bNeg, $bPos) / $UsPerDeg
    $worstAvail = [Math]::Min($degUp, $degDn)
    $usable = $worstAvail - $worstB
    $needDeg = $TrajNeed / 7007.0 * 180.0 / [Math]::PI
    L ""
    L ("  worst case: breakaway {0:N2} deg out of {1:N2} deg available => {2:N2} deg of real control" -f `
        $worstB, $worstAvail, $usable)
    L ("  the trajectory needs {0:N2} deg ({1:N0} mm/s^2) ON TOP of breakaway" -f $needDeg, $TrajNeed)
    if ($usable -lt $needDeg) {
        L ""
        L  "  => THIS is the binding constraint, and it is mechanical. Gains cannot help: below the"
        L  "     breakaway angle the ball does not respond at all, so the loop can only stick-slip."
        L  "     In order of expected payoff:"
        L  "       1. reduce friction at the source - clean the tube and the ball, check the inner wall for"
        L  "          a dent or a burr (the task statement itself excludes dented tubes), check the tube is"
        L  "          straight and not sagging, and that it is level in the OTHER axis too;"
        L  "       2. re-centre the servo horn so the level pulse sits mid-travel - that alone converts the"
        L  ("          wasted side into authority (from {0:N2} to {1:N2} deg symmetric);" -f $worstAvail, (($MaxUs-$MinUs)/2.0/$UsPerDeg))
        L  "       3. lengthen the horn / shorten the beam link to get more degrees per us;"
        L  "       4. only then tune, and add a small dither or a breakaway feedforward so the controller"
        L  "          does not have to wind up through the dead band every time."
        Finish ("FAIL - stick-slip limited: {0:N2} deg usable vs {1:N2} deg needed" -f $usable, $needDeg) 1
    }
    L ("  => {0:N0}% headroom above breakaway. Tuning is the productive lever." -f (100.0*($usable/$needDeg - 1.0)))
    Finish ("PASS - {0:N2} deg usable after {1:N2} deg breakaway" -f $usable, $worstB) 0
}
Finish "INCONCLUSIVE - at least one direction produced no breakaway measurement" 2
