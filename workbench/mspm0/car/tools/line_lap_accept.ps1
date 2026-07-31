# line_lap_accept.ps1 - acceptance run for requirement 2: virtual/physical button -> line follow ->
# detect the start/stop line -> auto-stop. Reports the FIRMWARE's own timing (that is what the LCD
# shows and what the judges read), and deliberately leaves the car where it stopped so the parking
# error can be measured by hand.
#
# WHY A SEPARATE SCRIPT: the tuning scripts all end with `z` and treat the run as data collection.
# This one is an acceptance test: it must not disturb the final position, and it must report the three
# things the requirement actually scores - did it stop by itself, how long did it take, where did it stop.
#
# READ THIS BEFORE INTERPRETING THE TIME:
#   Arrival is a DOUBLE gate (task.c): odometry >= TASK_LAP_MIN_MM (5200 mm) AND a start/stop-line
#   crossing. The practice track's loop measures 4245 mm (tools/track_map.ps1, closure error 16 mm),
#   which is BELOW the gate - so the first crossing is correctly rejected and the car stops on the
#   SECOND pass, at about 8490 mm / ~33 s. That is not a requirement-2 time; this run verifies the
#   CHAIN (detect -> gate -> stop), not the speed. On the standard 6141.6 mm map one lap passes the gate.
#
# On-chip firmware must already contain CROSS_TICKS=1 (flashed 2026-07-31, 69400 bytes): with TICKS=2
# the 18 mm-wide line is covered for only 69 ms at 259 mm/s = 1.4 ticks and can never be detected.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [double]$MaxS = 44.0,          # MODE_LINE hard cap is 45 s and cannot be bypassed
    [double]$PollS = 0.5,
    [double]$CountsPerMm = 5.109,
    [double]$LoopMm = 4245.0,      # MUST equal config.h TASK_LOOP_MM. standard map = 6141.6
    [string]$OutDir = "_logs\track"
)
# gates are DERIVED in config.h from TASK_LOOP_MM - mirror the same ratios here so the report
# never contradicts the firmware. (they used to be hardcoded 5200/5800 in both places = two
# copies of one fact, which is exactly what bit us before.)
$gateMm = 0.82 * $LoopMm
$slowMm = 0.94 * $LoopMm
$root = Split-Path -Parent $PSScriptRoot
$d0 = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $d0)) { New-Item -ItemType Directory -Path $d0 -Force | Out-Null }
$rep = Join-Path $d0 ("lap_accept_{0}.txt" -f (Get-Date -Format "HHmmss"))
$rows = New-Object System.Collections.ArrayList
function L { param($s) [void]$rows.Add([string]$s); Write-Output $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL: " + $_.Exception.Message); [IO.File]::WriteAllLines($rep, $rows); exit 3 }
$rx = ''
function Tx { param($s) foreach ($c in ([string]$s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Soak { param($ms) $t = Get-Date; while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $script:rx += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 30 } }
function Ask {
    $script:rx = ''
    Tx "?"; Soak 420
    $o = [pscustomobject]@{ st = $null; tsec = $null; xcnt = $null; on = $null; wlp = $null; vseg = $null; D = $null; lost = $null; fail = $null }
    foreach ($ln in ($script:rx -split "`n")) {
        # [task] RUN t=12.3s run#1 fail=- | line st=.. lostSeg=N on=a/b xrun=N xcnt=N wlp=N vseg=N D=N
        if ($ln -match '\[task\] (\w+) t=([\d.]+)s') { $o.st = $Matches[1]; $o.tsec = [double]$Matches[2] }
        # fail= MUST be read off the [task] line only. a bare 'fail=(\S+)' also matches the
        # [ball] line ("st=IDLE fail=NONE"), which comes later in the `?` dump and therefore wins
        # -> the task's real fail reason was silently overwritten by the ball layer's.
        if ($ln -match '\[task\] \w+ t=[\d.]+s run#\d+ fail=(\S+)') { $o.fail = $Matches[1] }
        if ($ln -match 'lostSeg=(\d+) on=(\d+)/(\d+) xrun=\d+ xcnt=(\d+) wlp=(-?\d+) vseg=(-?\d+) D=(\d+)') {
            $o.lost = [int]$Matches[1]; $o.on = [int]$Matches[2]; $o.xcnt = [int]$Matches[4]
            $o.wlp = [int]$Matches[5]; $o.vseg = [int]$Matches[6]; $o.D = [int]$Matches[7]
        }
    }
    return $o
}

L ("==== line_lap_accept  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
L "requirement 2: button -> clockwise lap -> stop at A, total time <=20s, parking error <=2cm"
L ("gates (derived from LoopMm={0}): arrival needs d>={1:N0}mm, pre-slow starts at {2:N0}mm" -f $LoopMm, $gateMm, $slowMm)
L "     one lap (~$([int]$LoopMm)mm) now PASSES the arrival gate => expect a stop on the FIRST pass."
Tx "z"; Soak 500
Tx "f0"; Soak 300      # telemetry off so the [ctl] stream does not drown the [task] replies
$b = Ask
if ($null -eq $b.D) { L "no [task] reply - link down? (try tools/link_up.ps1)"; $sp.Close(); [IO.File]::WriteAllLines($rep, $rows); exit 3 }
L ("baseline: state={0} D0={1} xcnt0={2}" -f $b.st, $b.D, $b.xcnt)
$D0 = $b.D; $x0 = $b.xcnt

$started = $false
for ($k = 1; $k -le 4; $k++) {
    Tx "K"; Soak 500
    $s = Ask
    if ($s.st -eq 'RUN') { $started = $true; L ("started on K#$k  (virtual button - same code path as the physical one: ti.btn is the OR of both)"); break }
    L ("  K#$k -> state=" + $s.st)
}
if (-not $started) { Tx "z"; Soak 300; $sp.Close(); L "RESULT: ABORT - task never entered RUN"; [IO.File]::WriteAllLines($rep, $rows); exit 2 }

$cross = New-Object System.Collections.ArrayList
$prevX = $x0
$last = $b
$t0 = Get-Date
$endSt = ''
while (((Get-Date) - $t0).TotalSeconds -lt $MaxS) {
    Start-Sleep -Milliseconds ([int]($PollS * 1000))
    $s = Ask
    if ($null -eq $s.D) { continue }
    $last = $s
    $d = $s.D - $D0
    if ($s.xcnt -gt $prevX) {
        [void]$cross.Add([pscustomobject]@{ n = $s.xcnt; d = $d; t = $s.tsec })
        L ("  *** start/stop line detected #{0}  at d={1}mm  fw_t={2}s  (gate needs d>={3:N0}) ***" -f $s.xcnt, $d, $s.tsec, $gateMm)
        $prevX = $s.xcnt
    }
    if ($s.st -eq 'DONE') { $endSt = 'DONE'; break }
    if ($s.st -eq 'ABORT') { $endSt = 'ABORT'; break }
}
# deliberately NOT sending `z`: the car has stopped itself and its final position is the measurement.
$sp.Close()

$dTot = $last.D - $D0
L ""
L "---- result ----"
L ("  end state       : {0}   fail={1}" -f $(if ($endSt) { $endSt } else { "still RUN at " + [int]$MaxS + "s" }), $last.fail)
L ("  firmware time   : {0} s   <- this is what the LCD shows / judges read" -f $last.tsec)
L ("  odometry driven : {0} mm  ({1:N2} laps of this {2:N0}mm track)" -f $dTot, ($dTot / $LoopMm), $LoopMm)
L ("  avg speed       : {0:N0} mm/s" -f $(if ($last.tsec -gt 0) { $dTot / $last.tsec } else { 0 }))
L ("  line crossings  : {0} detected" -f $cross.Count)
foreach ($c in $cross) { L ("      #{0} at d={1}mm t={2}s -> {3}" -f $c.n, $c.d, $c.t, $(if ($c.d -ge $gateMm) { "PASSED the gate => should trigger the stop" } else { "below gate => correctly rejected" })) }
L ("  lostSeg         : {0}   (0 = never lost the line)" -f $last.lost)
L ""
if ($endSt -eq 'DONE') {
    L "AUTO-STOP WORKED. The car has NOT been moved by this script."
    L "NOW MEASURE BY HAND: distance from the marked test position on the car's centre axis to the"
    L "reference line/point. Requirement 2 wants <= 2 cm."
    L "Also glance at the LCD: it should have been counting up during the run and be FROZEN on the"
    L "total now - that is the 'display' half of requirement 2 (never yet confirmed by eye)."
}
else {
    L "NO AUTO-STOP. Diagnose in this order:"
    L "  1) crossings = 0        -> detection failed. Check `on` peak vs CFG_LINE_CROSS_MIN_ON=4, and"
    L "                             whether the line sits near a curve (straight gate |w_lp|<=12)."
    L ("  2) crossings >=1 but all below {0:N0}mm -> gate rejected them correctly. either LoopMm/" -f $gateMm)
    L "                             TASK_LOOP_MM is set too large, or the car needs another pass."
    L "  3) counters went NEGATIVE -> the MCU rebooted (battery). Counters only ever increase."
}
[IO.File]::WriteAllLines($rep, $rows)
