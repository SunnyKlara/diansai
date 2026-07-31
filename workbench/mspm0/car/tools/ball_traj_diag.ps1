# ball_traj_diag.ps1 - DIAGNOSTIC companion to ball_traj.ps1 (which is the acceptance test that reads
# the firmware's own scorecard). This one reconstructs the tracking error PC-side from the m12-only
# `BE:` field, because that is what answers "is the shortfall a lack of AUTHORITY or a lack of
# TRACKING?" - it prints x_ref, th_cmd and the saturated-frame count alongside the position.
# Measured 2026-07-31 with it: P1 peaked at +44.4 mm with saturation at 0%, i.e. tracking, not authority.
#
# Original header follows.
# run and score the requirement-3 round trip: centre -> +50 mm -> back -> -50 mm,
# whole thing inside 5 s, ball must not leave the beam.
#
# WHY A SEPARATE TOOL FROM ball_run.ps1: HOLD is scored on "how far from zero", a trajectory is scored
# on "how far from the reference AT THIS INSTANT" plus "did it get there" plus "how long did it take".
# Those need x_ref, which only appears in the m12-only `BE:` telemetry field, and they need the run
# segmented in time rather than summarised over a fixed window.
#
# Telemetry parsed (car.c):
#   BALL:<cx*100 mm>,<servo_us>,<age_ms>,<stamp_ms>,<id>      id=-1 => camera sees no ball
#   BE:<x_est*10>,<v_est*10>,<x_ref*10>,<th_cmd*10>,<sat>,<peak*10>
# x_est is the observer output and x_ref the commanded profile, so tracking error = x_est - x_ref is
# exactly what the firmware itself is controlling on - no PC-side reconstruction of the profile needed.
#
# Constraints designed around (measured 2026-07-31):
#   * commands must go out one char at a time (4-byte MCU RX FIFO swallows longer bursts silently)
#   * CFG_RUN_MS_HARDCAP for MODE_BALL is 15 s and is not bypassable; the profile is ~4.5 s so there is
#     room, but the capture must not be stretched past it or the beam goes limp mid-measurement
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [string]$Tag = "traj",
    [int]$TeleMs = 50,
    [double]$SettleMm = 15.0,
    [double]$SettleWaitS = 12.0,
    [double]$Seconds = 9.0,      # capture window; profile is ~4.5 s, leaves room to see the settle
    [double]$AmpMm = 50.0,       # CFG_BALL_TRAJ_AMP_MM - what the task asks for
    [double]$LimitS = 5.0,       # the task's time limit for the round trip
    [string[]]$PreCmds = @(),
    [string]$OutDir = "_logs\ball"
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
$lines = New-Object System.Collections.ArrayList   # non-telemetry lines worth keeping ([ball] etc.)
function Tx { param($s) foreach ($c in ([string]$s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Drain {
    $v = New-Object System.Collections.ArrayList
    $parts = $script:rx -split "`n"
    $script:rx = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        $ln = $parts[$i]
        if ($ln -match 'TRAJ|st=TRAJ|\[ball\]') { [void]$script:lines.Add($ln.Trim()) }
        if ($ln -match 'BALL:(-?\d+),(-?\d+),(-?\d+),(\d+),(-?\d+)') {
            $id = [int]$Matches[5]; $cx = [int]$Matches[1] / 100.0; $us = [int]$Matches[2]
            $xe = $null; $xr = $null; $th = $null; $sat = $null
            if ($ln -match 'BE:(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)') {
                $xe = [int]$Matches[1] / 10.0; $xr = [int]$Matches[3] / 10.0
                $th = [int]$Matches[4] / 10.0; $sat = [int]$Matches[5]
            }
            [void]$v.Add([pscustomobject]@{
                t_s = 0.0; cx = $cx; servo_us = $us; id = $id; seen = ($id -ge 0)
                x_est = $xe; x_ref = $xr; th_cmd = $th; sat = $sat
            })
        }
    }
    return , @($v)
}
function Soak { param($ms)
    $t = Get-Date
    while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $script:rx += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 40 }
}

L ("==== ball_traj  tag=$Tag  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
L ("task: centre -> +{0}mm -> reverse -> -{0}mm, all inside {1}s, ball must stay on the beam" -f $AmpMm, $LimitS)
Tx "z"; Soak 400
Tx ("f" + $TeleMs); Soak 300
$script:rx = ''
Tx "m12"; Soak 500
foreach ($c in $PreCmds) { foreach ($cc in ([string]$c -split ',')) { if ($cc.Trim()) { Tx $cc.Trim(); Soak 250; L ("pre: " + $cc.Trim()) } } }
$script:rx = ''
Tx "?"; Soak 700
foreach ($ln in ($script:rx -split "`n")) { if ($ln -match 'kp\*1000=\d+' -or $ln -match 'center=\d+') { L ("readback: " + $ln.Trim()) } }

# ---- settle at centre. HOLD must be solid before a profile means anything ----
$script:rx = ''
$lastX = 999.0; $settled = $false
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $SettleWaitS) {
    Soak 400
    foreach ($s in (Drain)) { if ($s.seen) { $lastX = $s.cx } }
    if ([math]::Abs($lastX) -lt $SettleMm) { $settled = $true; break }
}
L ("settle: |x|={0:N1}mm after {1:N1}s -> {2}" -f $lastX, ((Get-Date) - $t0).TotalSeconds, $(if ($settled) { "OK" } else { "FAILED" }))
if (-not $settled) {
    Tx "z"; Soak 300; $sp.Close()
    L "RESULT: ABORT - HOLD never settled; a trajectory measured from a random start is not comparable."
    [IO.File]::WriteAllLines($rep, $rows); exit 2
}

# ---- fire the profile ----
$script:rx = ''
[void]$script:lines.Clear()
$series = New-Object System.Collections.ArrayList
Tx "P1"
$tc = Get-Date
while (((Get-Date) - $tc).TotalSeconds -lt $Seconds) {
    Soak 300
    foreach ($s in (Drain)) { $s.t_s = [math]::Round(((Get-Date) - $tc).TotalSeconds, 2); [void]$series.Add($s) }
}
Tx "P0"; Soak 300
Tx "z"; Soak 300
$sp.Close()

foreach ($ln in $script:lines) { if ($ln -match 'TRAJ') { L ("fw: " + $ln) } }

# ---- score ----
$seen = @($series | Where-Object { $_.seen })
$blind = $series.Count - $seen.Count
L ("samples: {0} total, {1} with a real target, {2} blind ({3:N1}%)" -f $series.Count, $seen.Count, $blind, (100.0 * $blind / [math]::Max(1, $series.Count)))
if ($seen.Count -lt 30) {
    L "RESULT: INCONCLUSIVE - too few samples with a target."
    [IO.File]::WriteAllLines($rep, $rows); exit 2
}
$series | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv

$xs = @($seen | ForEach-Object { $_.cx })
$maxPos = ($xs | Measure-Object -Maximum).Maximum
$minPos = ($xs | Measure-Object -Minimum).Minimum
# tracking error uses the firmware's own reference, so it is exactly what the controller saw
$tr = @($seen | Where-Object { $_.x_ref -ne $null } | ForEach-Object { [math]::Abs($_.x_est - $_.x_ref) })
$satN = @($seen | Where-Object { $_.sat -eq 1 }).Count
# reached-the-target instants: first time the measured position gets within 5 mm of each end
$tPlus = ($seen | Where-Object { $_.cx -ge ($AmpMm - 5.0) } | Select-Object -First 1)
$tMinus = ($seen | Where-Object { $_.cx -le -($AmpMm - 5.0) } | Select-Object -First 1)

L ""
L "---- scoring ----"
L ("  reached +{0}mm  : {1}" -f $AmpMm, $(if ($tPlus) { "YES at t={0:N2}s (peak +{1:N1}mm)" -f $tPlus.t_s, $maxPos } else { "NO  (best +{0:N1}mm)" -f $maxPos }))
L ("  reached -{0}mm  : {1}" -f $AmpMm, $(if ($tMinus) { "YES at t={0:N2}s (peak {1:N1}mm)" -f $tMinus.t_s, $minPos } else { "NO  (best {0:N1}mm)" -f $minPos }))
if ($tPlus -and $tMinus) {
    $dur = [math]::Max($tPlus.t_s, $tMinus.t_s)
    L ("  round trip time : {0:N2}s   (task limit {1:N1}s) -> {2}" -f $dur, $LimitS, $(if ($dur -le $LimitS) { "OK" } else { "OVER" }))
}
if ($tr.Count -gt 0) {
    L ("  tracking |x-ref|: peak {0:N1}mm   mean {1:N1}mm   (firmware's own reference)" -f `
        (($tr | Measure-Object -Maximum).Maximum), (($tr | Measure-Object -Average).Average))
}
L ("  saturated frames: {0} of {1} ({2:N0}%)  <- >0 means the profile is asking for more than the beam has" -f `
    $satN, $seen.Count, (100.0 * $satN / $seen.Count))
L ("  max |x| reached : {0:N1}mm   (soft limit 110, physical end 125 - ball off the beam = task failed)" -f `
    (($xs | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum))
L ("csv: $csv")
[IO.File]::WriteAllLines($rep, $rows)
if ($tPlus -and $tMinus) { exit 0 } else { exit 1 }
