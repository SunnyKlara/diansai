# monitor.ps1 - PASSIVE telemetry capture for button-driven rehearsal.
# Does NOT send any control command (judge operates the physical keys). Only sends
# one 'w20' to raise the heartbeat to 50Hz for timing precision, then 'w80' to restore.
# Parses every field, writes CSV, and auto-reports startup timing + steady-state stats
# by reading the TARGET (T:) straight from the stream.
param(
  [string]$Port = "COM10",
  [int]$Baud = 115200,
  [int]$Sec = 30,
  [double]$Band = 2.0,        # settle band (cm) around target
  [switch]$Track,             # tracking mode: t0 = first target(T) change, measure settle to new target
  [string]$Tag = "rehearsal"
)
$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName=$Port; $sp.BaudRate=$Baud
$sp.Parity=[System.IO.Ports.Parity]::None; $sp.DataBits=8; $sp.StopBits=[System.IO.Ports.StopBits]::One
$sp.ReadTimeout=500; $sp.WriteTimeout=500
try { $sp.Open() } catch { Write-Output ("OPEN FAIL: "+$_.Exception.Message); exit 1 }

$sp.Write("w20" + [char]10); Start-Sleep -Milliseconds 100   # telemetry-rate only (no control change)

$tok=[regex]'([A-Za-z]+):(0x[0-9A-Fa-f]+|-?[\d.]+)'
$rows = New-Object System.Collections.ArrayList
$buf=""
$sw=[System.Diagnostics.Stopwatch]::StartNew()
Write-Output ("MONITOR start: $Sec s @ $Port  (press keys now)")
while($sw.Elapsed.TotalSeconds -lt $Sec){
  try{ $buf+=$sp.ReadExisting() }catch{}
  while($buf.Contains("`n")){
    $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
    if($ln -notmatch 'H:' -or $ln -notmatch 'P:'){ continue }
    $m=[ordered]@{}; foreach($mm in $tok.Matches($ln)){ $m[$mm.Groups[1].Value]=$mm.Groups[2].Value }
    if($m.Contains('H')){ [void]$rows.Add(@{ ms=[int]$sw.Elapsed.TotalMilliseconds; m=$m }) }
  }
  Start-Sleep -Milliseconds 5
}
$sp.Write("w80" + [char]10); Start-Sleep -Milliseconds 80
$sp.Close()

$n=$rows.Count
Write-Output ("captured=$n samples over $Sec s")
if($n -lt 10){ Write-Output "too few samples"; exit 0 }

# write CSV (full fields)
$logdir=Join-Path $PSScriptRoot "logs"; if(-not(Test-Path $logdir)){ New-Item -ItemType Directory -Path $logdir | Out-Null }
$stamp=Get-Date -Format "yyyyMMdd_HHmmss"
$csvOut=Join-Path $logdir ("mon_${Tag}_$stamp.csv")
$keys=@('H','T','E','P','M','D','R','RAW','A','F','PRE','CAS','RS','CM','FH')
$csv=New-Object System.Collections.ArrayList
[void]$csv.Add("t_ms,"+($keys -join ','))
foreach($r in $rows){ $line=""+$r.ms; foreach($k in $keys){ $line+=","+($(if($r.m.Contains($k)){$r.m[$k]}else{''})) }; [void]$csv.Add($line) }
[System.IO.File]::WriteAllLines($csvOut,$csv)
Write-Output ("csv="+$csvOut)

# arrays
$T_ms=@($rows|%{$_.ms})
$H=@($rows|%{[double]$_.m['H']})
$Tg=@($rows|%{[double]$_.m['T']})
$P=@($rows|%{[double]$_.m['P']})
$tgt=$Tg[$n-1]   # target at end of capture

if($Track){
  # tracking mode: t0 = first sample where target T changes from its initial value
  $t0idx=-1; $tStart=$Tg[0]
  for($i=0;$i -lt $n;$i++){ if([math]::Abs($Tg[$i]-$tStart) -gt 0.3){ $t0idx=$i; break } }
  if($t0idx -lt 0){ Write-Output ("TRACK: target never changed (stayed {0}); press K2 during capture" -f $tStart) }
  else {
    $t0=$T_ms[$t0idx]; $newT=$Tg[$t0idx]
    # settle = first time after t0 that |H-newT|<=1 and holds for >=1.5s
    $settle=-1
    for($i=$t0idx;$i -lt $n;$i++){
      if([math]::Abs($H[$i]-$tgt) -le 1.0){
        $ok=$true; for($j=$i;$j -lt $n;$j++){ if($T_ms[$j]-$T_ms[$i] -gt 1500){break}; if([math]::Abs($H[$j]-$tgt) -gt 1.0){$ok=$false;break} }
        if($ok){ $settle=$i; break }
      }
    }
    $oR=($H[$t0idx..($n-1)]|Measure-Object -Maximum).Maximum
    $uR=($H[$t0idx..($n-1)]|Measure-Object -Minimum).Minimum
    if($settle -ge 0){
      Write-Output ("TRACK: step {0}->{1}cm @{2:N1}s  enter&hold +/-1cm @{3:N1}s  => settling {4:N1}s  (overshoot peak {5:N1} / dip {6:N1})" -f $tStart,$tgt,($t0/1000.0),($T_ms[$settle]/1000.0),(($T_ms[$settle]-$t0)/1000.0),$oR,$uR)
    } else {
      Write-Output ("TRACK: step {0}->{1}cm @{2:N1}s  NEVER held +/-1cm for 1.5s (peak {3:N1}/dip {4:N1})  => FAILS <=5s" -f $tStart,$tgt,($t0/1000.0),$oR,$uR)
    }
  }
}

# startup: first index P jumps above 500 (engage/boost), then first time H enters band AND stays
$engage=-1
for($i=0;$i -lt $n;$i++){ if($P[$i] -gt 500){ $engage=$i; break } }
if($engage -ge 0){
  $tEng=$T_ms[$engage]
  $settle=-1
  for($i=$engage;$i -lt $n;$i++){
    if([math]::Abs($H[$i]-$tgt) -le $Band){
      # require staying in band for >=1.5s
      $ok=$true; for($j=$i;$j -lt $n;$j++){ if($T_ms[$j]-$T_ms[$i] -gt 1500){break}; if([math]::Abs($H[$j]-$tgt) -gt $Band){$ok=$false;break} }
      if($ok){ $settle=$i; break }
    }
  }
  if($settle -ge 0){
    Write-Output ("STARTUP: engage@{0:N1}s  settled(+/-{1}cm of {2})@{3:N1}s  => time-to-stable {4:N1}s" -f ($tEng/1000.0),$Band,$tgt,($T_ms[$settle]/1000.0),(($T_ms[$settle]-$tEng)/1000.0))
  } else { Write-Output ("STARTUP: engage@{0:N1}s  but never held +/-{1}cm of {2} for 1.5s in window" -f ($tEng/1000.0),$Band,$tgt) }
} else { Write-Output "no engage detected (P stayed low -> fan never started; press K3?)" }

# H trace (downsampled) + steady-state over last 8s
$tr=@(); for($i=0;$i -lt $n;$i+=[math]::Max(1,[int]($n/30))){ $tr+=("{0:N1}" -f $H[$i]) }
Write-Output ("H trace: "+($tr -join ' '))
$k0=0; for($i=0;$i -lt $n;$i++){ if($T_ms[$n-1]-$T_ms[$i] -le 8000){ $k0=$i; break } }
$tail=$H[$k0..($n-1)]
$av=($tail|Measure-Object -Average).Average
$sd=[math]::Sqrt((($tail|%{($_-$av)*($_-$av)})|Measure-Object -Sum).Sum/$tail.Count)
$mn=($tail|Measure-Object -Minimum).Minimum; $mx=($tail|Measure-Object -Maximum).Maximum
$in1=100.0*(($tail|?{[math]::Abs($_-$tgt)-le 1.0}).Count)/$tail.Count
$in2=100.0*(($tail|?{[math]::Abs($_-$tgt)-le 2.0}).Count)/$tail.Count
Write-Output ("steady(last 8s vs target {0}): avg={1:N2} std={2:N2} min={3:N1} max={4:N1} +/-1cm={5:N0}% +/-2cm={6:N0}%" -f $tgt,$av,$sd,$mn,$mx,$in1,$in2)
