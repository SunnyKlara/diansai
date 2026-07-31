# ball_run.ps1 - drive the car with the ball on board and score the whole lap from the TELEMETRY
# WHY THIS EXISTS (2026-07-31): the first round of ball tuning was done by polling 'V' at ~300 ms,
# 12 samples over 4 s, while the ball's oscillation period is ~3.6 s. That is severe undersampling,
# and the metric used (peak-to-peak) is extremely sensitive to WHERE the samples land -> the
# apparent difference between configs was largely sampling luck. Three more flaws: the ball's
# start position differed every run (-13/+31/+35/+57/+93 mm), the car started from a different
# place on the track each time (straight vs curve disturbances are not comparable), and 4 s does
# not cover a lap. This script fixes all four:
#
#   1) PASSIVE read of the firmware telemetry stream at 'f<ms>' (default 50 ms = 20 Hz) instead of
#      polling 'V'  -> ~700 samples over a lap instead of 12. Ball position comes from the
#      'BALL:<x*100>,<ref*100>,...' field of the '[ctl]' line, so no extra round trips.
#   2) GATED START: after 'm12' it waits until |x| < -SettleMm before pressing 'K'. If the ball
#      never settles, it ABORTS without driving -> the start condition is the same every run.
#   3) METRICS THAT MATCH THE SCORING: the task asks for |error| <= 10 mm for the WHOLE run, so the
#      headline number is the FRACTION OF TIME outside +-10 mm, plus RMS. Peak is reported but is
#      only a secondary indicator.
#   4) Writes the raw series to CSV so a run can be re-analysed later instead of re-run.
#
# Environment constraints this script is built around (all measured on 2026-07-31):
#   * a foreground command longer than ~8-10 s gets killed -> run this via the background-process
#     tool and poll the log, or keep -Seconds small. It writes results to a file either way.
#   * child "powershell -File ..." touching a serial port sometimes produces no output at all ->
#     prefer dot-sourcing, or read the output FILE rather than trusting stdout.
#   * ALWAYS stop the background process when done; leftover terminals hold the COM port.
#
# Usage:
#   .\tools\ball_run.ps1 -Tag base                      # 20 Hz, gate at 15 mm, 35 s
#   .\tools\ball_run.ps1 -Tag slowramp -Cruise 45 -Seconds 40
#   .\tools\ball_run.ps1 -Tag holdonly -NoDrive         # stationary reference run (no 'K')
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [string]$Tag = "run",
    [int]$TeleMs = 50,          # firmware telemetry period -> sampling rate
    [double]$SettleMm = 15.0,   # start gate: |x| must get below this before 'K'
    [double]$SettleWaitS = 12.0,
    [int]$Cruise = 0,           # 0 = leave cruise alone; else send t<Cruise>
    # MUST stay inside CFG_RUN_MS_HARDCAP (15 s for the ball mode, not bypassable). When that
    # expires the ball layer drops to IDLE, the servo goes limp, the beam falls and the ball rolls
    # to a stop -- and the camera then repeats the SAME frame, so `valid` stays 1 while the reading
    # is frozen. Measured 2026-07-31: two 20-30 s captures both went dead flat after ~14 s
    # (-40.8 mm and +64.7 mm held to the last decimal for 6 s and 15 s), and those dead tails
    # dominated the statistics. Default 12 s leaves margin; the run is aborted early if the
    # reading goes stale anyway (see the vision-stale detector below).
    [double]$Seconds = 12.0,
    [switch]$NoDrive,
    # arbitrary online knobs to apply before the run, e.g. -PreCmds J60,p3000,d8000
    # (sent after 'm12' so ball-layer commands land; each is echoed into the report so the run
    #  file always states which configuration produced the numbers -- otherwise a CSV is useless
    #  a day later, which is exactly how the first tuning round became unciteable)
    [string[]]$PreCmds = @(),
    # metrics are computed over exactly this many valid samples from the start of capture, so that
    # two runs are comparable even when one of them ends early (see the note at the metrics block)
    [int]$WindowN = 120,
    [string]$OutDir = "_logs\ball"
)

$root = Split-Path -Parent $PSScriptRoot
$dir = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$stamp = Get-Date -Format "HHmmss"
$csv = Join-Path $dir ("{0}_{1}.csv" -f $Tag, $stamp)
$rep = Join-Path $dir ("{0}_{1}.txt" -f $Tag, $stamp)
$out = New-Object System.Collections.ArrayList
function L([string]$s) { [void]$out.Add($s); Write-Output $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL: " + $_.Exception.Message); [IO.File]::WriteAllLines($rep, $out); exit 3 }

$rx = ''
# MUST send one character at a time with a gap. The MCU's UART RX FIFO is only 4 bytes deep and
# the main loop does not drain it fast enough, so a single burst write of a 6-character command
# (`I2000`, `p4000`, `d12000`) is SILENTLY SWALLOWED -- no reply, no rej, the parameter simply
# never changes. Measured 2026-07-31: burst-writing `I2000` left `ki*1000=0` and produced zero
# reply lines, while the same command sent at 25 ms/char set `ki=2000` immediately. 4-character
# commands (`J30`, `m12`) happen to fit the FIFO, which is why the bug looked intermittent.
# This invalidated a whole round of tuning: three "different" configs returned bit-identical
# metrics because none of the writes had actually landed.
# (`tools/uart_send.ps1` has always done this; my collection scripts did not.)
function Tx([string]$s) {
    foreach ($ch in ($s + "`n").ToCharArray()) {
        $script:sp.Write([string]$ch)
        Start-Sleep -Milliseconds 25
    }
}
function Soak([int]$ms) {
    $t = Get-Date
    while (((Get-Date) - $t).TotalMilliseconds -lt $ms) {
        try { $script:rx += $sp.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 40
    }
}
# pull ball x (mm) out of every complete '[ctl]' telemetry line seen so far; returns the list and
# keeps the incomplete tail in $rx
function Drain() {
    $vals = New-Object System.Collections.ArrayList
    $parts = $script:rx -split "`n"
    $script:rx = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $ln = $parts[$i]
        # Firmware field order (car.c, the `| BALL:` block) is
        #   BALL:<cx*100 mm>,<servo_us>,<age_ms>,<stamp_ms>,<id>
        # THE LAST FIELD IS THE TARGET ID, AND -1 MEANS "CAMERA SEES NO BALL".
        # The K230 deliberately keeps sending frames in that case (`$V,-1,0,0,0`) so that "module
        # alive but blind" can be told apart from "module offline" -- and cx is 0 in those frames.
        # This script used to accept any id != 0, so every blind frame entered the statistics as
        # "ball exactly at 0.0 mm", i.e. as a PERFECT sample. Two consequences, both measured
        # 2026-07-31: the start gate passed in 0.4 s while the ball was actually parked off-camera
        # at the far + end, and runs of 126 samples contained bit-exact 0.0 entries that pulled the
        # "% outside +-10 mm" figure DOWN. car.c warns about exactly this trap for the calibration
        # scripts; the collection script never applied it.
        if ($ln -match 'BALL:(-?\d+),(-?\d+),(-?\d+),(\d+),(-?\d+)') {
            [void]$vals.Add([pscustomobject]@{
                x_mm     = [int]$Matches[1] / 100.0
                servo_us = [int]$Matches[2]
                age_ms   = [int]$Matches[3]
                t_ms     = [int]$Matches[4]
                id       = [int]$Matches[5]
                seen     = [int]($(if ([int]$Matches[5] -ge 0) { 1 } else { 0 }))
            })
        }
    }
    return $vals
}

L ("==== ball_run  tag=$Tag  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
Tx "z"; Soak 400
Tx ("f" + $TeleMs); Soak 300
if ($Cruise -gt 0) { Tx ("t" + $Cruise); Soak 200; L ("cruise set: t$Cruise") }
$script:rx = ''

# ---- phase 1: settle the ball (this also recentres it from an end stop, measured to work) ----
Tx "m12"; Soak 500
# `powershell -File ball_run.ps1 -PreCmds a,b,c` hands the script ONE string "a,b,c": arguments
# that cross the -File boundary are literal strings and the comma is NOT an array separator there.
# Measured 2026-07-31: the report printed a single line `pre: J120,p12000,d12000,I0,i1`, the MCU's
# format gate rejected the whole thing (commas), and the run silently used whatever gains were
# already in RAM -> several "different configs" were in fact the SAME config. Splitting here makes
# the dot-sourced form (a real array) and the -File form behave identically.
$pre = @(); foreach ($c in $PreCmds) { $pre += ($c -split ',') }
foreach ($c in $pre) { $c = $c.Trim(); if ($c) { Tx $c; Soak 250; L ("pre: " + $c) } }
# READ BACK, DO NOT ASSUME. The bug above was invisible for a whole tuning round because nothing
# ever checked that the knobs landed. Every run file now states the gains the CHIP reports, so a CSV
# is still interpretable later and a swallowed command shows up immediately as "unchanged gains".
$script:rx = ''
Tx "?"; Soak 700
foreach ($ln in ($script:rx -split "`n")) {
    # `[srv] ... center=` matters as much as the gains: the beam's true horizontal pulse is the
    # single largest disturbance in this rig (a wrong centre shows up as a constant tilt, i.e. a
    # constant ball acceleration), so a run file that does not state the centre it used cannot be
    # compared with any other run.
    if ($ln -match 'kp\*1000=\d+' -or $ln -match 'center=\d+') { L ("readback: " + $ln.Trim()) }
}
$settled = $false; $lastX = 999.0
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $SettleWaitS) {
    Soak 400
    foreach ($s in (Drain)) { if ($s.seen) { $lastX = $s.x_mm } }
    if ([math]::Abs($lastX) -lt $SettleMm) { $settled = $true; break }
}
L ("settle: |x|={0:N1}mm after {1:N1}s -> {2}" -f $lastX, ((Get-Date) - $t0).TotalSeconds, $(if ($settled) { "OK" } else { "FAILED" }))
if (-not $settled) {
    Tx "z"; Soak 300; $sp.Close()
    L "RESULT: ABORT - ball never settled below the gate; not driving (start condition would not be comparable)."
    [IO.File]::WriteAllLines($rep, $out); exit 2
}

# ---- phase 2: start (unless -NoDrive) ----
$startedOn = 0
if (-not $NoDrive) {
    for ($k = 1; $k -le 4; $k++) {
        $before = $script:rx.Length
        Tx "K"; Soak 350; Tx "?"; Soak 500
        if ($script:rx -match 'START -> m11' -or $script:rx -match '\[task\] RUN') { $startedOn = $k; break }
    }
    if ($startedOn -eq 0) {
        Tx "z"; Soak 300; $sp.Close()
        L "RESULT: ABORT - virtual button never started the run after 4 tries."
        [IO.File]::WriteAllLines($rep, $out); exit 2
    }
    if ($Cruise -gt 0) { Tx ("t" + $Cruise); Soak 150 }   # re-assert: entering m11 resets cruise
    L ("started on K#$startedOn")
}
else { L "NoDrive: stationary reference run" }

# ---- phase 3: passive capture ----
$script:rx = ''
$series = New-Object System.Collections.ArrayList
$tc = Get-Date
# vision-stale detector: judged on age_ms, NOT on a repeated position.
# REWRITTEN 2026-07-31 22:1x. Judging "dead data" by a repeated POSITION is wrong, and it got
# actively harmful once the loop started working: with the tuned gains the ball sits bit-exactly still
# (measured: -3.4 mm unchanged for >5 s, which is a PERFECT result) and the old detector threw that away
# as a frozen camera - the run came back "INCONCLUSIVE, 4 samples kept". Raising the sample threshold
# only postpones the same false positive.
# The unambiguous signature of a dead vision link is `age_ms` GROWING: age is (now - frame.stamp_ms), so
# a live camera keeps it at 0..30 ms no matter how still the ball is, while a stalled stream lets it
# climb without bound. That distinguishes "ball not moving" from "no new frames" with no ambiguity.
$staleAgeMs = 400        # a live 24 fps stream is ~40 ms; 400 ms means 10 frames missed in a row
$staleNeed = 20          # and it must persist, so one scheduling hiccup does not end a run
$staleN = 0; $staleAt = -1; $lastAge = 0
while (((Get-Date) - $tc).TotalSeconds -lt $Seconds) {
    Soak 400
    foreach ($s in (Drain)) {
        [void]$series.Add($s)
        if ($s.age_ms -ge $staleAgeMs) { $staleN++; $lastAge = $s.age_ms } else { $staleN = 0 }
    }
    if ($staleN -ge $staleNeed) { $staleAt = [math]::Round(((Get-Date) - $tc).TotalSeconds, 1); break }
}
if ($staleAt -ge 0) {
    L ("VISION STALE: age_ms stayed >={0} for {1} samples (last {2}ms); capture stopped at t={3}s" -f `
        $staleAgeMs, $staleNeed, $lastAge, $staleAt)
    # drop the stale tail so it cannot pollute the metrics
    while ($series.Count -gt 0 -and $series[$series.Count - 1].age_ms -ge $staleAgeMs) { $series.RemoveAt($series.Count - 1) }
    L ("  stale tail dropped; {0} samples kept" -f $series.Count)
}
Tx "z"; Soak 300
$sp.Close()

# ---- phase 4: metrics ----
$valid = @($series | Where-Object { $_.seen })
$blind = $series.Count - $valid.Count
L ("samples: {0} total, {1} with a real target, {2} blind (id=-1) ({3:N1} Hz effective)" -f $series.Count, $valid.Count, $blind, ($series.Count / $Seconds))
# A blind stretch is NOT a good result: either the ball left the camera's +-120 mm calibrated span
# (i.e. it ran off the beam - a failed run) or the vision link dropped. Report it as its own number
# instead of letting it dilute the position statistics.
L ("  blind fraction      : {0,6:N1} %   (id=-1: ball off-camera or link down)" -f (100.0 * $blind / [math]::Max(1, $series.Count)))
if ($valid.Count -lt 20) {
    L "RESULT: INCONCLUSIVE - too few samples with a real target (ball off the beam? camera stale? check V)."
    [IO.File]::WriteAllLines($rep, $out); exit 2
}
$series | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv

# FIXED WINDOW. Metrics are computed over the FIRST $WindowN valid samples only, never over
# "however many samples this run happened to produce". Measured 2026-07-31: two runs of the same
# config reported 0.0% vs 26.4% outside +-10 mm purely because one ended at 6.8 s (the ball got
# locked by friction and the freeze detector truncated it) and the other ran the full 12 s. A
# time-fraction metric is meaningless unless the window is identical across runs.
$xsAll = @($valid | ForEach-Object { $_.x_mm })
if ($xsAll.Count -lt $WindowN) {
    L ("RESULT: INCONCLUSIVE - only {0} valid samples, need {1} for a comparable window." -f $xsAll.Count, $WindowN)
    [IO.File]::WriteAllLines($rep, $out); exit 2
}
$xs = $xsAll[0..($WindowN - 1)]
L ("window: first {0} valid samples ({1:N1} s) of {2} captured" -f $WindowN, ($WindowN / 20.0), $xsAll.Count)
$n = $xs.Count
$mean = ($xs | Measure-Object -Average).Average
$sumsq = 0.0; foreach ($v in $xs) { $sumsq += $v * $v }
$rms = [math]::Sqrt($sumsq / $n)                       # RMS about ZERO (that is what is scored)
$sd = 0.0; foreach ($v in $xs) { $sd += ($v - $mean) * ($v - $mean) }
$sd = [math]::Sqrt($sd / [math]::Max(1, $n - 1))
$pk = ($xs | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum
$out10 = @($xs | Where-Object { [math]::Abs($_) -gt 10.0 }).Count
$out20 = @($xs | Where-Object { [math]::Abs($_) -gt 20.0 }).Count
$staleFrac = $blind / [double]$series.Count

L ""
L "---- metrics (headline first: the task scores |x|<=10mm for the WHOLE run) ----"
L ("  time outside +-10mm : {0,6:N1} %   <== headline" -f (100.0 * $out10 / $n))
L ("  time outside +-20mm : {0,6:N1} %" -f (100.0 * $out20 / $n))
L ("  RMS about 0         : {0,6:N1} mm" -f $rms)
L ("  mean (bias)         : {0,6:N1} mm" -f $mean)
L ("  std about mean      : {0,6:N1} mm" -f $sd)
L ("  peak |x|            : {0,6:N1} mm" -f $pk)
L ("  stale/invalid frac  : {0,6:N1} %" -f (100.0 * $staleFrac))
L ("csv: $csv")
[IO.File]::WriteAllLines($rep, $out)
if ((100.0 * $out10 / $n) -le 5.0) { exit 0 } else { exit 1 }
