# ball_sweep_hold.ps1 - do the requirement-3 round trip by STEPPING THE SETPOINT (`t<mm>`) instead of
# running the built-in smooth profile (`P1`), and time it.
#
# WHY: measured 2026-07-31, `P1` at the tuned gains (kp5 kd2) undershoots - the ball peaked at +44.4 mm
# while the profile had already turned around, because the profile finishes in 4.8 s and the ball lags
# it by roughly 0.4 s. Saturation during that run was 0 %, so the beam had authority to spare: it is a
# FEEDFORWARD/tracking problem, not an authority problem, and raising kp to fix tracking destabilised the
# loop instead (kp8/kp12 at kd2 diverged to 103 mm / 120 mm peaks, 63 %/80 % saturated) because kd has to
# scale with sqrt(kp). The profile's amplitude is compiled in (`ball_start_traj(&g_ball, 0.0f)` always
# takes the config value), so widening it would cost a 145 s reflash.
#
# The task only asks the ball to travel out to +-5 cm and back inside 5 s without leaving the beam. A
# setpoint step does exactly that, uses the already-proven HOLD tuning, and needs no reflash.
#
# Scoring: time from issuing each step until the ball first comes within -Tol of that target, plus the
# overshoot past it and whether it ever approached the soft limit.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [string]$Tag = "sweep",
    [int]$TeleMs = 50,
    [double]$AmpMm = 50.0,
    [double]$Tol = 5.0,          # "arrived" band around the target
    [double]$LegS = 3.0,         # max time allowed per leg before giving up on it
    [double]$SettleMm = 12.0,
    [double]$SettleWaitS = 12.0,
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
# one char at a time - the MCU RX FIFO is 4 bytes and silently drops longer bursts
function Tx { param($s) foreach ($c in ([string]$s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Drain {
    $v = New-Object System.Collections.ArrayList
    $parts = $script:rx -split "`n"
    $script:rx = $parts[-1]
    for ($i = 0; $i -lt $parts.Count - 1; $i++) {
        if ($parts[$i] -match 'BALL:(-?\d+),(-?\d+),(-?\d+),(\d+),(-?\d+)') {
            if ([int]$Matches[5] -ge 0) {
                [void]$v.Add([pscustomobject]@{ x = [int]$Matches[1] / 100.0; us = [int]$Matches[2] })
            }
        }
    }
    return , @($v)
}
function Soak { param($ms)
    $t = Get-Date
    while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $script:rx += $sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 40 }
}

$log = New-Object System.Collections.ArrayList   # t_s, target, x, us
$legRows = New-Object System.Collections.ArrayList
function Leg { param($target, $label)
    $t0 = Get-Date
    Tx ("t" + [int]$target)
    $arrived = $null; $peak = 0.0
    while (((Get-Date) - $t0).TotalSeconds -lt $LegS) {
        Soak 200
        foreach ($s in (Drain)) {
            $el = ((Get-Date) - $script:tRun).TotalSeconds
            [void]$script:log.Add([pscustomobject]@{ t_s = [math]::Round($el, 2); target = $target; x = $s.x; us = $s.us })
            if ([math]::Abs($s.x) -gt [math]::Abs($peak)) { $peak = $s.x }
            if ($null -eq $arrived -and [math]::Abs($s.x - $target) -le $Tol) { $arrived = ((Get-Date) - $t0).TotalSeconds }
        }
        if ($arrived -ne $null) { break }
    }
    # NOT $legs: PowerShell variable names are case-insensitive, so $legs IS the [double]$LegS parameter
    # and .Add() fails with "[System.Double] does not contain a method named 'Add'". Third time tonight
    # this pattern bit ($out/$Out, $dir, $legs/$LegS) -> internal collections get a distinct suffix.
    [void]$script:legRows.Add([pscustomobject]@{ label = $label; target = $target; t = $arrived; peak = $peak })
    L ("  {0,-14} target={1,6:N0}mm  arrived={2}  peak_seen={3:N1}mm" -f `
        $label, $target, $(if ($arrived) { "{0:N2}s" -f $arrived } else { "NO (>{0:N1}s)" -f $LegS }), $peak)
    return $arrived
}

L ("==== ball_sweep_hold  tag=$Tag  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
L ("round trip by setpoint steps: 0 -> +{0} -> -{0}mm, target total <= 5s, tol {1}mm" -f $AmpMm, $Tol)
Tx "z"; Soak 400
Tx ("f" + $TeleMs); Soak 300
$script:rx = ''
Tx "m12"; Soak 500
foreach ($c in $PreCmds) { foreach ($cc in ([string]$c -split ',')) { if ($cc.Trim()) { Tx $cc.Trim(); Soak 250; L ("pre: " + $cc.Trim()) } } }
$script:rx = ''
Tx "?"; Soak 700
foreach ($ln in ($script:rx -split "`n")) { if ($ln -match 'kp\*1000=\d+' -or $ln -match 'center=\d+') { L ("readback: " + $ln.Trim()) } }

# settle at centre first
$script:rx = ''
$lastX = 999.0; $ok = $false
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $SettleWaitS) {
    Soak 400
    foreach ($s in (Drain)) { $lastX = $s.x }
    if ([math]::Abs($lastX) -lt $SettleMm) { $ok = $true; break }
}
L ("settle at centre: |x|={0:N1}mm after {1:N1}s -> {2}" -f $lastX, ((Get-Date) - $t0).TotalSeconds, $(if ($ok) { "OK" } else { "FAILED" }))
if (-not $ok) { Tx "z"; Soak 300; $sp.Close(); L "RESULT: ABORT - HOLD did not settle."; [IO.File]::WriteAllLines($rep, $rows); exit 2 }

$script:rx = ''
$script:tRun = Get-Date
L ""
L "---- legs ----"
$a1 = Leg  $AmpMm      "out to +A"
$a2 = Leg (-$AmpMm)    "across to -A"
$total = ((Get-Date) - $script:tRun).TotalSeconds
Tx "t0"; Soak 400
Tx "z"; Soak 300
$sp.Close()

$log | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv
$xs = @($log | ForEach-Object { $_.x })
$absmax = ($xs | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum
L ""
L "---- scoring ----"
L ("  total round trip : {0:N2}s   (task limit 5.0s) -> {1}" -f $total, $(if ($total -le 5.0) { "OK" } else { "OVER" }))
L ("  max |x| reached  : {0:N1}mm  (soft limit 110, physical end 125)" -f $absmax)
L ("  samples          : {0}" -f $log.Count)
L ("csv: $csv")
[IO.File]::WriteAllLines($rep, $rows)
if ($a1 -and $a2 -and $total -le 5.0 -and $absmax -lt 110.0) { exit 0 } else { exit 1 }
