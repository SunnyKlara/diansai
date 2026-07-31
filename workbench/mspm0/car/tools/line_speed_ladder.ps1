# line_speed_ladder.ps1 - push the line follower up a speed ladder and find where it breaks.
#
# WHY: requirement 2 wants the standard 6141.6 mm lap in <=20 s => 307 mm/s average, and the geometry
# reconstruction (tools/track_map.ps1) showed the car currently runs at ~185 mm/s with straights and
# corners within 5% of each other. So corners are NOT the limit - cruise speed is - and the only open
# question is how fast the follower can go before it starts losing the line.
#
# ACCEPTANCE per rung (in priority order):
#   lostSeg == 0            the line was never lost. This is the hard gate; everything else is comfort.
#   |line err| peak         the 8-probe array only spans +-42 mm, so a peak near that means it is about
#                           to fall off the end of the array and has no idea how far off it really is.
#   actual vs commanded     measured from encoder counts. A growing gap means the speed loop is
#                           saturating or the wheels are slipping, either of which invalidates the rung.
#
# `t<rpm>` MUST be sent AFTER K: entering m11 resets the cruise to CFG_TASK_V_CRUISE, so setting it
# before the start silently has no effect (this is why ball_run.ps1 re-asserts cruise after K too).
#
# Line error lives only in the `[task]` line, which is printed on `?` - it is not in the `[ctl]` stream -
# hence the polling. At 400 ms the err peak is undersampled, but lostSeg and xcnt are CUMULATIVE
# counters so those two can never be missed.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    # [string[]] not [int[]] on purpose: `powershell -File x.ps1 -Speeds 65,85,105` hands the script ONE
    # string "65,85,105" because arguments crossing the -File boundary are literal strings and the comma
    # is not an array separator there. Declared as int[] it fails outright with a transformation error;
    # this is the same gotcha already documented for ball_run.ps1's -PreCmds, so it is split below.
    [string[]]$Speeds = @("65", "85", "105", "125"),
    [double]$RunS = 15.0,
    # Raise the CURVE cap along with the cruise. Measured 2026-07-31: with the cap left at
    # CFG_LINE_VSEG_V_SLOW=55 rpm, commanding t125 only produced 226 mm/s (59% of command) because the
    # practice track is 64% curve and `vseg` sat at 55 the whole time. Cruise alone can only speed up the
    # straight fraction, so the cap is the binding constraint, not the cruise.
    [switch]$MatchSlow,
    # Extra online knobs applied on every rung, comma separated, e.g. -Extra "i120,p2600".
    # Split inside the script: a comma-separated value crossing the -File boundary arrives as ONE string.
    [string]$Extra = "",
    [double]$PollMs = 400,
    [double]$CountsPerMm = 5.109,
    [string]$OutDir = "_logs\track"
)
$root = Split-Path -Parent $PSScriptRoot
$d0 = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $d0)) { New-Item -ItemType Directory -Path $d0 -Force | Out-Null }
$rep = Join-Path $d0 ("ladder_{0}.txt" -f (Get-Date -Format "HHmmss"))
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
    Tx "?"; Soak 450
    $o = [pscustomobject]@{ err = $null; lostSeg = $null; xcnt = $null; on = $null; wlp = $null; vseg = $null; st = $null; c1 = $null; c2 = $null }
    foreach ($ln in ($script:rx -split "`n")) {
        if ($ln -match 'err0\.1mm=(-?\d+) cal=\w+ lostSeg=(\d+) on=(\d+)/\d+ xrun=\d+ xcnt=(\d+) wlp=(-?\d+) vseg=(-?\d+)') {
            $o.err = [int]$Matches[1] / 10.0; $o.lostSeg = [int]$Matches[2]; $o.on = [int]$Matches[3]
            $o.xcnt = [int]$Matches[4]; $o.wlp = [int]$Matches[5]; $o.vseg = [int]$Matches[6]
        }
        if ($ln -match '\[task\] (\w+) t=') { $o.st = $Matches[1] }
        if ($ln -match 'C:(-?\d+),(-?\d+)') { $o.c1 = [int]$Matches[1]; $o.c2 = [int]$Matches[2] }
    }
    return $o
}

L ("==== line_speed_ladder  port=$Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
L ("target: requirement 2 needs 307 mm/s average on the standard 6141.6 mm lap (<=20 s)")
L ""
$summary = New-Object System.Collections.ArrayList
$rungs = @(); foreach ($sp0 in $Speeds) { foreach ($t0 in ([string]$sp0 -split ',')) { if ($t0.Trim()) { $rungs += [int]$t0.Trim() } } }
L ("rungs: " + ($rungs -join ' -> ') + " rpm")
foreach ($v in $rungs) {
    Tx "z"; Soak 500
    Tx "f50"; Soak 300
    $script:rx = ''
    $started = $false
    for ($k = 1; $k -le 4; $k++) {
        Tx "K"; Soak 500
        $s = Ask
        if ($s.st -eq 'RUN') { $started = $true; break }
    }
    if (-not $started) { L ("t{0}: ABORT - task never entered RUN" -f $v); continue }
    Tx ("t" + $v); Soak 250          # AFTER K on purpose - see header
    if ($MatchSlow) { Tx ("D" + $v); Soak 250 }   # lift the curve cap to the same value
    if ($Extra) { foreach ($cc in ($Extra -split ',')) { if ($cc.Trim()) { Tx $cc.Trim(); Soak 250 } } }
    $b = Ask
    if ($null -eq $b.c1) { L ("t{0}: ABORT - no telemetry" -f $v); Tx "z"; Soak 300; continue }
    $c10 = $b.c1; $c20 = $b.c2; $ls0 = $b.lostSeg; $xc0 = $b.xcnt
    $errs = New-Object System.Collections.ArrayList
    $vsegs = New-Object System.Collections.ArrayList
    $t0 = Get-Date
    $end = ''
    $last = $b
    while (((Get-Date) - $t0).TotalSeconds -lt $RunS) {
        Start-Sleep -Milliseconds ([int]$PollMs)
        $s = Ask
        if ($null -eq $s.err) { continue }
        [void]$errs.Add([math]::Abs($s.err)); [void]$vsegs.Add($s.vseg)
        $last = $s
        if ($s.st -eq 'DONE') { $end = 'DONE (auto-stop)'; break }
        if ($s.st -eq 'ABORT') { $end = 'ABORT'; break }
    }
    $el = ((Get-Date) - $t0).TotalSeconds
    Tx "z"; Soak 400
    $dc = ((($last.c1 - $c10) + ($last.c2 - $c20)) / 2.0)
    $mm = $dc / $CountsPerMm
    $act = if ($el -gt 0.5) { $mm / $el } else { 0 }
    $ep = if ($errs.Count) { ($errs | Measure-Object -Maximum).Maximum } else { -1 }
    $ea = if ($errs.Count) { ($errs | Measure-Object -Average).Average } else { -1 }
    $o = [pscustomobject]@{
        v = $v; act = $act; lost = ($last.lostSeg - $ls0); xc = ($last.xcnt - $xc0)
        epk = $ep; eav = $ea; el = $el; end = $end
        vsmin = $(if ($vsegs.Count) { ($vsegs | Measure-Object -Minimum).Minimum } else { 0 })
    }
    [void]$summary.Add($o)
    L ("t{0,-4} {1,5:N1}s  actual {2,4:N0} mm/s ({3,3:N0}% of cmd)  lostSeg {4}  |err| avg {5,4:N1} peak {6,4:N1} mm  xcnt+{7}  vseg_min {8}  {9}" -f `
        $v, $el, $act, (100.0 * $act / ($v * 3.08)), $o.lost, $ea, $ep, $o.xc, $o.vsmin, $end)
}
$sp.Close()
L ""
L "---- verdict ----"
$ok = @($summary | Where-Object { $_.lost -eq 0 -and $_.epk -lt 30 })
if ($ok.Count -gt 0) {
    $bestv = ($ok | Sort-Object v -Descending)[0]
    L ("highest rung with lostSeg=0 and |err| peak < 30 mm : t{0}  (actual {1:N0} mm/s)" -f $bestv.v, $bestv.act)
    L ("standard lap 6141.6 mm at that speed = {0:N1} s   -> req2 (<=20s): {1}   req5/6 (<=30s): {2}" -f `
        (6141.6 / $bestv.act), $(if ((6141.6 / $bestv.act) -le 20) { "PASS" } else { "FAIL" }), $(if ((6141.6 / $bestv.act) -le 30) { "PASS" } else { "FAIL" }))
}
else { L "no rung met lostSeg=0 with |err| peak < 30 mm" }
L "reminder: this practice track has tighter corners (median R=433mm) than the standard 500mm, so a"
L "speed that holds here should also hold on the real map - it is a conservative test bed."
[IO.File]::WriteAllLines($rep, $rows)
