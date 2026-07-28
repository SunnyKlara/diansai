# Analyse one telemetry capture: report STEADY-STATE stats only.
# TRAP this fixes: naively skipping a fixed small number of samples leaves the acceleration
# ramp inside the "steady" window -> std/CV blow up (seen 70% CV) and you misdiagnose clean
# feedback as noisy. Here the steady window is found from the DATA: take the last N samples,
# and additionally report the per-tick encoder delta so "is the count monotonic" is visible.
# ASCII-only by repo rule.
param([string]$File, [int]$TailN = 30)

$lines = Get-Content $File -Encoding UTF8 | Where-Object { $_ -match 'V:(-?\d+),(-?\d+)' }
$L=@(); $R=@(); $CL=@(); $CR=@(); $T=@()
foreach ($ln in $lines) {
    if ($ln -match 'V:(-?\d+),(-?\d+)') { $L += [int]$Matches[1]; $R += [int]$Matches[2] }
    if ($ln -match 'C:(-?\d+),(-?\d+)')  { $CL += [int]$Matches[1]; $CR += [int]$Matches[2] }
    if ($ln -match 't(\d+)')             { $T  += [int]$Matches[1] }
}
if ($L.Count -lt 5) { Write-Output "too few samples ($($L.Count))"; exit 1 }

$n = [Math]::Min($TailN, $L.Count)
$Ls = $L[($L.Count-$n)..($L.Count-1)]
$Rs = $R[($R.Count-$n)..($R.Count-1)]

# NOTE: returns an object and prints NOTHING -- a PowerShell function returns its whole
# output stream, so a Write-Output inside would end up inside the returned array (real bug hit
# on 2026-07-27: "Cannot convert value 'LEFT V avg 45.2...' to type System.Int32").
function stat($a) {
    $m = ($a | Measure-Object -Average).Average
    $sd = [Math]::Sqrt((($a | ForEach-Object { ($_-$m)*($_-$m) }) | Measure-Object -Sum).Sum / $a.Count)
    [pscustomobject]@{
        Avg = $m
        Std = $sd
        Min = ($a | Measure-Object -Minimum).Minimum
        Max = ($a | Measure-Object -Maximum).Maximum
        CV  = if ($m -ne 0) { 100*$sd/[Math]::Abs($m) } else { 0 }
    }
}
function show($s, $name) {
    Write-Output ("{0,-8} avg {1,7:N1}  std {2,5:N1}  min {3,5}  max {4,5}  CV {5,4:N1}%" -f $name, $s.Avg, $s.Std, $s.Min, $s.Max, $s.CV)
}

Write-Output ("total samples {0}, steady window = last {1}" -f $L.Count, $n)
$sl = stat $Ls
$sr = stat $Rs
show $sl 'LEFT V'
show $sr 'RIGHT V'
$ml = $sl.Avg; $mr = $sr.Avg
$avg = ($ml + $mr) / 2
if ($avg -ne 0) {
    Write-Output ("asymmetry  L-R = {0:N1} RPM  ({1:N1}% of mean)" -f ($ml-$mr), (100*[Math]::Abs($ml-$mr)/[Math]::Abs($avg)))
}

# encoder delta per tick over the steady window -> proves monotonic counting
if ($CL.Count -ge $n+1) {
    $dl=@(); $dr=@()
    for ($i = $CL.Count-$n; $i -lt $CL.Count; $i++) { $dl += ($CL[$i]-$CL[$i-1]); $dr += ($CR[$i]-$CR[$i-1]) }
    $neg = @($dl + $dr | Where-Object { $_ -lt 0 }).Count
    show (stat $dl) 'dL/tick'
    show (stat $dr) 'dR/tick'
    Write-Output ("negative/zero deltas in window: {0} (0 = strictly monotonic = clean quadrature)" -f $neg)
}
