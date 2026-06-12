# analyze_log.ps1 - offline analyzer for the rolling live_session.csv produced by serial_logger.ps1.
# Usage:
#   analyze_log.ps1                 # summary + target-change events + last 10s steady stats
#   analyze_log.ps1 -Last 20        # steady stats over last 20s
#   analyze_log.ps1 -Segments       # per-segment stats (between each target change) + settle times
param(
  [string]$Csv = "",
  [double]$Last = 10.0,     # window (s) for steady stats at the end
  [switch]$Segments         # also print per-target-segment stats with settle-to-+/-1cm times
  ,[switch]$Disturb         # disturbance-rejection: peak excursion in window + recovery time to +/-2cm
)
if ($Csv -eq "") { $Csv = Join-Path (Join-Path $PSScriptRoot "logs") "live_session.csv" }
if (-not (Test-Path $Csv)) { Write-Output "no log: $Csv"; exit 1 }

$rows = New-Object System.Collections.ArrayList
$hdr = $null
# shared read so we can analyze while the logger is still writing the same file
$fs = New-Object System.IO.FileStream($Csv,[System.IO.FileMode]::Open,[System.IO.FileAccess]::Read,[System.IO.FileShare]::ReadWrite)
$sr = New-Object System.IO.StreamReader($fs)
while($null -ne ($line = $sr.ReadLine())){
  if($null -eq $hdr){ $hdr = $line.Split(','); continue }
  $p = $line.Split(',')
  if($p.Length -lt $hdr.Length){ continue }
  $o = @{}; for($i=0;$i -lt $hdr.Length;$i++){ $o[$hdr[$i]] = $p[$i] }
  [void]$rows.Add($o)
}
$sr.Close(); $fs.Close()
$n = $rows.Count
if($n -lt 5){ Write-Output "too few rows ($n)"; exit 0 }

$ms  = @($rows | ForEach-Object { [double]$_['elapsed_ms'] })
$H   = @($rows | ForEach-Object { [double]$_['H'] })
$T   = @($rows | ForEach-Object { [double]$_['T'] })
$P   = @($rows | ForEach-Object { [double]$_['P'] })
$R   = @($rows | ForEach-Object { [double]$_['R'] })
$RS  = @($rows | ForEach-Object { if($_['RS'] -ne '' -and $null -ne $_['RS']){[double]$_['RS']}else{0.0} })
$dur = ($ms[$n-1]-$ms[0])/1000.0
Write-Output ("LOG: $n samples over {0:N1}s  (~{1:N1} Hz)  file=$Csv" -f $dur, ($n/[math]::Max($dur,0.001)))

# --- target-change events ---
$events = @()
for($i=1;$i -lt $n;$i++){ if([math]::Abs($T[$i]-$T[$i-1]) -gt 0.3){ $events += $i } }
if($events.Count){
  Write-Output ("target changes: " + (($events | ForEach-Object { "{0:N1}s:{1}->{2}cm" -f ($ms[$_]/1000.0),$T[$_-1],$T[$_] }) -join '  '))
} else { Write-Output ("target constant = {0}cm" -f $T[$n-1]) }

function Stats($idx0,$idx1,$tgt,$label){
  if($idx1 -le $idx0){ return }
  $seg=$H[$idx0..$idx1]
  $av=($seg|Measure-Object -Average).Average
  $sd=[math]::Sqrt((($seg|%{($_-$av)*($_-$av)})|Measure-Object -Sum).Sum/$seg.Count)
  $mn=($seg|Measure-Object -Minimum).Minimum; $mx=($seg|Measure-Object -Maximum).Maximum
  $in1=100.0*(($seg|?{[math]::Abs($_-$tgt)-le 1.0}).Count)/$seg.Count
  $in2=100.0*(($seg|?{[math]::Abs($_-$tgt)-le 2.0}).Count)/$seg.Count
  Write-Output ("{0}: tgt={1} avg={2:N2} std={3:N2} min={4:N1} max={5:N1} +/-1cm={6:N0}% +/-2cm={7:N0}% (n={8})" -f $label,$tgt,$av,$sd,$mn,$mx,$in1,$in2,$seg.Count)
  $ps=$P[$idx0..$idx1]; $rs2=$R[$idx0..$idx1]; $rss=$RS[$idx0..$idx1]
  $pmax=($ps|Measure-Object -Maximum).Maximum; $pavg=($ps|Measure-Object -Average).Average
  $pinhi=100.0*(($ps|?{$_ -ge 4095}).Count)/$ps.Count
  $ravg=($rs2|Measure-Object -Average).Average; $rsavg=($rss|Measure-Object -Average).Average
  Write-Output ("      PWM avg={0:N0} max={1:N0} (>=4095: {2:N0}% of time)  RPM avg={3:N0}  rpm_sp avg={4:N0}" -f $pavg,$pmax,$pinhi,$ravg,$rsavg)
}

# --- last-N-seconds steady stats vs current target ---
$k0=0; for($i=0;$i -lt $n;$i++){ if($ms[$n-1]-$ms[$i] -le $Last*1000.0){ $k0=$i; break } }
Stats $k0 ($n-1) $T[$n-1] ("last {0:N0}s" -f $Last)
# downsampled H(P) trace over the window: "H@t" pairs, ~24 points
$np=24; $stepn=[math]::Max(1,[int](($n-$k0)/$np))
$tr=@(); for($i=$k0;$i -lt $n;$i+=$stepn){ $tr+=("{0:N1}@{1:N0}s" -f $H[$i],($ms[$i]/1000.0)) }
Write-Output ("H trace: " + ($tr -join '  '))

# --- disturbance rejection: largest excursion in window + recovery time to +/-2cm ---
if($Disturb){
  $tg=$T[$n-1]
  $pk=$k0; $pkdev=0.0
  for($i=$k0;$i -lt $n;$i++){ $d=[math]::Abs($H[$i]-$tg); if($d -gt $pkdev){ $pkdev=$d; $pk=$i } }
  $dir = if($H[$pk] -ge $tg){"up"}else{"down"}
  Write-Output ("disturbance: peak excursion {0:N1}cm ({1}, H={2:N1} vs tgt {3}) @{4:N1}s" -f $pkdev,$dir,$H[$pk],$tg,($ms[$pk]/1000.0))
  $rec2=-1
  for($i=$pk;$i -lt $n;$i++){
    if([math]::Abs($H[$i]-$tg) -le 2.0){
      $ok=$true; for($j=$i;$j -lt $n;$j++){ if($ms[$j]-$ms[$i] -gt 1500){break}; if([math]::Abs($H[$j]-$tg) -gt 2.0){$ok=$false;break} }
      if($ok){ $rec2=$i; break }
    }
  }
  $rec1=-1
  for($i=$pk;$i -lt $n;$i++){
    if([math]::Abs($H[$i]-$tg) -le 1.0){
      $ok=$true; for($j=$i;$j -lt $n;$j++){ if($ms[$j]-$ms[$i] -gt 1500){break}; if([math]::Abs($H[$j]-$tg) -gt 1.0){$ok=$false;break} }
      if($ok){ $rec1=$i; break }
    }
  }
  if($rec2 -ge 0){ Write-Output ("  recover->+/-2cm: {0:N1}s after peak" -f (($ms[$rec2]-$ms[$pk])/1000.0)) } else { Write-Output "  recover->+/-2cm: not recovered in window" }
  if($rec1 -ge 0){ Write-Output ("  recover->+/-1cm: {0:N1}s after peak" -f (($ms[$rec1]-$ms[$pk])/1000.0)) } else { Write-Output "  recover->+/-1cm: not held in window" }
}

# --- per-segment + settle times ---
if($Segments){
  $bounds = @(0) + $events + @($n)
  for($s=0;$s -lt $bounds.Count-1;$s++){
    $a=$bounds[$s]; $b=$bounds[$s+1]-1; $tg=$T[$a]
    Stats $a $b $tg ("seg{0} [{1:N1}-{2:N1}s]" -f $s,($ms[$a]/1000.0),($ms[$b]/1000.0))
    # settle to +/-1cm of this segment's target, measured from segment start, held >=1.5s
    $settle=-1
    for($i=$a;$i -le $b;$i++){
      if([math]::Abs($H[$i]-$tg) -le 1.0){
        $ok=$true; for($j=$i;$j -le $b;$j++){ if($ms[$j]-$ms[$i] -gt 1500){break}; if([math]::Abs($H[$j]-$tg) -gt 1.0){$ok=$false;break} }
        if($ok){ $settle=$i; break }
      }
    }
    if($settle -ge 0){ Write-Output ("        settle->+/-1cm: {0:N1}s after step" -f (($ms[$settle]-$ms[$a])/1000.0)) }
    else { Write-Output ("        settle->+/-1cm: NEVER held 1.5s in segment") }
  }
}
