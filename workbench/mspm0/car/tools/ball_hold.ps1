# ball_hold.ps1 - first CLOSED-LOOP measurement: park the ball at a setpoint with the firmware's own
# 50 Hz m12 loop and report what the steady-state error actually is.
#
# WHY THIS EXISTS
#   Everything measured so far came from OPEN-LOOP sweeps whose repositioning was done by a PC-side herd
#   running at ~6.7 Hz (one U command = 25 ms/char x ~6 chars = 150 ms). On 2026-07-31 that herd never
#   settled, so every sweep capture began with the ball doing 45..100 mm/s and 6 of 8 points died on the
#   guard. Reading that as "the servo cannot position the ball accurately" would be wrong: the scored
#   loop runs at 50 Hz inside the firmware with no serial in the path. Those are different regimes by an
#   order of magnitude.
#   So measure the thing we actually care about, with the loop we actually ship, and put a number on it.
#
#   It doubles as the repositioning primitive the sweep needs: -Park leaves the ball held at 0 mm and
#   returns, which is strictly better than the PC herd it replaces.
#
# WHAT IT REPORTS
#   x_est mean / std / peak |error| over the window, plus how much of the time the beam output was
#   saturated. Saturation matters: with the measured authority the PD term clips for large errors, so a
#   high sat fraction means the number you are reading is an authority limit, not a tuning limit.
#
# WHY std AND peak, not just one
#   Q37 scores the WORST frame, so peak is the graded quantity. But std is what tells you whether the
#   residual is broadband noise (raise gains / add feedforward) or a slow limit cycle from dead-zone and
#   backlash (a completely different fix). Reporting only peak hides which of the two you have.
#
# SAFETY
#   m12 has its own silence timeout and a hard cap in firmware, so the beam flattens by itself if this
#   script dies. The finally block also sends z.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_hold.ps1 -Port COM30 -Sec 15
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_hold.ps1 -Port COM30 -Park
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_hold.ps1 -Port COM30 -Sec 20 -Kp 6 -Kd 8
#
# EXIT CODES: 0 = held, 1 = did not hold, 2 = could not measure
# ASCII only in the code.
param(
    [string]$Port    = "COM30",
    [int]$Baud       = 115200,
    [double]$Setpoint = 0.0,     # mm
    # 9 s, not 15: settle + window must fit inside the 15 s MODE_BALL hard cap (see the note where it is
    # used). At 40 Hz telemetry a 9 s window is ~360 samples, plenty for std/peak.
    [int]$Sec        = 9,
    [double]$Kp      = 0,        # 0 = leave the firmware's config.h value alone
    [double]$Kd      = 0,
    [int]$ThetaMaxDeg = 0,       # 0 = leave alone (M command)
    [int]$Alpha100   = 0,        # 0 = leave alone (F command)
    [int]$FfMask     = -1,       # -1 = leave alone; 0..3 = i<mask> (bit0 a_x, bit1 pitch)
    # Servo geometry, mirrored from config.h. Needed because the firmware's own `sat` flag reports only
    # the BALL layer's clamp (CFG_BALL_THETA_MAX), while the binding limit is the SERVO layer's pulse-width
    # clamp - and the ball layer is never told about it. On 2026-07-31 that produced a flatly contradictory
    # reading: `beam cmd max 5.80 deg` alongside `saturated 0%`, when the real authority is -1.67/+3.10 deg.
    # Believing `sat` there would have sent tuning off in exactly the wrong direction.
    [int]$CenterUs   = 1086,
    [double]$UsPerDeg = 75.4,
    [int]$MinUs      = 960,
    [int]$MaxUs      = 1320,
    [double]$SettleSec = 4.0,      # batch diagnostics may use 3 s; keep settle+window <= 13 s
    [switch]$Park,               # just get the ball to the setpoint and leave it held, no statistics
    [switch]$Yes,
    [string]$Out     = "_logs\ball\ball_hold_out.txt",
    [string]$Json    = "_logs\ball\ball_hold_result.json",
    [string]$RunId   = "",
    [string]$Label   = ""
)

$ErrorActionPreference = "Continue"
$runSw = [System.Diagnostics.Stopwatch]::StartNew()
if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = "hold-" + (Get-Date -Format "yyyyMMdd-HHmmss") }
$carRoot = Split-Path $PSScriptRoot -Parent
function ResolveResultPath([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return "" }
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path $carRoot $p))
}
$Out = ResolveResultPath $Out
$Json = ResolveResultPath $Json
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$result = [ordered]@{
    run_id = $RunId
    tool = "ball_hold"
    label = $Label
    elapsed_ms = 0
    port = $Port
    parameters = [ordered]@{ setpoint_mm=$Setpoint; capture_s=$Sec; settle_s=$SettleSec; kp=$Kp; kd=$Kd; theta_max_deg=$ThetaMaxDeg; alpha100=$Alpha100; ff_mask=$FfMask }
    health = [ordered]@{ first_be_ms=$null; samples=0; expected_samples=0; telemetry_hz=0.0 }
    metrics = [ordered]@{}
    verdict = "INCONCLUSIVE"
    reason = "script did not reach Finish"
    next_action = "inspect raw_log and rerun"
    raw_log = $Out
}

$sp = $null
function Finish([string]$verdict, [int]$code) {
    $runSw.Stop()
    $result.elapsed_ms = [int][Math]::Round($runSw.Elapsed.TotalMilliseconds)
    $result.verdict = $(if ($code -eq 0) { "PASS" } elseif ($code -eq 1) { "FAIL" } else { "INCONCLUSIVE" })
    $result.reason = $verdict
    $result.next_action = $(if ($code -eq 0) { "compare this JSON with the adjacent single-variable run" } else { "follow reason, fix the failed gate, then rerun" })
    L ""; L ("elapsed: {0:N1} s" -f $runSw.Elapsed.TotalSeconds); L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        if ($Json) {
            $jd = Split-Path $Json -Parent
            if ($jd -and -not (Test-Path $jd)) { New-Item -ItemType Directory -Path $jd -Force | Out-Null }
            $result | ConvertTo-Json -Depth 7 | Set-Content $Json -Encoding UTF8
            Write-Host ("RESULT_JSON: {0}" -f $Json)
        }
        Write-Host "(log -> $Out)"
    } catch {}
    if ($code -ne 0 -and $sp -and $sp.IsOpen) {
        try { foreach ($ch in "z`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } } catch {}
    }
    if ($sp -and $sp.IsOpen) { try { $sp.Close(); $sp.Dispose() } catch {} }
    exit $code
}

L ("================ ball_hold  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
L ("port {0}   setpoint {1:N1} mm   window {2}s" -f $Port, $Setpoint, $Sec)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { L ("OPEN_FAIL ({0}): {1}" -f $Port, $_.Exception.Message); Finish "INCONCLUSIVE - serial port could not be opened" 2 }

function Send([string]$c) { foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

# BE:<x_est*10>,<v_est*10>,<x_ref*10>,<th_cmd*10>,<sat>,<peak*10>  - only emitted in m12.
$reBE = [regex]'BE:(?<x>-?\d+),(?<v>-?\d+),(?<r>-?\d+),(?<th>-?\d+),(?<sat>\d+),(?<pk>-?\d+)'
$samples = New-Object System.Collections.Generic.List[object]
$rx = ''
function Drain() {
    $t = ""
    try { $t = $sp.ReadExisting() } catch {}
    if ($t) { $script:rx += $t }
    while ($script:rx.Contains("`n")) {
        $i = $script:rx.IndexOf("`n")
        $ln = $script:rx.Substring(0, $i)
        $script:rx = $script:rx.Substring($i + 1)
        $m = $reBE.Match($ln)
        if ($m.Success) {
            $samples.Add([pscustomobject]@{
                x   = [double]$m.Groups['x'].Value / 10.0
                v   = [double]$m.Groups['v'].Value / 10.0
                r   = [double]$m.Groups['r'].Value / 10.0
                th  = [double]$m.Groups['th'].Value / 10.0
                sat = [int]$m.Groups['sat'].Value
                pk  = [double]$m.Groups['pk'].Value / 10.0 })
        }
    }
}
function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 15 } }

try {
    try { $sp.DiscardInBuffer() } catch {}
    Send "z";   Start-Sleep -Milliseconds 300
    Send "l1";  Start-Sleep -Milliseconds 250
    Send "f25"; Start-Sleep -Milliseconds 250

    $modeSw = [System.Diagnostics.Stopwatch]::StartNew()
    Send "m12"; Start-Sleep -Milliseconds 300
    # EVERY ball knob (t / p / d / i / M / F) is dispatched by ball_cmd, which returns 0 unless
    # g_mode == MODE_BALL. Sent before m12 they fall through to the global switch and do nothing useful -
    # `M2` sent in IDLE simply vanished, which is why run 2 produced no data at all. So: m12 first, knobs
    # after. All of them are RAM-only; anything that meets spec gets pasted into config.h afterwards.
    if ($ThetaMaxDeg -gt 0) { Send ("M" + $ThetaMaxDeg);  Start-Sleep -Milliseconds 200 }
    if ($Alpha100 -gt 0)    { Send ("F" + $Alpha100);     Start-Sleep -Milliseconds 200 }
    if ($Kp -gt 0)          { Send ("p" + [int]($Kp * 1000)); Start-Sleep -Milliseconds 200 }
    if ($Kd -gt 0)          { Send ("d" + [int]($Kd * 1000)); Start-Sleep -Milliseconds 200 }
    if ($FfMask -ge 0)      { Send ("i" + $FfMask);        Start-Sleep -Milliseconds 200 }

    # Health gate before paying for settle+capture. Flush pre-command bytes, arm the parser, send the
    # setpoint, and require a fresh BE line within 1 s. The old script waited the whole 13 s before it
    # admitted BE was absent; this turns the same failure into a ~3 s result.
    try { $sp.DiscardInBuffer() } catch {}
    $script:rx = ''
    $samples.Clear()
    $beSw = [System.Diagnostics.Stopwatch]::StartNew()
    Send ("t" + [int]$Setpoint)
    while ($beSw.Elapsed.TotalSeconds -lt 1.0 -and $samples.Count -eq 0) { Drain; Start-Sleep -Milliseconds 10 }
    if ($samples.Count -eq 0) {
        Finish "INCONCLUSIVE - no BE sample within 1 s of entering m12; mode/telemetry/firmware gate failed early" 2
    }
    $result.health.first_be_ms = [int][Math]::Round($beSw.Elapsed.TotalMilliseconds)
    L ("health gate  : first BE in {0} ms" -f $result.health.first_be_ms)

    # Give the loop time to pull the ball in before measuring. Statistics taken during the approach would
    # describe a transient, not the steady state.
    # Compute the window from the ACTUAL age of m12, not from nominal sleeps: every online knob consumes
    # part of the un-bypassable 15 s hard cap. Keep a full 1 s margin for the final sample and stop command.
    L ""
    L "settling (not measured) ..."
    $maxWindow = [int][Math]::Floor(14.0 - $modeSw.Elapsed.TotalSeconds - $SettleSec - 0.25)
    if ($maxWindow -lt 2) { Finish "INCONCLUSIVE - setup left less than 2 s inside the m12 hard cap" 2 }
    if ($Sec -gt $maxWindow) {
        L ("  NOTE: actual m12 age leaves {0}s for capture; clamping requested {1}s window." -f $maxWindow, $Sec)
        $Sec = $maxWindow
        $result.parameters.capture_s = $Sec
    }
    Wait $SettleSec
    $samples.Clear()
    Send ("t" + [int]$Setpoint); Start-Sleep -Milliseconds 200   # clear peak so it reflects the window only

    if ($Park) {
        L "-Park: ball is being held at the setpoint; leaving the loop running."
        Finish "PASS - parked (m12 still holding; send z to release)" 0
    }

    L ("measuring {0}s ..." -f $Sec)
    $samples.Clear()
    Wait $Sec
    Drain
} finally {
    try { Send "z" } catch {}
}

$n = $samples.Count
$expect = $Sec * 40.0
$result.health.samples = $n
$result.health.expected_samples = [int][Math]::Round($expect)
$result.health.telemetry_hz = if ($Sec -gt 0) { [Math]::Round($n / [double]$Sec, 2) } else { 0.0 }
if ($n -lt 20) {
    L ""
    L ("only {0} BE: samples - m12 is not emitting them." -f $n)
    L "  BE: is only printed in m12, and every ball knob needs m12 ACTIVE first. Check in this order:"
    L "  the mode never engaged (send `?` and look at st=), a knob was sent before m12 and vanished,"
    L "  telemetry is muted to the wireless sink (l3/l1), or the firmware predates the BE: field."
    Finish "INCONCLUSIVE - not enough closed-loop samples" 2
}
# Sample-rate guard: telemetry runs at 25 ms, so a clean window yields ~40 samples/s. Substantially fewer
# means the session was cut short (hard cap) and part of the window had NO control at all - statistics
# over that are meaningless, and silently reporting them is how run 1 produced a believable-looking lie.
if ($n -lt (0.75 * $expect)) {
    L ""
    L ("got {0} samples where a full {1}s window should give about {2:N0} ({3:N1} Hz vs 40 Hz)." -f $n, $Sec, $expect, ($n/[double]$Sec))
    L  "  The m12 session ended early - almost certainly the 15 s MODE_BALL hard cap, measured from mode"
    L  "  entry and not bypassable. During the missing part the beam was flat and the ball rolled free, so"
    L  "  any error statistic computed over it would be nonsense. Shorten -Sec and re-run."
    Finish "INCONCLUSIVE - m12 ended before the window did; statistics rejected" 2
}

$errs = @($samples | ForEach-Object { $_.x - $_.r })
$abs  = @($errs | ForEach-Object { [Math]::Abs($_) })
$mean = ($errs | Measure-Object -Average).Average
$std  = if ($n -gt 1) { [Math]::Sqrt((($errs | ForEach-Object { ($_ - $mean) * ($_ - $mean) }) | Measure-Object -Sum).Sum / ($n - 1)) } else { 0 }
$peak = ($abs | Measure-Object -Maximum).Maximum
$satN = @($samples | Where-Object { $_.sat -ne 0 }).Count
$thAbs = @($samples | ForEach-Object { [Math]::Abs($_.th) })
$thMax = ($thAbs | Measure-Object -Maximum).Maximum
$thRms = [Math]::Sqrt((($thAbs | ForEach-Object { $_ * $_ }) | Measure-Object -Sum).Sum / $n)

L ""
L "---- closed loop, held at setpoint ----"
L ("  samples      : {0} over {1}s  ({2:N1} Hz of telemetry)" -f $n, $Sec, ($n / [double]$Sec))
L ("  error mean   : {0,7:N2} mm   <- a non-zero mean is a CENTRE error, not noise" -f $mean)
L ("  error std    : {0,7:N2} mm" -f $std)
L ("  error PEAK   : {0,7:N2} mm   <- this is the graded number (Q37 scores the worst frame)" -f $peak)
L ("  beam cmd     : rms {0:N2} deg, max {1:N2} deg" -f $thRms, $thMax)
L ("  ball-layer sat flag : {0} of {1} ({2:N0}%)   <- CFG_BALL_THETA_MAX only; NOT the real limit" -f $satN, $n, (100.0 * $satN / $n))

# The limit that actually bites: the pulse-width clamp, computed here because the firmware cannot see it.
$degUp = ($MaxUs - $CenterUs) / $UsPerDeg
$degDn = ($CenterUs - $MinUs) / $UsPerDeg
$clip = @($samples | Where-Object { $_.th -gt $degUp -or $_.th -lt (-1.0 * $degDn) }).Count
$result.metrics = [ordered]@{
    error_mean_mm = [Math]::Round($mean, 4)
    error_std_mm = [Math]::Round($std, 4)
    error_peak_mm = [Math]::Round($peak, 4)
    beam_rms_deg = [Math]::Round($thRms, 4)
    beam_max_deg = [Math]::Round($thMax, 4)
    clipped_samples = $clip
    clipped_pct = [Math]::Round(100.0 * $clip / $n, 2)
}
L ("  REAL authority      : -{0:N2} .. +{1:N2} deg  (from {2}..{3} us around centre {4})" -f $degDn, $degUp, $MinUs, $MaxUs, $CenterUs)
L ("  actually CLIPPED    : {0} of {1} samples ({2:N0}%)   <- this is the number that matters" -f $clip, $n, (100.0 * $clip / $n))

L ""
L "how to read this:"
if ($clip -gt (0.1 * $n)) {
    L ("  * the beam command was outside the achievable range {0:N0}% of the time." -f (100.0*$clip/$n))
    L  "    Two consequences, and the second one is the nasty one:"
    L  "      1. the loop simply has less force than it asks for;"
    L  "      2. the observer runs with use_model=1, i.e. it predicts the ball from the COMMANDED angle."
    L  "         While clipped, the applied angle is smaller than commanded, so the prediction is wrong and"
    L  "         v_est drifts - and the D term is then damping against a velocity that never happened."
    L  "    => Do NOT tune gains against this. First make the ball layer's clamp match reality (M<deg>, so"
    L  "       the ball layer clips and reports it honestly), or lower kd so the demand stays in range."
}
if ([Math]::Abs($mean) -gt 1.5) {
    L ("  * mean {0:N2} mm is well off zero => the LEVEL PULSE is off, not the gains. Nudge CFG_SERVO_CENTER_US" -f $mean)
    L  "    (about 1 us per 0.1 mm of steady offset at these gains) or re-run the sweep to re-fit the zero."
} else {
    L ("  * mean {0:N2} mm is essentially zero => the level pulse is right." -f $mean)
}
if ($clip -le (0.1 * $n)) {
    L  "  * the command stayed inside the achievable range => tuning is the productive lever here."
}
if ([Math]::Abs($degUp - $degDn) / [Math]::Max([Math]::Min($degUp,$degDn), 0.01) -gt 0.25) {
    L ("  * authority is asymmetric ({0:N2} vs {1:N2} deg). Under saturation that alone biases the mean:" -f $degDn, $degUp)
    L  "    the loop can push harder one way than the other, so it settles off-centre even with a perfect"
    L  "    level pulse. Re-centring the horn removes this, and it is the cheapest fix available."
}
if ($std -gt 0.01 -and ($peak / $std) -gt 4.0) {
    L ("  * peak/std = {0:N1} (high) => the residual is dominated by OCCASIONAL excursions, not by noise." -f ($peak/$std))
    L  "    That pattern is dead-zone / backlash hunting or an external disturbance (car body pitch),"
    L  "    neither of which responds to more gain. Feedforward is the lever - try -FfMask 3 vs 0."
} else {
    L ("  * peak/std = {0:N1} (low) => broadband residual, consistent with sensor noise and gain limits." -f ($peak/[Math]::Max($std,0.01)))
}
L ""
L "  Anything that meets spec here lives in RAM only - paste it into config.h and reflash."

if ($peak -le 10.0) { Finish ("PASS - peak error {0:N2} mm (within the +-10 mm budget)" -f $peak) 0 }
Finish ("FAIL - peak error {0:N2} mm exceeds the +-10 mm budget" -f $peak) 1
