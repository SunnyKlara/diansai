# ball_break.ps1 - measure the ball's BREAKAWAY (static friction) angle at its current position, in
# both directions, as a number. Open loop by design.
#
# WHY THIS TOOL EXISTS (2026-07-31)
#   config.h records breakaway as 0.53..1.59 deg, and every gain choice on the ball loop was made
#   against that figure. Then a closed-loop run showed the ball sitting bit-stable at -15.5 mm while
#   the command swept up to 2.39 deg in the direction that would free it. An open-loop probe then
#   freed it instantly at 2.81 deg (U960). So the real breakaway at that spot was 2.4..2.8 deg -
#   ABOVE the 1.96 deg that the mechanism can produce in the other direction. A single wrong number
#   here invalidates the whole tuning effort, so it needs to be measured, not inherited.
#
#   That matters beyond tuning: 2.4 deg of breakaway for a 1 cm steel ball in a 25 cm tube is not
#   physics, it is a dirty or damaged tube. This script is the before/after meter for cleaning it.
#
# METHOD
#   Park the beam at the centre pulse, then step the pulse away from centre in small increments,
#   holding each step long enough to see motion. The first step that produces motion IS the breakaway
#   pulse; convert with CFG_SERVO_US_PER_DEG. Repeat on the other side. Between the two sides the ball
#   is left wherever it stopped - breakaway is position dependent (3x spread along the tube is on
#   record), so the report always states where the ball was.
#
# WHY OPEN LOOP: under closed loop the command never holds still, so "did not move" conflates
#   stiction, backlash and a too-small average command. A held pulse separates them.
#
# WHY MOTION IS JUDGED ON A THRESHOLD, not on any change: the reading carries about +-0.2 mm of
#   quantisation jitter, so "x changed" is true even for a locked ball.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [int]$Center = 1172,        # CFG_SERVO_CENTER_US (measured 2026-07-31; 1086 was wrong by 86 us)
    [int]$MinUs = 960,          # CFG_SERVO_MIN_US - mechanical stop minus 20 us
    [int]$MaxUs = 1320,         # CFG_SERVO_MAX_US
    [int]$Step = 15,            # pulse increment per stair (15 us = 0.20 deg at 75.4 us/deg)
    [double]$HoldS = 1.2,       # dwell per stair; static friction breaks instantly once exceeded
    [double]$MoveMm = 3.0,      # displacement that counts as "it moved"
    [double]$UsPerDeg = 75.4,   # CFG_SERVO_US_PER_DEG (Sweep-identified, unaffected by the centre fix)
    [double]$KMmS2PerUs = 1.622,# |K_total| mm/s^2 per us
    [string]$Out = "_logs\ball_break.txt"
)
$root = Split-Path -Parent $PSScriptRoot
$rep = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
# NOTE: this used to be called $dir, which collided with the Stair loop variable at script scope.
$outDir = Split-Path -Parent $rep
if ($outDir -and -not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir -Force | Out-Null }
# NOT named $out: PowerShell variable names are case-insensitive, so $out IS the [string]$Out parameter
# and every .Add() call fails with "[System.String] does not contain a method named 'Add'".
$rows = New-Object System.Collections.ArrayList
function L([string]$s) { [void]$rows.Add($s); Write-Output $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL: " + $_.Exception.Message); [IO.File]::WriteAllLines($rep, $rows); exit 3 }
$rx = ''
# one char at a time: the MCU RX FIFO is 4 bytes deep and swallows longer bursts silently
function Tx([string]$s) { foreach ($c in ($s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Grab([int]$ms) {
    $t = Get-Date; $script:rx = ''
    while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $script:rx += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 40 }
    $v = @()
    foreach ($ln in ($script:rx -split "`n")) {
        # BALL:<cx*100>,<servo_us>,<age_ms>,<stamp_ms>,<id>   id=-1 means the camera sees no ball
        if ($ln -match 'BALL:(-?\d+),(-?\d+),(-?\d+),(\d+),(-?\d+)') {
            if ([int]$Matches[5] -ge 0) { $v += [double]$Matches[1] / 100.0 }
        }
    }
    return , @($v)
}
function Park([int]$us, [double]$s) { Tx ("U" + $us); [void](Grab ([int]($s * 1000))) }

function Stair {
    # named parameters on purpose: Stair (1) "text" binds both args into ONE array and fails with
    # "Object[] -> Int32", which looked like a logic bug and cost a run.
    $dir = $script:sDir
    $name = $script:sName
    # dir = -1 walks the pulse below centre (pushes the ball toward +x), +1 walks above it
    $limit = if ($dir -lt 0) { $MinUs } else { $MaxUs }
    L ""
    L ("---- " + $name + " : stepping " + $Step + "us at a time from " + $Center + " toward " + $limit + " ----")
    Park $Center 1.5
    $v = Grab 800
    if ($v.Count -lt 3) { L "  NO VISION DATA - abort this side (check V / lighting)"; return $null }
    $x0 = ($v | Measure-Object -Average).Average
    L ("  start x = {0:N1}mm (beam parked at centre, ball confirmed still)" -f $x0)
    $us = $Center
    while ($true) {
        $us = $us + $dir * $Step
        if (($dir -lt 0 -and $us -lt $limit) -or ($dir -gt 0 -and $us -gt $limit)) {
            L ("  reached the mechanical limit " + $limit + "us without breaking the ball loose")
            $deg = [math]::Abs($limit - $Center) / $UsPerDeg
            L ("  => breakaway > {0:N2} deg  (> {1:N0} mm/s^2)  ** NOT ENOUGH AUTHORITY ON THIS SIDE **" -f $deg, ([math]::Abs($limit - $Center) * $KMmS2PerUs))
            return $null
        }
        Tx ("U" + $us)
        $v = Grab ([int]($HoldS * 1000))
        if ($v.Count -lt 3) { L ("  us=" + $us + " : no vision data, skipping"); continue }
        $xn = $v[-1]
        $d = $xn - $x0
        $ad = [math]::Abs($d)
        L ("  us={0,5}  dev={1,4}us = {2,5:N2}deg   x={3,7:N1}mm  moved={4,6:N1}mm {5}" -f `
            $us, [math]::Abs($us - $Center), ([math]::Abs($us - $Center) / $UsPerDeg), $xn, $d, $(if ($ad -ge $MoveMm) { "<== BROKE LOOSE" } else { "" }))
        if ($ad -ge $MoveMm) {
            $dev = [math]::Abs($us - $Center)
            L ("  => breakaway = {0:N2} deg  ({1:N0} us, {2:N0} mm/s^2) at x0={3:N1}mm" -f ($dev / $UsPerDeg), $dev, ($dev * $KMmS2PerUs), $x0)
            return [pscustomobject]@{ deg = $dev / $UsPerDeg; us = $dev; x0 = $x0 }
        }
    }
}

L ("==== ball_break  port=$Port  centre=$Center  " + (Get-Date -Format "HH:mm:ss") + " ====")
L "Reference: a 1 cm steel ball in a clean 25 cm tube should break loose well under 0.5 deg."
L ("Authority available: {0:N2} deg toward +x ({1}->{2}) and {3:N2} deg toward -x ({1}->{4})" -f `
    (($Center - $MinUs) / $UsPerDeg), $Center, $MinUs, (($MaxUs - $Center) / $UsPerDeg), $MaxUs)
Tx "z"; [void](Grab 400)
Tx "f50"; [void](Grab 300)

$script:sDir = -1; $script:sName = "push ball toward +x (pulse below centre)"; $plus = Stair
$script:sDir = 1; $script:sName = "push ball toward -x (pulse above centre)"; $minus = Stair

Tx "z"; [void](Grab 300)
$sp.Close()
L ""
L "---- summary ----"
foreach ($p in @(@{n = "toward +x"; r = $plus }, @{n = "toward -x"; r = $minus })) {
    if ($p.r -eq $null) { L ("  {0} : NOT BROKEN within the mechanical limit" -f $p.n) }
    else { L ("  {0} : {1,5:N2} deg  ({2,3:N0} us)  measured with the ball at {3:N1}mm" -f $p.n, $p.r.deg, $p.r.us, $p.r.x0) }
}
L ""
L "How to read this: breakaway must be comfortably BELOW the authority printed at the top, on BOTH"
L "sides, or the loop cannot recover the ball from a stop. Cleaning the tube and the ball is the"
L "cheapest way to move this number - re-run and compare."
[IO.File]::WriteAllLines($rep, $rows)
if ($plus -and $minus -and $plus.deg -lt 1.0 -and $minus.deg -lt 1.0) { exit 0 } else { exit 1 }
