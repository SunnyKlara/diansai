# track_map.ps1 - reconstruct the ACTUAL track geometry by dead reckoning, from telemetry we already send.
#
# WHY THIS EXISTS
#   Every measurement so far was a local number (lap time, lostSeg, wlp at one instant). None of them
#   answer the questions that decide the port to the real map: how long is this loop, how tight are its
#   corners, what fraction is straight, and where does the start/stop line sit relative to a corner.
#   Guessing from lap time went wrong once already: from "38.9 s and 5681 mm without reaching the line"
#   the loop was inferred to be LONGER than 5.68 m, when in fact the car had gone round more than once
#   and the line simply was never detected. A geometric reconstruction cannot make that mistake.
#
# INPUTS - both already in the `[ctl]` telemetry line, nothing new to add to the firmware:
#   C:<c1>,<c2>   cumulative encoder counts per wheel
#   Y:<yaw*10>    fused heading in 0.1 deg (continuous, can exceed +-360)
# MODEL: ds = ((dC1+dC2)/2) / CFG_ENC_COUNTS_PER_MM ; x += ds*cos(yaw) ; y += ds*sin(yaw)
#   Using the WHEEL AVERAGE for distance and the IMU for heading is deliberate: the gyro heading is
#   already calibrated (measured drift ~0.4 deg/min after `k`) and does not care about wheel slip, while
#   differential-odometry heading would fold slip straight into the shape.
#
# WHAT IT REPORTS
#   path length, loop-closure error, bounding box, an ASCII plot of the shape, and a segmentation into
#   straights and arcs with the fitted radius of each arc - which is exactly what the task specifies
#   (AB/CD = 1.5 m straights, BC/DA = semicircles of r = 0.5 m, loop = 6141.6 mm).
#
# NOTE ON `k`: a heading-based reconstruction is only as good as the gyro zero, so the run starts with a
#   gyro-bias calibration. The car MUST be still for those 2 s.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [string]$Tag = "map",
    [double]$MaxS = 42.0,
    [double]$CountsPerMm = 5.109,   # CFG_ENC_COUNTS_PER_MM (real-machine calibrated)
    [switch]$NoCal,                 # skip `k` (use when the gyro was calibrated moments ago)
    [switch]$NoDrive,               # just capture; drive the car by hand / another way
    [int]$Cruise = 0,
    [string]$OutDir = "_logs\track"
)
$root = Split-Path -Parent $PSScriptRoot
$d0 = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $d0)) { New-Item -ItemType Directory -Path $d0 -Force | Out-Null }
$stamp = Get-Date -Format "HHmmss"
$csv = Join-Path $d0 ("{0}_{1}.csv" -f $Tag, $stamp)
$rep = Join-Path $d0 ("{0}_{1}.txt" -f $Tag, $stamp)
$rows = New-Object System.Collections.ArrayList
function L { param($s) [void]$rows.Add([string]$s); Write-Output $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L ("OPEN_FAIL: " + $_.Exception.Message); [IO.File]::WriteAllLines($rep, $rows); exit 3 }
$rx = ''
function Tx { param($s) foreach ($c in ([string]$s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Soak { param($ms) $t = Get-Date; while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $script:rx += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 40 } }
function Drain {
    $v = New-Object System.Collections.ArrayList
    $parts = $script:rx -split "`n"
    $script:rx = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $ln = $parts[$i]
        if ($ln -match 'C:(-?\d+),(-?\d+).*Y:(-?\d+)') {
            [void]$v.Add([pscustomobject]@{ c1 = [int]$Matches[1]; c2 = [int]$Matches[2]; yaw = [int]$Matches[3] / 10.0 })
        }
    }
    return , @($v)
}

L ("==== track_map  tag=$Tag  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
Tx "z"; Soak 400
if (-not $NoCal) {
    L "gyro bias calibration (`k`) - the car must be COMPLETELY STILL for 2 s..."
    Tx "k"; Soak 3000
}
Tx "o"; Soak 400              # zero the yaw so the reconstruction starts pointing along +x
Tx "f50"; Soak 300
if ($Cruise -gt 0) { Tx ("t" + $Cruise); Soak 200 }
$script:rx = ''

if (-not $NoDrive) {
    $started = $false
    for ($k = 1; $k -le 4; $k++) {
        Tx "K"; Soak 600
        if ($script:rx -match 'START|\[task\] RUN') { $started = $true; L ("started on K#$k"); break }
    }
    if (-not $started) { L "WARN: no start confirmation seen; capturing anyway" }
}
else { L "NoDrive: capturing without pressing K (push the car by hand for a geometry-only run)" }

$script:rx = ''
$pts = New-Object System.Collections.ArrayList
$t0 = Get-Date
$stopped = ''
while (((Get-Date) - $t0).TotalSeconds -lt $MaxS) {
    Soak 300
    if ($script:rx -match 'DONE|TIMEOUT|ABORT') { if (-not $stopped) { $stopped = ("{0:N1}s" -f ((Get-Date) - $t0).TotalSeconds) } }
    foreach ($s in (Drain)) { [void]$pts.Add($s) }
    if ($stopped) { break }
}
Tx "z"; Soak 300
$sp.Close()

L ("samples: {0}   stop event: {1}" -f $pts.Count, $(if ($stopped) { $stopped } else { "none (ran the full window)" }))
if ($pts.Count -lt 40) { L "RESULT: INCONCLUSIVE - too few samples."; [IO.File]::WriteAllLines($rep, $rows); exit 2 }

# ---- dead reckoning ----
$x = 0.0; $y = 0.0; $s = 0.0
$path = New-Object System.Collections.ArrayList
$prev = $pts[0]
[void]$path.Add([pscustomobject]@{ s = 0.0; x = 0.0; y = 0.0; yaw = $prev.yaw })
for ($i = 1; $i -lt $pts.Count; $i++) {
    $cur = $pts[$i]
    $dc = (($cur.c1 - $prev.c1) + ($cur.c2 - $prev.c2)) / 2.0
    $ds = $dc / $CountsPerMm
    # a wheel-count jump this large in one 50 ms tick is a dropped/garbled line, not motion
    if ([math]::Abs($ds) -gt 200.0) { $prev = $cur; continue }
    $th = $cur.yaw * [math]::PI / 180.0
    $x += $ds * [math]::Cos($th)
    $y += $ds * [math]::Sin($th)
    $s += [math]::Abs($ds)
    [void]$path.Add([pscustomobject]@{ s = [math]::Round($s, 1); x = [math]::Round($x, 1); y = [math]::Round($y, 1); yaw = $cur.yaw })
    $prev = $cur
}
$path | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv

$xs = @($path | ForEach-Object { $_.x }); $ys = @($path | ForEach-Object { $_.y })
$xmin = ($xs | Measure-Object -Minimum).Minimum; $xmax = ($xs | Measure-Object -Maximum).Maximum
$ymin = ($ys | Measure-Object -Minimum).Minimum; $ymax = ($ys | Measure-Object -Maximum).Maximum
$totYaw = $path[-1].yaw - $path[0].yaw
L ""
L "---- geometry ----"
L ("  path length      : {0:N0} mm" -f $path[-1].s)
L ("  net heading change: {0:N1} deg   ({1:N2} laps' worth of turning)" -f $totYaw, ($totYaw / 360.0))
L ("  bounding box     : {0:N0} x {1:N0} mm" -f ($xmax - $xmin), ($ymax - $ymin))
# loop closure: find the later point that comes back closest to the start
$best = $null
for ($i = [int]($path.Count * 0.35); $i -lt $path.Count; $i++) {
    $d = [math]::Sqrt($path[$i].x * $path[$i].x + $path[$i].y * $path[$i].y)
    if ($null -eq $best -or $d -lt $best.d) { $best = [pscustomobject]@{ d = $d; s = $path[$i].s; i = $i } }
}
if ($best) {
    L ("  loop closes at   : s = {0:N0} mm  (returns within {1:N0} mm of the start)" -f $best.s, $best.d)
    L ("  => measured loop length = {0:N0} mm   (task standard = 6141.6 mm, i.e. {1:N0}%)" -f $best.s, (100.0 * $best.s / 6141.6))
}

# ---- straight / arc segmentation over arc length ----
# radius from the heading rate: R = ds / dtheta. Window over ~150 mm so single-sample noise cannot
# masquerade as a corner.
$win = 150.0
$segs = New-Object System.Collections.ArrayList
$i = 0
while ($i -lt $path.Count - 1) {
    $j = $i
    while ($j -lt $path.Count - 1 -and ($path[$j].s - $path[$i].s) -lt $win) { $j++ }
    if ($j -le $i) { break }
    $dth = $path[$j].yaw - $path[$i].yaw
    $dss = $path[$j].s - $path[$i].s
    $r = if ([math]::Abs($dth) -gt 0.5) { $dss / ([math]::Abs($dth) * [math]::PI / 180.0) } else { [double]::PositiveInfinity }
    [void]$segs.Add([pscustomobject]@{ s = $path[$i].s; len = $dss; dth = $dth; r = $r })
    $i = $j
}
$arcs = @($segs | Where-Object { $_.r -lt 1500.0 })
$strs = @($segs | Where-Object { $_.r -ge 1500.0 })
$arcLen = 0.0; foreach ($a in $arcs) { $arcLen += $a.len }
$strLen = 0.0; foreach ($a in $strs) { $strLen += $a.len }
L ""
L "---- straight vs curve (window {0:N0} mm, 'straight' = fitted R >= 1500 mm) ----" -f $win
L ("  straight : {0,6:N0} mm  ({1,3:N0}%)      task standard: 3000 mm (49%)" -f $strLen, (100.0 * $strLen / [math]::Max(1, $strLen + $arcLen)))
L ("  curved   : {0,6:N0} mm  ({1,3:N0}%)      task standard: 3141.6 mm (51%), all at R=500 mm" -f $arcLen, (100.0 * $arcLen / [math]::Max(1, $strLen + $arcLen)))
if ($arcs.Count -gt 0) {
    $rr = @($arcs | ForEach-Object { $_.r })
    L ("  corner radius   : min {0:N0}  median {1:N0}  mm   (task standard is a constant 500 mm)" -f `
        (($rr | Measure-Object -Minimum).Minimum), (($rr | Sort-Object)[[int]($rr.Count / 2)]))
    $tight = @($rr | Where-Object { $_ -lt 400 }).Count
    L ("  segments tighter than R=400mm : {0} of {1}" -f $tight, $segs.Count)
}

# ---- ASCII plot ----
$W = 64; $H = 26
$grid = @()
for ($r0 = 0; $r0 -lt $H; $r0++) { $grid += , (, ' ' * $W) }
$sx = if (($xmax - $xmin) -gt 1) { ($W - 1) / ($xmax - $xmin) } else { 1.0 }
$sy = if (($ymax - $ymin) -gt 1) { ($H - 1) / ($ymax - $ymin) } else { 1.0 }
$sc = [math]::Min($sx, $sy)
foreach ($p in $path) {
    $cx = [int](($p.x - $xmin) * $sc)
    $cy = [int](($ymax - $p.y) * $sc)
    if ($cx -ge 0 -and $cx -lt $W -and $cy -ge 0 -and $cy -lt $H) { $grid[$cy][$cx] = '#' }
}
$g0x = [int]((0 - $xmin) * $sc); $g0y = [int](($ymax - 0) * $sc)
if ($g0x -ge 0 -and $g0x -lt $W -and $g0y -ge 0 -and $g0y -lt $H) { $grid[$g0y][$g0x] = 'A' }
L ""
L ("---- shape (A = start point / start-stop line, 1 char ~ {0:N0} mm) ----" -f (1.0 / $sc))
for ($r0 = 0; $r0 -lt $H; $r0++) { L ("  " + (-join $grid[$r0])) }
L ""
L ("csv: $csv")
[IO.File]::WriteAllLines($rep, $rows)
