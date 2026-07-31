# ball_traj.ps1 - acceptance test for requirement 3, read from the FIRMWARE's own scorecard.
#
# WHY THIS EXISTS
#   Requirement 3 is the O -> +50 mm -> reverse -> -50 mm round trip, and the firmware already implements
#   it (`P1`, formerly R - see car.c ball_cmd for the letter collision) and scores itself: waypoint errors at the outbound and return targets, plus the total time.
#   Those are the graded quantities. Earlier runs measured a PC-side tracking error instead, which is
#   useful for tuning but is NOT what the task asks about - it averages over the whole path, including the
#   parts nobody scores.
#
# TWO MISTAKES THIS SCRIPT EXISTS TO PREVENT (both cost a run on 2026-07-31)
#   1. `z` wipes the scorecard. stop_all() calls ball_reset_stats(), so sending z before reading leaves
#      traj_phase = 0 and print_ball() then suppresses the `traj ph=` line entirely. Nothing auto-prints
#      on completion either (checked: no print at BALL_TRAJ_DONE), so the order must be
#      R1 -> wait for completion -> `?` -> only then z.
#   2. M / F / p / d are RAM-only. An `M1` left over from an experiment half an hour earlier silently
#      clamped the beam to 1.00 deg - below the 1.14 deg the trajectory needs as pure feedforward - and
#      the run looked like a tuning problem. So every run READS BACK thmax/alpha/kp/kd first and prints
#      them, and it refuses to interpret results if the clamp is below what the trajectory demands.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_traj.ps1 -Port COM30
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_traj.ps1 -Port COM30 -ThetaMaxDeg 2 -Kd 7 -Alpha100 85
#
# EXIT CODES: 0 = both waypoints within the 10 mm gate, 1 = ran but out of gate, 2 = could not measure
# ASCII only in the code.
param(
    [string]$Port      = "COM30",
    [int]$Baud         = 115200,
    [int]$ThetaMaxDeg  = 0,       # 0 = leave whatever is loaded (but it is still read back and checked)
    [double]$Kp        = 0,
    [double]$Kd        = 0,
    [double]$Ki        = -1,      # <0 = leave loaded value; >=0 sends I<Ki*1000>
    [int]$Alpha100     = 0,
    [int]$FfMask       = 3,
    # J<deg x100>: friction feedforward. -1 = leave whatever is loaded.
    [int]$Fric         = -1,
    [double]$GateMm    = 10.0,    # requirement 3 waypoint gate
    [double]$TrajNeedDeg = 1.14,  # feedforward the trajectory needs; used to sanity-check the clamp
    [string]$Out       = "_logs\ball_traj_out.txt"
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
    if ($sp -and $sp.IsOpen) { try { $sp.Write("z`n") } catch {}; try { $sp.Close(); $sp.Dispose() } catch {} }
    exit $code
}

L ("================ ball_traj (requirement 3)  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }
function Send([string]$c) { foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }
function Read([double]$sec) {
    $b = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) { try { $b += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 20 }
    return $b
}

$reBE = [regex]'BE:(?<x>-?\d+),(?<v>-?\d+),(?<r>-?\d+),(?<th>-?\d+),(?<sat>\d+),(?<pk>-?\d+)'
# Raw camera position, captured alongside the observer's estimate.
#
# WHY BOTH: BE:'s x is x_est from the observer, which runs with use_model=1 and predicts the ball from the
# COMMANDED beam angle. When the servo layer clips that command (and the ball layer is not told), the
# model input is fiction and x_est diverges from reality. On 2026-07-31 a kp ladder produced spans like
# "-245.6 .. 143.4 mm" in a +-125 mm tube - which reads as "the ball flew out" and is really the observer
# running away, since the raw camera was simultaneously reporting a perfectly sane -26..+104 mm.
# Reporting the two side by side makes that failure self-evident instead of mystifying.
$reBALL = [regex]'BALL:(?<cx>-?\d+),(?<sus>-?\d+),(?<age>\d+),(?<st>\d+),(?<id>-?\d+)'

try {
    try { $sp.DiscardInBuffer() } catch {}
    Send "z";   Start-Sleep -Milliseconds 250
    Send "l1";  Start-Sleep -Milliseconds 250
    Send "f25"; Start-Sleep -Milliseconds 250
    Send "m12"; Start-Sleep -Milliseconds 300
    Send ("i" + $FfMask); Start-Sleep -Milliseconds 200
    if ($ThetaMaxDeg -gt 0) { Send ("M" + $ThetaMaxDeg); Start-Sleep -Milliseconds 250 }
    if ($Alpha100 -gt 0)    { Send ("F" + $Alpha100);    Start-Sleep -Milliseconds 250 }
    if ($Fric -ge 0)        { Send ("J" + $Fric);        Start-Sleep -Milliseconds 250 }
    if ($Kp -gt 0)          { Send ("p" + [int]($Kp*1000)); Start-Sleep -Milliseconds 200 }
    if ($Kd -gt 0)          { Send ("d" + [int]($Kd*1000)); Start-Sleep -Milliseconds 200 }
    if ($Ki -ge 0)          { Send ("I" + [int]($Ki*1000)); Start-Sleep -Milliseconds 200 }
    Send "t0"; Start-Sleep -Milliseconds 400

    # ---- read back what is ACTUALLY loaded, and prove the ball is READY at O -----------------
    # A live camera is not enough. Requirement 3 starts at O; launching while the previous run is still
    # rolling back from +/-120 mm makes waypoint scatter look like a controller change. That invalidated
    # the first 3x2 friction A/B: only one run actually began near O. Use the last 12 unique camera frames
    # (about 0.5 s at the measured 24-25 fps), not the whole settling history. Every one must remain inside
    # O +/-10 mm and their span must be <=8 mm. Wait at most 4 s so the later 4.8 s trajectory still fits
    # under the firmware's independent hard runtime cap; failure is INCONCLUSIVE and P1 is never sent.
    $settleFrames = 12
    $startGateMm = 10.0
    $startSpanMm = 8.0
    $settleTimeoutS = 4.0
    $cfg = $null
    $ballState = $null
    $ballFail = $null
    $freshStamps = @{}
    $freshCx = New-Object System.Collections.Generic.List[double]
    $settledAtO = $false
    $lastWindowCount = 0
    $lastWindowMin = 0.0
    $lastWindowMax = 0.0
    $lastWindowMaxAbs = 0.0
    $preSw = [System.Diagnostics.Stopwatch]::StartNew()

    while ($preSw.Elapsed.TotalSeconds -lt $settleTimeoutS) {
        try { $sp.DiscardInBuffer() } catch {}
        Send "?"
        $left = $settleTimeoutS - $preSw.Elapsed.TotalSeconds
        $chunkSec = [Math]::Min(1.0, [Math]::Max(0.25, $left))
        $chunk = Read $chunkSec
        foreach ($ln in ($chunk -split "`n")) {
            if ($ln -match 'kp\*1000=(?<kp>\d+)\s+kd\*1000=(?<kd>\d+)\s+ki\*1000=(?<ki>\d+).*?thmax\*10=(?<tm>\d+)\s+a\*100=(?<a>\d+)\s+b\*100=(?<b>\d+)') {
                $cfg = @{ kp = [double]$Matches['kp']/1000.0; kd = [double]$Matches['kd']/1000.0
                          ki = [double]$Matches['ki']/1000.0; thmax = [double]$Matches['tm']/10.0
                          a = [double]$Matches['a']/100.0; b = [double]$Matches['b']/100.0 }
            }
            if ($ln -match '^\[ball\]\s+st=(?<st>\w+)\s+fail=(?<fail>\w+)') {
                $ballState = $Matches['st']; $ballFail = $Matches['fail']
            }
            $cam = $reBALL.Match($ln)
            if ($cam.Success) {
                $age = [int]$cam.Groups['age'].Value
                $stamp = [int]$cam.Groups['st'].Value
                $id = [int]$cam.Groups['id'].Value
                if ($id -ne -1 -and $age -le 150 -and -not $freshStamps.ContainsKey($stamp)) {
                    $freshStamps[$stamp] = 1
                    $freshCx.Add([double]$cam.Groups['cx'].Value / 100.0)
                }
            }
        }

        $lastWindowCount = [Math]::Min($settleFrames, $freshCx.Count)
        if ($lastWindowCount -gt 0) {
            $window = @()
            $first = $freshCx.Count - $lastWindowCount
            for ($wi = $first; $wi -lt $freshCx.Count; $wi++) { $window += $freshCx[$wi] }
            $lastWindowMin = ($window | Measure-Object -Minimum).Minimum
            $lastWindowMax = ($window | Measure-Object -Maximum).Maximum
            $lastWindowMaxAbs = ($window | ForEach-Object { [Math]::Abs($_) } | Measure-Object -Maximum).Maximum
            $span = $lastWindowMax - $lastWindowMin
            L ("start preflight: state={0} fail={1} fresh={2} last={3} raw={4:N1}..{5:N1}mm span={6:N1} max|x|={7:N1}" -f `
                $ballState, $ballFail, $freshStamps.Count, $lastWindowCount,
                $lastWindowMin, $lastWindowMax, $span, $lastWindowMaxAbs)
            if ($lastWindowCount -ge $settleFrames -and $ballState -eq 'HOLD' -and $ballFail -eq 'NONE' -and
                $lastWindowMaxAbs -le $startGateMm -and $span -le $startSpanMm) {
                $settledAtO = $true
                break
            }
        } else {
            L ("start preflight: state={0} fail={1} fresh=0 raw=NONE" -f $ballState, $ballFail)
        }
    }

    if ($null -eq $cfg) { Finish "INCONCLUSIVE - could not read the ball layer's loaded parameters" 2 }
    L ("loaded : kp={0:N2}  kd={1:N2}  ki={2:N3}  thmax={3:N2} deg  alpha={4:N2}  beta={5:N2}  ff={6}" -f `
        $cfg.kp, $cfg.kd, $cfg.ki, $cfg.thmax, $cfg.a, $cfg.b, $FfMask)

    if ($freshStamps.Count -lt $settleFrames -or $ballFail -ne 'NONE') {
        Finish ("INCONCLUSIVE - vision preflight failed (state={0}, fail={1}, fresh={2}/{3:N1}s); restore K230 detection/UART before tuning" -f `
            $ballState, $ballFail, $freshStamps.Count, $preSw.Elapsed.TotalSeconds) 2
    }
    if (-not $settledAtO) {
        Finish ("INCONCLUSIVE - start condition failed: ball did not settle at O (last {0} frames {1:N1}..{2:N1}mm, max|x|={3:N1}; require max|x|<={4:N1} and span<={5:N1})" -f `
            $lastWindowCount, $lastWindowMin, $lastWindowMax, $lastWindowMaxAbs, $startGateMm, $startSpanMm) 2
    }
    L ("start condition PASS: last {0} frames stayed at O ({1:N1}..{2:N1}mm)" -f `
        $lastWindowCount, $lastWindowMin, $lastWindowMax)
    if ($cfg.thmax -lt $TrajNeedDeg) {
        L ("  WARNING: thmax {0:N2} deg is BELOW the {1:N2} deg the trajectory needs as feedforward alone." -f $cfg.thmax, $TrajNeedDeg)
        L  "  Whatever comes out is a clamp artefact, not a tuning result. Pass -ThetaMaxDeg 2 or higher."
    }

    # ---- run it ----
    # P1 must be CONFIRMED, not merely sent. A dropped P1 leaves the loop sitting in HOLD and the run
    # comes back "no scorecard", which reads like a firmware fault and is really one lost character.
    # (2026-07-31: the original letter R was silently eaten by vseg_cmd, which is why this ack check exists.)
    # The banner is unambiguous, so use it as the ack and retry up to three times.
    $head = ""
    $dur = 0.0
    for ($k = 1; $k -le 3; $k++) {
        try { $sp.DiscardInBuffer() } catch {}
        Send "P1"
        $head = Read 1.2
        foreach ($ln in ($head -split "`n")) {
            if ($ln -match 'TRAJ start') { L ("  " + $ln.TrimEnd("`r")) }
            if ($ln -match 't\*10=(\d+)\s*\(') { $dur = [double]$Matches[1] / 10.0 }
        }
        if ($head -match 'TRAJ start') { break }
        L ("  P1 not acknowledged (no TRAJ start banner) - resending, attempt {0}/3" -f $k)
    }
    if ($head -notmatch 'TRAJ start') { Finish "INCONCLUSIVE - P1 never acknowledged; the trajectory did not start" 2 }
    if ($dur -le 0.0) { $dur = 4.8 }
    L ("  trajectory duration reported by firmware: {0:N1}s ; waiting it out plus margin" -f $dur)
    $body = Read ($dur + 1.8)

    # ---- ask for the scorecard BEFORE z (z would reset it) ----
    Send "?"
    $tail = Read 2.0
} finally {
    try { Send "z" } catch {}
}

$all = $head + $body + $tail
$rows = New-Object System.Collections.Generic.List[object]
$cams = New-Object System.Collections.Generic.List[double]
$seenSt = @{}
foreach ($ln in ($all -split "`n")) {
    $m = $reBE.Match($ln)
    if ($m.Success) {
        $rows.Add([pscustomobject]@{
            x = [double]$m.Groups['x'].Value/10.0; r = [double]$m.Groups['r'].Value/10.0
            th = [double]$m.Groups['th'].Value/10.0; sat = [int]$m.Groups['sat'].Value
            pk = [double]$m.Groups['pk'].Value/10.0 })
    }
    $c = $reBALL.Match($ln)
    if ($c.Success -and [int]$c.Groups['id'].Value -ne -1) {
        $st = $c.Groups['st'].Value
        if (-not $seenSt.ContainsKey($st)) { $seenSt[$st] = 1; $cams.Add([double]$c.Groups['cx'].Value/100.0) }
    }
}

L ""
L "---- firmware scorecard (the graded numbers) ----"
$sc = $null
foreach ($ln in ($tail -split "`n")) {
    $t = $ln.TrimEnd("`r")
    if ($t -match '^\[ball\]') { L ("  " + $t) }
    if ($t -match 'traj ph=(?<ph>\d+)\s+t\*10=(?<tt>\d+)\s+wpOUT\*10=(?<wo>-?\d+)\s+wpBACK\*10=(?<wb>-?\d+)') {
        $sc = @{ ph = [int]$Matches['ph']; t = [double]$Matches['tt']/10.0
                 wpOut = [double]$Matches['wo']/10.0; wpBack = [double]$Matches['wb']/10.0 }
    }
}
if ($null -eq $sc) {
    L ""
    L "no trajectory line came back, so the firmware has no result to report."
    L "  print_ball() only prints it while traj_phase > 0, and stop_all() (z) resets it - so this means"
    L "  either the trajectory never started, or something sent z first. Check the TRAJ start line above."
    Finish "INCONCLUSIVE - no firmware scorecard" 2
}

L ""
L ("  phase reached : {0}  (4 = finished)" -f $sc.ph)
L ("  total time    : {0:N1} s   (requirement 3 limit 5.0 s)" -f $sc.t)
L ("  wpOUT  error  : {0,6:N1} mm   <- at the +50 mm waypoint" -f $sc.wpOut)
L ("  wpBACK error  : {0,6:N1} mm   <- at the -50 mm waypoint" -f $sc.wpBack)

if ($rows.Count -gt 20) {
    $xMin = ($rows | ForEach-Object { $_.x } | Measure-Object -Minimum).Minimum
    $xMax = ($rows | ForEach-Object { $_.x } | Measure-Object -Maximum).Maximum
    $errs = @($rows | ForEach-Object { [Math]::Abs($_.x - $_.r) })
    $satN = @($rows | Where-Object { $_.sat -ne 0 }).Count
    L ""
    L "---- supporting detail (PC side, for tuning only - not graded) ----"
    L ("  x_est travelled: {0:N1} .. {1:N1} mm   (observer)" -f $xMin, $xMax)
    if ($cams.Count -gt 10) {
        $cMin = ($cams | Measure-Object -Minimum).Minimum
        $cMax = ($cams | Measure-Object -Maximum).Maximum
        L ("  camera raw     : {0:N1} .. {1:N1} mm   ({2} frames)" -f $cMin, $cMax, $cams.Count)
        # The tube is +-125 mm and the camera is calibrated to +-120, so anything past that is not a ball.
        if ([Math]::Abs($xMin) -gt 130.0 -or [Math]::Abs($xMax) -gt 130.0) {
            L  "  🔴 x_est left the physical tube while the camera did not => THE OBSERVER DIVERGED."
            L  "     Cause is structural, not noise: use_model=1 predicts from the COMMANDED angle, and while"
            L  "     the servo layer clips that command the model is integrating an angle that never happened."
            L  "     Fix the clamp (CFG_BALL_THETA_MAX <= the weaker side's authority) before reading any"
            L  "     tuning result from this run - the numbers below describe the observer, not the ball."
        }
    }
    L ("  path error     : mean {0:N1} mm, worst {1:N1} mm" -f (($errs | Measure-Object -Average).Average), (($errs | Measure-Object -Maximum).Maximum))
    L ("  ball-layer sat : {0:N0}% of samples" -f (100.0*$satN/$rows.Count))
    $peak = ($rows | ForEach-Object { $_.pk } | Measure-Object -Maximum).Maximum
    L ("  firmware PEAK  : {0:N1} mm  (its own worst-frame tracker)" -f $peak)
}

$worst = [Math]::Max([Math]::Abs($sc.wpOut), [Math]::Abs($sc.wpBack))
L ""
if ($sc.ph -lt 4) {
    L ("  the trajectory stopped at phase {0} instead of finishing." -f $sc.ph)
    Finish ("FAIL - trajectory did not complete (phase {0})" -f $sc.ph) 1
}
if ($worst -le $GateMm -and $sc.t -le 5.0) {
    L  "  Both waypoints inside the gate and inside the time limit."
    L  "  => LOCK IT IN: write these into config.h 7.12 (CFG_BALL_KP/KD/ALPHA, CFG_BALL_THETA_MAX) and"
    L  "     reflash, because M/F/p/d are RAM only and die at power-off."
    Finish ("PASS - worst waypoint {0:N1} mm in {1:N1} s" -f $worst, $sc.t) 0
}
L ("  worst waypoint {0:N1} mm against a {1:N0} mm gate." -f $worst, $GateMm)
Finish ("FAIL - worst waypoint {0:N1} mm (gate {1:N0}), time {2:N1} s" -f $worst, $GateMm, $sc.t) 1
