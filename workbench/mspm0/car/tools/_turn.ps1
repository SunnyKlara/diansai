# Summarise one turn capture: report the nav verdict + how yaw settles AFTER the turn ends.
# Why the settle part matters: the interesting error is not only "did it reach 90" but also
# "does it drift/recoil after stopping" (gyro integration drift + mechanical recoil).
# Writes everything to a file (this shell swallows piped output intermittently).
# ASCII-only by repo rule.
param([string]$File, [string]$Out = '_turn_report.txt')

$l = Get-Content $File -Encoding UTF8
$res = New-Object System.Collections.Generic.List[string]
$res.Add("file: $File   lines: $($l.Count)")
$res.Add('')
$res.Add('--- nav lines ---')
foreach ($ln in $l) { if ($ln -match '\[nav\]') { $res.Add($ln.Trim()) } }

# collect yaw (x10 deg) and W (x100 dps) in order
$Y = @(); $W = @(); $mode = @()
foreach ($ln in $l) {
    if ($ln -match 'Y:(-?\d+)\s+W:(-?\d+)') {
        $Y += [int]$Matches[1]; $W += [int]$Matches[2]
        $mode += if ($ln -match '\]\s*(\w+)\s+tgt') { $Matches[1] } else { '?' }
    }
}
$res.Add('')
$res.Add("--- yaw samples: $($Y.Count) ---")
if ($Y.Count -gt 0) {
    $res.Add(("peak |yaw| during run = {0:N1} deg" -f (($Y | ForEach-Object { [Math]::Abs($_) } | Measure-Object -Maximum).Maximum / 10)))
    $res.Add(("peak |W| = {0:N1} dps" -f (($W | ForEach-Object { [Math]::Abs($_) } | Measure-Object -Maximum).Maximum / 100)))
    # IDLE samples after the last non-IDLE one = the settle window
    $lastMove = -1
    for ($i = 0; $i -lt $mode.Count; $i++) { if ($mode[$i] -ne 'IDLE') { $lastMove = $i } }
    if ($lastMove -ge 0 -and $lastMove -lt $Y.Count-1) {
        $set = $Y[($lastMove+1)..($Y.Count-1)]
        $res.Add('')
        $res.Add("--- settle window (IDLE after turn): $($set.Count) samples ---")
        $res.Add(("yaw first {0:N1} deg -> last {1:N1} deg   drift {2:N1} deg" -f ($set[0]/10), ($set[-1]/10), (($set[-1]-$set[0])/10)))
        $res.Add(("yaw min {0:N1}  max {1:N1}  (peak-to-peak {2:N1} deg)" -f (($set|Measure-Object -Min).Minimum/10), (($set|Measure-Object -Max).Maximum/10), ((($set|Measure-Object -Max).Maximum-($set|Measure-Object -Min).Minimum)/10)))
    } else {
        $res.Add('(no IDLE samples after the move -- capture window too short to judge settling)')
    }
}
$res | Out-File $Out -Encoding utf8
Get-Content $Out
