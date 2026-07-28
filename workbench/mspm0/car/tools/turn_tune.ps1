# turn_tune.ps1 - AUTONOMOUS in-place-turn tuning: sweep (W, Kd), score every run, pick a winner.
#                 No human in the loop for anything the gyro can judge about ITSELF.
#
# WHAT THIS AUTOMATES (and why it had to become a script)
#   The turn loop has three distinct failure modes and a human watching the car can only reliably
#   report the first one:
#     1. wrong final angle        - visible, but eyeball resolution is +/-3..5 deg
#     2. STALL (stops short/past and cannot nudge)  - looks identical to "it finished"
#     3. end-game limit cycle ("it wobbles once before settling") - the user literally said
#        the user reported it as "seemed to wobble once, did not watch closely". That is not a
#        human's job at all: it is a sign-change count.
#   All three are in the telemetry. So: measure them, sweep the two knobs, print a table.
#
# WHAT THIS CANNOT DO - READ THIS BEFORE TRUSTING A "PASS"
#   Every angle here is measured BY THE SAME GYRO THAT DROVE THE TURN. A scale-factor error
#   (reads 90 while the car physically turned 85) is INVISIBLE to every check in this file.
#   => the gyro scale needs exactly ONE external observation, and the cheapest way to spend it
#      is a BIG multiplier: `-Deg 360 -Cycles 4 -SameDir` then have a human read how far the car
#      ended up from its start line. A 2% scale error shows up as 28.8 deg there vs 1.8 deg on a
#      single j90. One look, ~0.5% resolution.
#
# DEFAULT IS ALTERNATING DIRECTION (+deg, -deg, +deg, ...). Two reasons:
#   * the car stays put instead of walking off the test area;
#   * left/right asymmetry comes out for free - and asymmetry is where the OLD board actually
#     failed (forward reached 89.8 deg, reverse overshot to -95 and stalled). Never assume the
#     two directions behave the same: the two motors have different dead zones (measured).
#
# SAFETY: wheels ON THE GROUND, ~0.5 m clear. Always ends with `z`. Firmware hard cap (15 s per
#   mode entry) is the backstop if this script dies mid-turn.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\turn_tune.ps1 -Port COM4
#   ... -WList 8,10,12 -KdList 250,400 -Deg 90 -Cycles 2
#   ... -Deg 360 -Cycles 4 -SameDir          # scale-factor run (needs the human afterwards)
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in .ps1).

param(
    [string]$Port     = 'COM4',
    [int]$Baud        = 115200,
    # TRAP (cost a whole wasted sweep on 2026-07-28, and verify_addr.ps1 hit the same thing):
    #   with `powershell -File script.ps1 -WList 10,14` the shell hands over ONE string "10,14",
    #   so an [int[]] param silently becomes the single number 1014 -> the firmware rejects `W1014`
    #   (out of 0..30) and the sweep runs with whatever was already set. Nothing errors out.
    #   => declare these as [string] and split them ourselves. Always print what was parsed.
    [string]$WList    = '10',
    [string]$KdList   = '250',       # Kd x1000, as the `d` command takes it
    [int]$Deg         = 90,
    [int]$Cycles      = 2,           # runs per (W,Kd) combo
    [switch]$SameDir,                # do not alternate; all turns the same way (multi-lap scale test)
    [int]$TelemMs     = 30,          # telemetry period during the sweep (finer = better wobble detection)
    [double]$Timeout  = 18.0,        # per-run wait for the [nav] scorecard
    [switch]$NoCal,                  # skip the gyro bias calibration `k`
    [double]$CalWait  = 14.0,
    [string]$Csv      = '',
    [string]$Out      = ''
)

$ErrorActionPreference = 'Continue'
$log = New-Object System.Collections.ArrayList
function L([string]$s) { [void]$log.Add($s); Write-Host $s }

function Parse-IntList([string]$s, [string]$name) {
    $out = @()
    foreach ($p in ($s -split '[,; ]+')) { if ($p.Trim() -ne '') { $out += [int]$p.Trim() } }
    if ($out.Count -eq 0) { throw "$name parsed to nothing from '$s'" }
    return $out
}
$Wvals  = Parse-IntList $WList  'WList'
$Kdvals = Parse-IntList $KdList 'KdList'
foreach ($w in $Wvals)  { if ($w -lt 0 -or $w -gt 30) { throw "W=$w out of firmware range 0..30 (the `W` command would be silently rejected)" } }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200

function Send-Cmd([string]$cmd) {
    foreach ($ch in $cmd.ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 20 }
    $sp.Write([string][char]10)
}
# Read until $pat appears or $sec elapses. Returns the whole text (so nothing is thrown away -
# the repo has been bitten by chopping a stream per-ReadExisting-block and losing half lines).
function Read-Until([string]$pat, [double]$sec) {
    $sb = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        try { [void]$sb.Append($sp.ReadExisting()) } catch { }
        if ($pat -ne '' -and $sb.ToString() -match $pat) { break }
        Start-Sleep -Milliseconds 25
    }
    return $sb.ToString()
}

# ---- score one captured run -------------------------------------------------------------
# Telemetry in nav modes carries  ... | D:<v>,<w> | NAV:<state>,<err_mm>,<err_deg*10>,<peak*10> | Y:<yaw*10> ...
function Score-Run([string]$text, [int]$target) {
    $errSeq = @(); $wSeq = @(); $yawSeq = @()
    foreach ($line in ($text -split "`r?`n")) {
        if ($line -match 'D:(-?\d+),(-?\d+).*NAV:\d+,-?\d+,(-?\d+),(-?\d+).*Y:(-?\d+)') {
            $wSeq   += [int]$Matches[2]
            $errSeq += [double]$Matches[3] / 10.0
            $yawSeq += [double]$Matches[5] / 10.0
        }
    }
    $sc = [pscustomobject]@{
        stall = ($text -match 'FAIL=STALL'); off = ($text -match 'OFFCOURSE')
        done  = $null; err = $null; peak = $null; overshoot = $null
        rev   = 0; settle_ms = $null; n = $errSeq.Count; noScore = $true
    }
    if ($text -match 'done_deg\*10=(-?\d+)\s+err_deg\*10=(-?\d+)') {
        $sc.done = [double]$Matches[1] / 10.0
        $sc.err  = [double]$Matches[2] / 10.0
        $sc.noScore = $false
    } elseif ($yawSeq.Count) {
        # scorecard line lost (it lands between captures sometimes) -> fall back to yaw, mark it
        $sc.done = $yawSeq[-1]; $sc.err = $target - $yawSeq[-1]
    }
    if ($yawSeq.Count) {
        $sc.peak = ($yawSeq | ForEach-Object { [math]::Abs($_) } | Measure-Object -Maximum).Maximum
        $sc.overshoot = [math]::Round($sc.peak - [math]::Abs($target), 1)
    }
    # WOBBLE = sign changes of the COMMANDED differential w, counted only after the controller
    # first got inside 2x tolerance. Before that a sign change is just normal braking.
    if ($errSeq.Count -gt 3) {
        $tol2 = 4.0
        $start = -1
        for ($i = 0; $i -lt $errSeq.Count; $i++) { if ([math]::Abs($errSeq[$i]) -le $tol2) { $start = $i; break } }
        if ($start -ge 0) {
            $prev = 0; $rev = 0
            for ($i = $start; $i -lt $wSeq.Count; $i++) {
                $s = if ($wSeq[$i] -gt 0) { 1 } elseif ($wSeq[$i] -lt 0) { -1 } else { 0 }
                if ($s -ne 0 -and $prev -ne 0 -and $s -ne $prev) { $rev++ }
                if ($s -ne 0) { $prev = $s }
            }
            $sc.rev = $rev
            $sc.settle_ms = ($wSeq.Count - $start) * $TelemMs
        }
    }
    return $sc
}

$rows = New-Object System.Collections.ArrayList
$rc = 2
try {
    $sp.Open(); Start-Sleep -Milliseconds 400; try { $sp.DiscardInBuffer() } catch { }

    L ("================ turn_tune  " + (Get-Date -Format 'HH:mm:ss') + " ================")
    L ("port $Port   deg $Deg   cycles/combo $Cycles   " + $(if ($SameDir) { 'SAME direction' } else { 'ALTERNATING +/-' }))
    L ("W list  : " + ($Wvals -join ', ') + "     Kd*1e3 list : " + ($Kdvals -join ', '))
    L 'WHEELS ON THE GROUND, ~0.5 m clear. Ends with z.'
    L ''

    Send-Cmd 'z'
    Send-Cmd ('f' + $TelemMs)
    if (-not $NoCal) {
        L ("gyro bias cal (k) - hold still ~" + $CalWait + "s ...")
        Send-Cmd 'k'
        [void](Read-Until 'cal done' $CalWait)
        Start-Sleep -Milliseconds 500
    }

    L 'combo          run  dir   done_deg   err   overshoot  rev  settle_ms  flags'
    foreach ($w in $Wvals) {
        foreach ($kd in $Kdvals) {
            Send-Cmd 'z'
            Send-Cmd ('W' + $w)
            Send-Cmd 'm9'
            Send-Cmd ('d' + $kd)
            Send-Cmd 'z'
            [void](Read-Until '' 0.6)
            for ($c = 1; $c -le $Cycles; $c++) {
                $sign = if ($SameDir -or ($c % 2 -eq 1)) { 1 } else { -1 }
                $tgt = $Deg * $sign
                Send-Cmd 'o'
                Start-Sleep -Milliseconds 250
                try { $sp.DiscardInBuffer() } catch { }
                Send-Cmd ('j' + $tgt)
                $txt = Read-Until '\[nav\]' $Timeout
                $txt += Read-Until '' 0.8          # let the tail (and any late scorecard) arrive
                Send-Cmd 'z'
                $s = Score-Run $txt $tgt
                $flags = @()
                if ($s.stall)   { $flags += 'STALL' }
                if ($s.off)     { $flags += 'OFFCOURSE' }
                if ($s.noScore) { $flags += 'no-scorecard(yaw-fallback)' }
                L ("W{0,-3} Kd{1,-5}  {2,3}  {3,4}   {4,8}  {5,5}   {6,8}  {7,3}  {8,9}  {9}" -f `
                    $w, $kd, $c, $(if ($sign -gt 0) { 'CCW' } else { 'CW' }), $s.done, $s.err, $s.overshoot, $s.rev, $s.settle_ms, ($flags -join ' '))
                [void]$rows.Add([pscustomobject]@{
                    W = $w; Kd = $kd; run = $c; target = $tgt; done = $s.done; err = $s.err
                    overshoot = $s.overshoot; rev = $s.rev; settle_ms = $s.settle_ms
                    stall = $s.stall; samples = $s.n
                })
                Start-Sleep -Milliseconds 900
            }
        }
    }

    # ---- verdict ----
    L ''
    L '---- per-combo summary (what to lock into config.h) ----'
    L 'combo          |err| max  |err| mean  rev tot  stalls   verdict'
    $best = $null
    foreach ($g in ($rows | Group-Object W, Kd)) {
        $e = $g.Group | ForEach-Object { [math]::Abs($_.err) }
        $emax = ($e | Measure-Object -Maximum).Maximum
        $emean = [math]::Round((($e | Measure-Object -Average).Average), 2)
        $revt = ($g.Group | Measure-Object rev -Sum).Sum
        $st = ($g.Group | Where-Object { $_.stall }).Count
        $v = if ($st -gt 0) { 'REJECT (stall)' } elseif ($emax -gt 3) { 'REJECT (>3 deg)' } elseif ($revt -gt 0) { 'ok but wobbles' } else { 'CLEAN' }
        L ("{0,-14} {1,9} {2,11} {3,8} {4,7}   {5}" -f $g.Name, $emax, $emean, $revt, $st, $v)
        if ($st -eq 0 -and $emax -le 3) {
            $key = $emax + 0.5 * $revt
            if ($null -eq $best -or $key -lt $best.key) { $best = @{ key = $key; name = $g.Name; emax = $emax; rev = $revt } }
        }
    }
    L ''
    if ($best) {
        L ("BEST: W,Kd = " + $best.name + "   |err| max " + $best.emax + " deg, wobble reversals " + $best.rev)
        L '  -> back-fill CFG_DRV_FF_DZ (W) and CFG_KD_TURN (Kd/1000) in config.h, then reflash.'
        $rc = 0
    } else {
        L 'No combo passed (all stalled or all >3 deg). Widen the sweep.'
        $rc = 1
    }
    L ''
    L '!! REMINDER: every angle above was measured by the gyro that drove the turn.'
    L '   Scale factor is NOT verified here. Do that once with: -Deg 360 -Cycles 4 -SameDir'
    L '   then have a human read the car offset from its start line (2% error => 28.8 deg).'
}
catch { L "RESULT: FAIL - $($_.Exception.Message)"; $rc = 1 }
finally {
    try { Send-Cmd 'z' } catch { }
    try { Send-Cmd 'f100' } catch { }
    try { $sp.Close() } catch { }
    if ($Csv -ne '' -and $rows.Count) { $rows | Export-Csv -Path $Csv -NoTypeInformation -Encoding ASCII }
    if ($Out -ne '') { $log | Out-File -FilePath $Out -Encoding UTF8 }
}
exit $rc
