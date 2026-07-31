# lap_instrumented.ps1 - fire the start button and stream-record the whole lap, so a mid-run MCU
# reboot can be located in TIME and put next to the electrical state that caused it.
#
# WHY THIS EXISTS (two bugs it fixes):
#  1) line_lap_accept.ps1 read distance from the [task] line's `D=` field. That field is
#     g_lf.n_dig - the count of digital line-sensor reads - NOT millimetres. It grows while the car
#     stands still (measured: 95840 -> 103890 with the car parked), so every "d=NNNmm" it printed
#     was really "N sensor reads". The firmware's arrival gate is in mm and was being compared
#     against reads. The only real odometry on the wire is C:<c1>,<c2> on the [ctl] stream.
#  2) Polling `?` once per 500 ms cannot see a reboot: after the reset the task is back to IDLE and
#     every counter restarts, so the poll just reports a quiet, idle car and the run looks like
#     "never stopped". A reboot is only visible as uptime (the trailing t<ms>) going BACKWARDS.
#
# So this script does not poll for state. It records the stream and reconstructs the run afterwards.
# Never sends `z` at the end: on a successful self-stop the final position IS the measurement.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [double]$MaxS = 44.0,        # MODE_LINE hard cap is 45 s and cannot be bypassed
    [int]$Fms = 100,             # telemetry period; 100 = 10 Hz, light enough to never drown replies
    [double]$CountsPerMm = 5.109,
    [double]$LoopMm = 4245.0,    # must equal config.h TASK_LOOP_MM
    # Commands sent (in order) before the start button. Declared [string], NOT [string[]]:
    # across a -File boundary PowerShell hands the whole "a,b,c" over as ONE string, and a
    # [string[]]/[int[]] parameter then fails to convert. So take one string and split it here.
    [string]$PreCmds = "",
    # Commands sent IMMEDIATELY AFTER the run starts. This is not a nicety - t/p/d/i are
    # MODE-DEPENDENT (loop_index() returns -1 outside the line mode), and the task layer only
    # enters m11 when the button fires. Sent before the start they land on the SPEED loop instead:
    # measured, `t65` in IDLE replied "[ctl] mode=IDLE tgt=65" - it set the speed-loop target, and
    # the line cruise/gains were never touched, so the whole run used the old values silently.
    [string]$LineCmds = "",
    [string]$OutDir = "_logs\track"
)
$gateMm = 0.82 * $LoopMm
$root = Split-Path -Parent $PSScriptRoot
$dOut = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $dOut)) { New-Item -ItemType Directory -Path $dOut -Force | Out-Null }
$stamp = Get-Date -Format "HHmmss"
$csv = Join-Path $dOut ("lap_$stamp.csv")
$rep = Join-Path $dOut ("lap_$stamp.txt")
$lines = New-Object System.Collections.ArrayList
function L { param($s) [void]$lines.Add([string]$s); Write-Output $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL: " + $_.Exception.Message); exit 3 }
function Slow([string]$s) { foreach ($c in ($s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Drain([int]$ms) { $o = ''; $t = Get-Date; while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $o += $script:sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 30 }; return $o }

L ("==== lap_instrumented  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
L ("arrival gate = {0:N0} mm (0.82 x LoopMm {1:N0})" -f $gateMm, $LoopMm)

Slow "z";           [void](Drain 400)
Slow ("f" + $Fms);  [void](Drain 400)
if ($PreCmds.Trim()) {
    foreach ($cmd in ($PreCmds -split ',')) {
        $c = $cmd.Trim(); if (-not $c) { continue }
        Slow $c
        $r = Drain 500
        # echo back whatever the firmware replied so a silently-rejected command is visible
        $e = (($r -replace "`r", '') -split "`n" | Where-Object { $_ -match '\[line\]|\[vseg\]|\[ctl\] mode|\[task\]' } | Select-Object -First 1)
        L ("pre-cmd {0,-6} -> {1}" -f $c, $(if ($e) { $e.Trim() } else { "(no echo)" }))
    }
}
Slow "?"
$pre = Drain 900
foreach ($ln in ($pre -split "`n")) { if ($ln -match '\[task\]') { L ("pre : " + $ln.Trim()) } }

# Press the button until the task is actually in RUN, and verify it.
# WHY a single press is not enough (task.c:151): from DONE/ABORT a press only resets the state
# machine to IDLE - it does NOT start a run. So after any previous run that ended in ABORT (e.g.
# the 40 s timeout) the FIRST press is consumed by the reset and a second one is needed. `z` does
# not clear the task state either. Sending one `K` and assuming it started gives a 42 s recording
# of a car that never moved (measured: every poll frozen at the previous run's values).
$started = $false
for ($k = 1; $k -le 4; $k++) {
    Slow "K"
    $r = Drain 700
    if ($r -match '\[task\] RUN ') { $started = $true; L ("started on K#$k"); break }
    Slow "?"
    $r = Drain 800
    if ($r -match '\[task\] RUN ') { $started = $true; L ("started on K#$k"); break }
    if ($r -match '\[task\] (\w+) t=') { L ("  K#$k -> state=" + $Matches[1]) }
}
if (-not $started) {
    L "RESULT: ABORT - task never entered RUN after 4 presses"
    Slow "z"; [void](Drain 300); $sp.Close(); [IO.File]::WriteAllLines($rep, $lines); exit 2
}
if ($LineCmds.Trim()) {
    foreach ($cmd in ($LineCmds -split ',')) {
        $c = $cmd.Trim(); if (-not $c) { continue }
        Slow $c
        $r = Drain 350
        # in m11 these must be answered by a [line] echo; anything else means the command was
        # dispatched to another layer and the value did NOT reach the line follower
        $e = (($r -replace "`r", '') -split "`n" | Where-Object { $_ -match '\[line\]' } | Select-Object -First 1)
        L ("run-cmd {0,-6} -> {1}" -f $c, $(if ($e) { $e.Trim() } else { "NO [line] ECHO - did not reach the line layer" }))
    }
}
$t0 = Get-Date
$buf = ''
$rows = New-Object System.Collections.ArrayList
$marks = New-Object System.Collections.ArrayList   # [task] snapshots, aligned to distance
$reboot = $null
$prevUp = -1
$nextAsk = (Get-Date).AddMilliseconds(600)
$lastMm = 0.0
while (((Get-Date) - $t0).TotalSeconds -lt $MaxS) {
    try { $buf += $sp.ReadExisting() } catch {}
    Start-Sleep -Milliseconds 40
    # Interleave `?` polls with the stream. WHY: the arrival test is two AND-ed gates
    # (car.c:2344)  n_on >= g_cross_min  AND  |w_lp| <= CFG_LINE_CROSS_W_MAX (12, no runtime
    # setter). Only the [task] line carries n_on / w_lp / xcnt, so without these polls a failed
    # arrival is indistinguishable between "not enough probes on black" and "car was not straight
    # enough at that instant". xcnt and the on= max are cumulative, so a 600 ms poll cannot miss
    # them; w_lp is low-passed and moves slowly, so it is well sampled at this rate too.
    if ((Get-Date) -ge $nextAsk) {
        $nextAsk = (Get-Date).AddMilliseconds(600)
        $script:sp.Write("?`n")
    }
    # note: no need to clear $buf here - the drain loop below consumes every complete line, and
    # [task] lines simply do not match the '| C:' stream pattern, so they are dropped there.
    if ($buf -match '\[task\] (\w+) t=([\d.]+)s run#\d+ fail=([^\s]+)[^\n]*?on=(\d+)/(\d+) xrun=\d+ xcnt=(\d+) wlp=(-?\d+) vseg=(-?\d+)') {
        [void]$marks.Add([pscustomobject]@{
                el = [math]::Round(((Get-Date) - $t0).TotalSeconds, 2); mm = $lastMm
                st = $Matches[1]; fail = $Matches[3]
                on = [int]$Matches[4]; onMax = [int]$Matches[5]; xcnt = [int]$Matches[6]
                wlp = [int]$Matches[7]; vseg = [int]$Matches[8]
            })
    }
    while ($buf -match "`n") {
        $i = $buf.IndexOf("`n")
        $ln = $buf.Substring(0, $i).Trim()
        $buf = $buf.Substring($i + 1)
        if ($ln -notmatch '\| C:(-?\d+),(-?\d+)') { continue }
        $c1 = [int]$Matches[1]; $c2 = [int]$Matches[2]
        $up = if ($ln -match 't(\d+)$') { [int]$Matches[1] } else { -1 }
        $v1 = 0; $v2 = 0; $p1 = 0; $p2 = 0; $i1 = 0; $i2 = 0
        if ($ln -match '\| V:(-?\d+),(-?\d+)') { $v1 = [int]$Matches[1]; $v2 = [int]$Matches[2] }
        if ($ln -match '\| PWM:(-?\d+),(-?\d+)') { $p1 = [int]$Matches[1]; $p2 = [int]$Matches[2] }
        if ($ln -match '\| I:(-?\d+),(-?\d+)') { $i1 = [int]$Matches[1]; $i2 = [int]$Matches[2] }
        [void]$rows.Add([pscustomobject]@{
                el = [math]::Round(((Get-Date) - $t0).TotalSeconds, 2)
                up = $up; c1 = $c1; c2 = $c2; v1 = $v1; v2 = $v2; p1 = $p1; p2 = $p2; i1 = $i1; i2 = $i2
                mm = [math]::Round((($c1 + $c2) / 2.0) / $CountsPerMm, 1)
            })
        $lastMm = [math]::Round((($c1 + $c2) / 2.0) / $CountsPerMm, 1)
        # a reboot is the ONLY way uptime can decrease
        if ($null -eq $reboot -and $prevUp -gt 0 -and $up -gt 0 -and $up -lt $prevUp) { $reboot = $rows.Count - 1 }
        if ($up -gt 0) { $prevUp = $up }
    }
}
Slow "?"
$post = Drain 1000
$sp.Close()

$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv
L ""
L "---- stream ----"
L ("  samples        : {0}   (expect ~{1} at {2} ms)" -f $rows.Count, [int]($MaxS * 1000 / $Fms), $Fms)
if ($rows.Count -lt 2) { L "  TOO FEW SAMPLES - link died or telemetry off"; [IO.File]::WriteAllLines($rep, $lines); exit 3 }
$last = $rows[$rows.Count - 1]
L ("  uptime         : {0:N1}s -> {1:N1}s" -f ($rows[0].up / 1000.0), ($last.up / 1000.0))
$peakP = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.p1), [math]::Abs($_.p2)) } | Measure-Object -Maximum).Maximum
$peakI = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.i1), [math]::Abs($_.i2)) } | Measure-Object -Maximum).Maximum
$peakV = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.v1), [math]::Abs($_.v2)) } | Measure-Object -Maximum).Maximum
L ("  peak |PWM| {0}%  peak |I| {1} mA  peak |rpm| {2}" -f $peakP, $peakI, $peakV)

if ($null -ne $reboot) {
    L ""
    L "  *** MCU REBOOTED MID-RUN *** (uptime went backwards - the only possible cause)"
    $a = [math]::Max(0, $reboot - 6)
    L ("  last {0} samples BEFORE the reset, then the first one after:" -f ($reboot - $a))
    L ("      el      up_ms   mm     rpm        PWM       I(mA)")
    for ($k = $a; $k -le [math]::Min($rows.Count - 1, $reboot + 1); $k++) {
        $r = $rows[$k]
        L ("      {0,6}  {1,6}  {2,6}  {3,4},{4,-4}  {5,3},{6,-3}   {7,4},{8,-4}{9}" -f $r.el, $r.up, $r.mm, $r.v1, $r.v2, $r.p1, $r.p2, $r.i1, $r.i2, $(if ($k -eq $reboot) { "  <== FIRST SAMPLE AFTER RESET" } else { "" }))
    }
    $b4 = $rows[[math]::Max(0, $reboot - 1)]
    L ("  reached {0:N0} mm ({1:P0} of the {2:N0} mm gate) in {3}s before dying" -f $b4.mm, ($b4.mm / $gateMm), $gateMm, $b4.el)
}
else {
    L ("  no reboot: uptime rose monotonically for the whole {0}s" -f $MaxS)
    L ("  distance       : {0:N0} mm   avg speed {1:N0} mm/s" -f $last.mm, $(if ($last.el -gt 0) { $last.mm / $last.el } else { 0 }))
    L ("  gate           : {0}" -f $(if ($last.mm -ge $gateMm) { "PASSED ({0:N0} >= {1:N0})" -f $last.mm, $gateMm } else { "not reached ({0:N0} < {1:N0})" -f $last.mm, $gateMm }))
}
L ""
L "---- arrival gates (car.c:2344 needs BOTH: n_on >= xmin AND |w_lp| <= 12) ----"
if ($marks.Count) {
    $onPeak = ($marks | ForEach-Object { $_.on } | Measure-Object -Maximum).Maximum
    $xEnd = ($marks | ForEach-Object { $_.xcnt } | Measure-Object -Maximum).Maximum
    L ("  polls {0}   n_on peak seen while driving = {1} (threshold {2})   xcnt reached {3}" -f $marks.Count, $onPeak, ($marks[0].onMax), $xEnd)
    $wMin = ($marks | ForEach-Object { $_.wlp } | Measure-Object -Minimum).Minimum
    $wMax = ($marks | ForEach-Object { $_.wlp } | Measure-Object -Maximum).Maximum
    $nOpen = @($marks | Where-Object { [math]::Abs($_.wlp) -le 12 }).Count
    L ("  w_lp: min {0}  max {1}   -> samples with |w_lp|<=12 (gate open): {2} of {3}" -f $wMin, $wMax, $nOpen, $marks.Count)
    L "      mm     on  xcnt  wlp  vseg"
    foreach ($m in $marks) { L ("  {0,7}  {1,3}  {2,4}  {3,4}  {4,4}{5}" -f $m.mm, $m.on, $m.xcnt, $m.wlp, $m.vseg, $(if ($m.on -ge 4) { "   <== on>=4" } else { "" })) }
}
else { L "  no [task] polls captured" }
L ""
foreach ($ln in ($post -split "`n")) { if ($ln -match '\[task\]') { L ("post: " + $ln.Trim()) } }
L ("csv: " + $csv)
[IO.File]::WriteAllLines($rep, $lines)
