# ball_map.ps1 - separate TUBE SLOPE from STICTION, with repeats, across the working range.
#
# WHY THIS EXISTS (and why the earlier measurement was not good enough)
#   On 2026-07-31 three single-shot experiments suggested a large, position-dependent breakaway angle
#   (0.93 deg at +78 mm; 1.46 deg not enough at +104 mm; 1.99 deg moved it). Those numbers are
#   contaminated, and the contamination changes the diagnosis:
#
#   At the nominal level pulse (1086 us) the ball did NOT stay put in mid-tube - it ran from +35 to
#   +104 mm, and later to +119 mm. Yet the SAME 1086 us held it motionless at +78 and +104 mm. Both can
#   only be true if the LOCAL equilibrium pulse varies along the tube, i.e. the tube is not straight or
#   not level, and 1086 us is merely its average zero (which is exactly what a sweep fitted over mixed
#   positions would return).
#   So "0.93 deg to start moving" was really (cancel the local slope) + (overcome stiction) added
#   together. Worse, the striking "62 fresh frames, ball frozen at 1.00 deg" result has a second, equally
#   consistent explanation: that 1.00 deg may simply have cancelled the local slope, leaving zero net
#   force - no stiction required at all.
#
#   Slope and stiction call for DIFFERENT mechanical fixes (straighten/level/stiffen the tube vs clean it
#   / find a dent or burr), so guessing between them wastes exactly the time it is meant to save.
#
# HOW THEY ARE SEPARATED
#   At each position, first find the LOCAL equilibrium pulse - the pulse at which the ball does not drift.
#   Then step away from THAT pulse in both directions and find where it starts to move.
#     * stiction is symmetric about the local equilibrium: breakaway_up ~ breakaway_down
#     * slope is not symmetric about 1086 us, but it IS absorbed into us_level(x)
#   The us_level(x) curve is therefore the slope/sag map, and the two breakaway numbers about it are the
#   friction, with the slope removed.
#
# WHY REPEATS
#   Breakaway is stochastic - it depends on the exact contact asperities where the ball happens to stop -
#   so a single trial can easily be off by tens of percent. Each condition is repeated -Repeats times and
#   both the mean and the spread are reported. A large spread is itself the finding: it means no single
#   feedforward value can compensate it and the fix has to be mechanical.
#
# SAFETY
#   Every pulse is clamped to MinUs..MaxUs, the beam is levelled between measurements, and a guard keeps
#   the ball away from the end stops. Motors are never driven.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_map.ps1 -Port COM30                 # Quick: 0 mm x2
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_map.ps1 -Port COM30 -Profile Confirm
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_map.ps1 -Port COM30 -Profile Full   # original 5x3 acceptance
#   Explicit -Positions/-Repeats/-StepUs/-DwellMs override the selected profile.
#
# EXIT CODES: 0 = mapped, 1 = mapped and the mechanics are the binding constraint, 2 = could not measure
# ASCII only in the code.
param(
    [string]$Port     = "COM30",
    [int]$Baud        = 115200,
    [int]$CenterUs    = 1086,      # nominal level from ball_ident (the tube's AVERAGE zero)
    [double]$UsPerDeg = 75.4,
    [double]$KTotal   = 1.622,     # |K| mm/s^2 per us
    [int]$MinUs       = 960,
    [int]$MaxUs       = 1320,
    [ValidateSet("Quick","Confirm","Full")]
    [string]$Profile  = "Quick",
    [string]$Positions = "-60,-30,0,30,60",   # Full defaults; profiles override unless explicitly passed
    [int]$Repeats     = 3,
    [int]$StepUs      = 8,         # Full resolution (8 us = 0.106 deg)
    [int]$DwellMs     = 1000,      # Full dwell per breakaway step
    [double]$MoveMm   = 2.5,       # displacement that counts as "moving"
    [double]$PosTolMm = 18.0,      # how close to the requested position is close enough
    [double]$GuardMm  = 105.0,
    [double]$CxPerMm  = 100.0,
    [double]$TrajNeed = 139.0,     # mm/s^2 the +-50 mm trajectory needs
    [string]$Out      = "_logs\ball\ball_map_out.txt",
    [string]$Csv      = "_logs\ball\ball_map.csv",
    [string]$Json     = "_logs\ball\ball_map_result.json",
    [string]$RunId    = ""
)

$ErrorActionPreference = "Continue"
$runSw = [System.Diagnostics.Stopwatch]::StartNew()
if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = "map-" + (Get-Date -Format "yyyyMMdd-HHmmss") }

# Profile values are diagnostic scheduling choices, not identified plant constants. Quick/Confirm timing
# is PENDING REAL-MACHINE VERIFICATION; Full is the original 5-position x 3-repeat acceptance map.
switch ($Profile) {
    "Quick" {
        if (-not $PSBoundParameters.ContainsKey("Positions")) { $Positions = "0" }
        if (-not $PSBoundParameters.ContainsKey("Repeats"))   { $Repeats = 2 }
        if (-not $PSBoundParameters.ContainsKey("StepUs"))    { $StepUs = 24 }
        if (-not $PSBoundParameters.ContainsKey("DwellMs"))   { $DwellMs = 600 }
    }
    "Confirm" {
        if (-not $PSBoundParameters.ContainsKey("Positions")) { $Positions = "-60,0,60" }
        if (-not $PSBoundParameters.ContainsKey("Repeats"))   { $Repeats = 2 }
        if (-not $PSBoundParameters.ContainsKey("StepUs"))    { $StepUs = 12 }
        if (-not $PSBoundParameters.ContainsKey("DwellMs"))   { $DwellMs = 800 }
    }
}
if ($Repeats -lt 1 -or $StepUs -lt 1 -or $DwellMs -lt 100) {
    Write-Host "invalid schedule: Repeats>=1, StepUs>=1, DwellMs>=100 required" -ForegroundColor Red
    exit 2
}
$carRoot = Split-Path $PSScriptRoot -Parent
function ResolveResultPath([string]$p) {
    if ([string]::IsNullOrWhiteSpace($p)) { return "" }
    if ([System.IO.Path]::IsPathRooted($p)) { return [System.IO.Path]::GetFullPath($p) }
    return [System.IO.Path]::GetFullPath((Join-Path $carRoot $p))
}
$Out = ResolveResultPath $Out
$Csv = ResolveResultPath $Csv
$Json = ResolveResultPath $Json
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$result = [ordered]@{
    run_id = $RunId
    tool = "ball_map"
    elapsed_ms = 0
    port = $Port
    parameters = [ordered]@{ profile=$Profile; positions=$Positions; repeats=$Repeats; step_us=$StepUs; dwell_ms=$DwellMs; guard_mm=$GuardMm }
    health = [ordered]@{ completed_trials=0; goto_failures=0; fuse_tripped=$false }
    metrics = [ordered]@{}
    verdict = "INCONCLUSIVE"
    reason = "script did not reach Finish"
    next_action = "inspect raw_log and rerun"
    raw_log = $Out
}

$sp = $null
$rows = New-Object System.Collections.Generic.List[object]
function Finish([string]$verdict, [int]$code) {
    $runSw.Stop()
    $result.elapsed_ms = [int][Math]::Round($runSw.Elapsed.TotalMilliseconds)
    $result.verdict = $(if ($code -eq 0) { "PASS" } elseif ($code -eq 1) { "FAIL" } else { "INCONCLUSIVE" })
    $result.reason = $verdict
    $result.next_action = $(if ($code -eq 0 -and $Profile -ne "Full") { "use -Profile Full only after the fast diagnosis is healthy" } elseif ($code -eq 0) { "full mechanical map completed" } else { "follow reason, recover the ball/mechanism, then rerun Quick" })
    L ""; L ("elapsed: {0:N1} s" -f $runSw.Elapsed.TotalSeconds); L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        if ($rows.Count -gt 0) { $rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $Csv; Write-Host "(csv -> $Csv)" }
        if ($Json) {
            $jd = Split-Path $Json -Parent
            if ($jd -and -not (Test-Path $jd)) { New-Item -ItemType Directory -Path $jd -Force | Out-Null }
            $result | ConvertTo-Json -Depth 7 | Set-Content $Json -Encoding UTF8
            Write-Host ("RESULT_JSON: {0}" -f $Json)
        }
        Write-Host "(log -> $Out)"
    } catch {}
    if ($sp -and $sp.IsOpen) {
        try {
            foreach ($cmd in @("U$CenterUs", "z")) {
                foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
                Start-Sleep -Milliseconds 150
            }
        } catch {}
        try { $sp.Close(); $sp.Dispose() } catch {}
    }
    exit $code
}

L ("================ ball_map  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
L ("profile {0}   port {1}   nominal centre {2} us   step {3} us ({4:N3} deg)   dwell {5} ms   repeats {6}" -f `
    $Profile, $Port, $CenterUs, $StepUs, ($StepUs/$UsPerDeg), $DwellMs, $Repeats)
if ($Profile -ne "Full") { L "  diagnostic schedule only; Quick/Confirm wall-clock and resolution are PENDING REAL-MACHINE VERIFICATION" }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { L ("OPEN_FAIL ({0}): {1}" -f $Port, $_.Exception.Message); Finish "INCONCLUSIVE - serial port could not be opened" 2 }

function Send([string]$c) { foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }
function ClampUs([int]$u) { if ($u -lt $MinUs) { return $MinUs }; if ($u -gt $MaxUs) { return $MaxUs }; return $u }

$reBALL = [regex]'BALL:(?<cx>-?\d+),(?<us>-?\d+),(?<age>\d+),(?<st>\d+),(?<id>-?\d+)'
$rx = ''
$script:lastStamp = -1
$script:frames = New-Object System.Collections.Generic.List[object]
function Drain() {
    $t = ""
    try { $t = $sp.ReadExisting() } catch {}
    if ($t) { $script:rx += $t }
    while ($script:rx.Contains("`n")) {
        $i = $script:rx.IndexOf("`n")
        $ln = $script:rx.Substring(0, $i); $script:rx = $script:rx.Substring($i + 1)
        $m = $reBALL.Match($ln)
        if (-not $m.Success) { continue }
        $st = [int]$m.Groups['st'].Value
        if ($st -eq $script:lastStamp) { continue }
        $script:lastStamp = $st
        if ([int]$m.Groups['id'].Value -eq -1) { continue }
        $script:frames.Add([pscustomobject]@{ x = [double]$m.Groups['cx'].Value / $CxPerMm; st = $st })
    }
}
function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 15 } }
function X() { Drain; if ($script:frames.Count -eq 0) { return $null }; return $script:frames[$script:frames.Count-1].x }

# Drift over a window, in mm. Positive = moved toward +x. Uses first vs last fresh frame, which is what
# "did it move" means here - a velocity estimate would just reintroduce the observer we are auditing.
function Drift([double]$sec) {
    $script:frames.Clear()
    Wait $sec
    if ($script:frames.Count -lt 4) { return $null }
    return ($script:frames[$script:frames.Count-1].x - $script:frames[0].x)
}

function AtRest([double]$timeoutS) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $same = 0; $prev = $null
    while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
        Drain
        $x = X
        if ($null -ne $x) {
            if ($null -ne $prev -and [Math]::Abs($x - $prev) -lt 0.005) { $same++ } else { $same = 0 }
            $prev = $x
            if ($same -ge 6) { return $x }
        }
        Start-Sleep -Milliseconds 25
    }
    return $prev
}

# Move the ball to about $target mm, open loop, then level and let it rest.
#
# Bang-bang with an early release, coarse to fine. The first version used 0.35 s bursts separated by
# levelling and it was far too slow - the ball crawled from +109 mm to +79 mm in a 25 s timeout, because
# each short burst barely got past breakaway before being released. A SUSTAINED push moves it freely
# (measured: 2 s at 150 us above centre carried the ball from +104 mm all the way to -118 mm), so the
# right shape is: hold the push, release early, let it coast, wait for rest, then correct with a smaller
# push. The release margin shrinks each pass, which converges instead of oscillating.
# Positioning uses the FIRMWARE's closed loop (m12), not an open-loop push.
#
# Open-loop bang-bang was tried twice and does not work here, and the reason is the very thing being
# measured: breakaway varies by 3x from trial to trial, so the distance a fixed push produces is
# unpredictable. Asked for -40 mm it delivered +62.7 and +51.9; asked for 0 it delivered +116.6. Four of
# six trials then had to be skipped for sitting too near an end stop, which is how a run comes back with
# no position dependence at all.
# m12 runs at 50 Hz inside the firmware with the observer in the loop and has already been shown to hold
# 0.35 mm mean, so it is strictly the better actuator for this job. The only constraint is the 15 s
# MODE_BALL hard cap (measured from mode entry, not bypassable), which is ample for repositioning.
# After z the beam KEEPS its last commanded pulse - stop_all does not level it (measured: `after z` read
# us=1140) - so the level pulse must be re-sent explicitly before any open-loop measurement.
function GoTo([double]$target, [double]$timeoutS) {
    Send "z"; Start-Sleep -Milliseconds 250
    Send ("t" + [int]$target); Start-Sleep -Milliseconds 200
    Send "m12"; Start-Sleep -Milliseconds 300
    Send "i3";  Start-Sleep -Milliseconds 200
    Send ("t" + [int]$target); Start-Sleep -Milliseconds 200
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $good = 0
    $cap = [Math]::Min($timeoutS, 13.0)          # stay inside the hard cap
    while ($sw.Elapsed.TotalSeconds -lt $cap) {
        Wait 0.15
        $x = X
        if ($null -eq $x) { continue }
        if ([Math]::Abs($x - $target) -le $PosTolMm) { $good++ } else { $good = 0 }
        if ($good -ge 8) { break }               # ~1.2 s inside tolerance before trusting it
    }
    Send "z"; Start-Sleep -Milliseconds 200
    Send ("U" + $CenterUs)                       # z leaves the beam tilted; level it for the open-loop part
    return (AtRest 4.0)
}

# ---- the stick band ----------------------------------------------------------------------------
#
# 🔴 The first version of this looked for a single "local level" pulse - the pulse at which the ball does
# not drift - and measured breakaway relative to it. That is logically broken whenever friction matters,
# and the run of 2026-07-31 06:32 proved it: a ball that is STUCK does not drift at ANY pulse, so the
# search accepted the very first value it tried (the nominal 1086) and reported slope = 0.00 every time.
# The giveaway was in the same output: breakaway came out 0.53 deg on one side and "none within the whole
# travel" on the other. Such asymmetry means the reference was not an equilibrium at all.
#
# With friction, equilibrium is not a point, it is an INTERVAL - the stick band. Its two edges are what
# can actually be measured, and they separate the two effects cleanly:
#     true equilibrium (slope) = (edge_up + edge_dn) / 2
#     stiction                 = (edge_up - edge_dn) / 2
# Both edges are probed from the SAME rest position, alternating so neither side gets a head start, and
# the ball is returned to that rest position between probes.
function StickBand([double]$x0) {
    $up = $null; $dn = $null
    # Probe upward (us above centre pushes toward -x) and downward independently. Each probe starts from
    # the nominal centre and walks outward; the ball is re-levelled and allowed to settle in between so
    # the second probe is not measuring a ball that the first one already nudged.
    foreach ($sign in @(1, -1)) {
        Send ("U" + $CenterUs); Wait 0.6
        $xr = AtRest 4.0
        if ($null -eq $xr) { continue }
        $limit = if ($sign -gt 0) { $MaxUs - $CenterUs } else { $CenterUs - $MinUs }
        $found = $null
        for ($d = $StepUs; $d -le $limit; $d += $StepUs) {
            Send ("U" + (ClampUs ($CenterUs + $sign * $d)))
            $script:frames.Clear()
            Wait ($DwellMs / 1000.0)
            $x = X
            if ($null -eq $x) { continue }
            if ([Math]::Abs($x - $xr) -ge $MoveMm) { $found = $sign * $d; break }
            if ([Math]::Abs($x) -ge $GuardMm) { break }
        }
        if ($sign -gt 0) { $up = $found } else { $dn = $found }
        Send ("U" + $CenterUs); Wait 0.5
    }
    return @{ up = $up; dn = $dn }
}

$posList = @($Positions -split ',' | ForEach-Object { [double]($_.Trim()) })
$fuseReason = $null
$failureSide = $null
$failureStreak = 0

try {
    try { $sp.DiscardInBuffer() } catch {}
    Send "z";   Start-Sleep -Milliseconds 300
    Send "l1";  Start-Sleep -Milliseconds 250
    Send "f25"; Start-Sleep -Milliseconds 250
    Send ("U" + $CenterUs); Wait 1.0

    :positionLoop foreach ($p in $posList) {
        for ($r = 1; $r -le $Repeats; $r++) {
            L ""
            L ("---- position {0,6:N0} mm , trial {1}/{2} ----" -f $p, $r, $Repeats)
            $xr = GoTo $p 25.0
            if ($null -eq $xr) {
                $result.health.goto_failures = [int]$result.health.goto_failures + 1
                if ($failureSide -eq "NO_DATA") { $failureStreak++ } else { $failureSide = "NO_DATA"; $failureStreak = 1 }
                L ("  no camera data after GoTo ({0}/2 consecutive); skipping" -f $failureStreak)
                if ($failureStreak -ge 2) {
                    $fuseReason = "two consecutive GoTo attempts returned no camera data"
                    $result.health.fuse_tripped = $true
                    break positionLoop
                }
                continue
            }

            $missed = [Math]::Abs($xr - $p) -gt 30.0
            $atGuard = [Math]::Abs($xr) -gt $GuardMm
            if ($missed -or $atGuard) {
                $side = if ($xr -ge 0) { "+END" } else { "-END" }
                $result.health.goto_failures = [int]$result.health.goto_failures + 1
                if ($failureSide -eq $side) { $failureStreak++ } else { $failureSide = $side; $failureStreak = 1 }
                L ("  GoTo ended at {0:N1} mm (wanted {1:N0}), side {2} ({3}/2 consecutive same-side failures)" -f `
                    $xr, $p, $side, $failureStreak)
                if ($failureStreak -ge 2) {
                    $fuseReason = "two consecutive GoTo/guard failures ended at the same $side; the ball is not recovering, so further trials would duplicate invalid endpoint data"
                    $result.health.fuse_tripped = $true
                    break positionLoop
                }
            } else {
                $failureSide = $null
                $failureStreak = 0
            }

            if ($missed) {
                L ("  could only reach {0:N1} mm (wanted {1:N0}); first miss only, measuring there once" -f $xr, $p)
            }
            if ($atGuard) { L ("  at {0:N1} mm, too close to a stop; skipping" -f $xr); continue }

            $band = StickBand $xr
            $up = $band.up; $dn = $band.dn
            L ("  stick band edges from nominal centre: up {0}   down {1}" -f `
                $(if ($null -ne $up) { ("{0,+4} us = {1,5:N2} deg" -f $up, ($up/$UsPerDeg)) } else { "none within travel" }), `
                $(if ($null -ne $dn) { ("{0,+4} us = {1,5:N2} deg" -f $dn, ($dn/$UsPerDeg)) } else { "none within travel" }))

            $lvlUs = $null; $slopeDeg = $null; $stictDeg = $null
            if ($null -ne $up -and $null -ne $dn) {
                # Both edges known => the decomposition is available.
                $lvlUs    = $CenterUs + ($up + $dn) / 2.0
                $slopeDeg = (($up + $dn) / 2.0) / $UsPerDeg
                $stictDeg = (($up - $dn) / 2.0) / $UsPerDeg
                L ("  => true equilibrium {0:N0} us (local slope {1,5:N2} deg) ; stiction {2,5:N2} deg ({3:N0} mm/s^2)" -f `
                    $lvlUs, $slopeDeg, $stictDeg, ($stictDeg * $UsPerDeg * $KTotal))
            } else {
                L  "  => only one edge found, so slope and stiction cannot be separated for this trial."
                L  "     One-sided means the true equilibrium is outside the pulse travel at this position,"
                L  "     i.e. the tube slope here exceeds what the beam can cancel - that is itself a finding."
            }

            $rows.Add([pscustomobject]@{
                pos_req_mm = $p; pos_act_mm = [Math]::Round($xr,2); trial = $r
                edge_up_us = $up; edge_dn_us = $dn
                local_level_us = $(if ($null -ne $lvlUs) { [Math]::Round($lvlUs,1) } else { $null })
                local_slope_deg = $(if ($null -ne $slopeDeg) { [Math]::Round($slopeDeg,3) } else { $null })
                stiction_deg = $(if ($null -ne $stictDeg) { [Math]::Round($stictDeg,3) } else { $null }) })
            $result.health.completed_trials = $rows.Count
        }
    }
} finally {
    try { Send ("U" + $CenterUs); Send "z" } catch {}
}

if ($fuseReason) { Finish ("INCONCLUSIVE - fuse tripped: " + $fuseReason) 2 }

if ($rows.Count -eq 0) { Finish "INCONCLUSIVE - no conditions could be measured" 2 }

function Stat($vals) {
    $v = @($vals | Where-Object { $null -ne $_ } | ForEach-Object { [double]$_ })
    if ($v.Count -eq 0) { return $null }
    $m = ($v | Measure-Object -Average).Average
    $sd = if ($v.Count -gt 1) { [Math]::Sqrt((($v | ForEach-Object { ($_-$m)*($_-$m) }) | Measure-Object -Sum).Sum / ($v.Count-1)) } else { 0.0 }
    return @{ mean = $m; sd = $sd; n = $v.Count; min = ($v | Measure-Object -Minimum).Minimum; max = ($v | Measure-Object -Maximum).Maximum }
}

L ""
L "================ slope map (local equilibrium pulse vs position) ================"
L ("{0,8} {1,6} {2,14} {3,14}" -f "pos mm", "n", "level us", "slope deg")
foreach ($p in $posList) {
    $g = @($rows | Where-Object { $_.pos_req_mm -eq $p })
    if ($g.Count -eq 0) { continue }
    $s = Stat @($g | ForEach-Object { $_.local_level_us })
    $sd = Stat @($g | ForEach-Object { $_.local_slope_deg })
    if ($null -eq $s -or $null -eq $sd) {
        L ("{0,8:N0} {1,6} {2,14} {3,14}" -f $p, $g.Count, "not separable", "not separable")
        continue
    }
    L ("{0,8:N0} {1,6} {2,8:N0} +-{3,-4:N0} {4,8:N2} +-{5,-4:N2}" -f $p, $s.n, $s.mean, $s.sd, $sd.mean, $sd.sd)
}
$lvlAll = Stat @($rows | ForEach-Object { $_.local_level_us })
$hasRangeMap = ($null -ne $lvlAll -and $posList.Count -ge 2 -and $lvlAll.n -ge 2)
if ($hasRangeMap) {
    L ("  spread of local level across the range: {0:N0} .. {1:N0} us = {2:N2} deg of tube slope variation" -f `
        $lvlAll.min, $lvlAll.max, (($lvlAll.max - $lvlAll.min)/$UsPerDeg))
} else {
    L "  single-position/insufficient profile: local stick band is measurable, but slope variation across the tube is NOT proven."
}

L ""
L "================ stiction (half-width of the stick band, slope removed) ================"
L ("{0,8} {1,6} {2,20} {3,20}" -f "pos mm", "n", "stiction deg", "equiv mm/s^2")
foreach ($p in $posList) {
    $g = @($rows | Where-Object { $_.pos_req_mm -eq $p })
    if ($g.Count -eq 0) { continue }
    $s = Stat @($g | ForEach-Object { $_.stiction_deg })
    if ($null -eq $s) { L ("{0,8:N0} {1,6} {2,20}" -f $p, $g.Count, "not separable"); continue }
    L ("{0,8:N0} {1,6} {2,20} {3,20:N0}" -f $p, $s.n, ("{0:N2} +-{1:N2}" -f $s.mean, $s.sd), `
        ($s.mean * $UsPerDeg * $KTotal))
}
$allB = Stat @($rows | ForEach-Object { $_.stiction_deg })
if ($null -eq $allB) {
    L ""
    L "  no trial produced BOTH stick-band edges, so stiction could not be separated from slope anywhere."
    L "  That happens when the local tube slope is larger than the beam can cancel: one edge falls outside"
    L "  the 960..1320 us travel. Levelling the tube (or re-centring the horn to widen the travel) has to"
    L "  come before any of this can be quantified."
    Finish "INCONCLUSIVE - stick band one-sided everywhere; level the tube first" 2
}

L ""
L "================ verdict ================"
$slopeVar = if ($hasRangeMap) { ($lvlAll.max - $lvlAll.min) / $UsPerDeg } else { $null }
$needDeg  = $TrajNeed / 7007.0 * 180.0 / [Math]::PI
$degUp = ($MaxUs - $CenterUs) / $UsPerDeg
$degDn = ($CenterUs - $MinUs) / $UsPerDeg
$avail = [Math]::Min($degUp, $degDn)
if ($hasRangeMap) { L ("  tube slope varies by {0:N2} deg over the mapped range" -f $slopeVar) }
else { L "  tube slope variation     : NOT MEASURED by this profile" }
if ($allB) { L ("  stiction (slope removed) : {0:N2} deg mean, spread {1:N2}, range {2:N2}..{3:N2} over {4} trials" -f `
    $allB.mean, $allB.sd, $allB.min, $allB.max, $allB.n) }
L ("  authority available      : {0:N2} deg (weaker side)" -f $avail)
L ("  trajectory needs         : {0:N2} deg" -f $needDeg)
L ""
if ($hasRangeMap -and $null -ne $allB -and $slopeVar -gt $allB.mean) {
    L  "  => SLOPE dominates friction. The tube is not straight/level along its length, and that is what"
    L  "     ate the authority budget - not the ball sticking. Fix: level and straighten the tube (check"
    L  "     for sag at the ends, check it is level in the OTHER axis too, check the mounts are rigid)."
    L  "     A slope map like this can also be fed forward in software if the mechanics cannot improve."
} elseif ($hasRangeMap -and $null -ne $allB) {
    L  "  => FRICTION dominates slope. Fix at the source: clean the tube and the ball, inspect the inner"
    L  "     wall for a dent or burr (the task statement itself excludes dented tubes), and check the ball"
    L  "     is not deformed. Software can only paper over this with dither or a breakaway feedforward."
} else {
    L  "  => Quick is deliberately a centre-point diagnostic. It can reject a bad stick band quickly,"
    L  "     but it cannot decide slope-vs-friction across the full tube; use Confirm/Full only if needed."
}
$result.metrics = [ordered]@{
    stiction_mean_deg = [Math]::Round($allB.mean, 4)
    stiction_sd_deg = [Math]::Round($allB.sd, 4)
    stiction_trials = $allB.n
    slope_variation_deg = $(if ($hasRangeMap) { [Math]::Round($slopeVar, 4) } else { $null })
    authority_deg = [Math]::Round($avail, 4)
    trajectory_need_deg = [Math]::Round($needDeg, 4)
    full_range_proven = ($Profile -eq "Full" -and $hasRangeMap)
}
if (($avail - $allB.mean) -lt $needDeg) {
    Finish ("FAIL - mechanics bind: {0:N2} deg usable after {1:N2} deg stiction, need {2:N2}" -f ($avail-$allB.mean), $allB.mean, $needDeg) 1
}
if ($Profile -eq "Quick") { Finish "PASS - Quick centre diagnostic completed with authority headroom; full-range slope is not proven" 0 }
if ($Profile -eq "Confirm") { Finish "PASS - Confirm map completed with authority headroom; use Full only for final acceptance evidence" 0 }
Finish "PASS - Full map completed; authority has headroom above stiction" 0
