# watch_log.ps1 - passive observe: capture N seconds of heartbeats to CSV WITHOUT
# sending any command (does not reset/re-engage the running loop). Then analyze.py
# can read the CSV. Use this to inspect the currently-running controller live.
# Usage: powershell -ExecutionPolicy Bypass -File watch_log.ps1 -Port COM10 -Watch 15
param([string]$Port="COM10",[int]$Baud=115200,[double]$Watch=15.0,[string]$Tag="watch")

$sp = New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One')
$sp.ReadTimeout=400; $sp.WriteTimeout=400
try { $sp.Open() } catch { Write-Output ("OPEN FAIL: "+$_.Exception.Message); exit 1 }
Start-Sleep -Milliseconds 150
$sp.ReadExisting() | Out-Null

$lines = New-Object System.Collections.ArrayList
$buf=""
$sw=[System.Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt $Watch){
  try { $buf += $sp.ReadExisting() } catch {}
  while($buf.Contains("`n")){
    $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
    if($ln.Trim() -ne ""){ [void]$lines.Add(@{ ms=[int]$sw.Elapsed.TotalMilliseconds; txt=$ln }) }
  }
  Start-Sleep -Milliseconds 10
}
$sp.Close()

# parse: H T E P M D R RAW A  (+ optional RS for cascade tracking)
$rx='H:(-?[\d.]+) T:(-?[\d.]+) E:(-?[\d.]+) P:(\d+) M:\d+ D:(-?[\d.]+) R:(\d+) RAW:(-?[\d.]+) A:(\d+)'
$rxRS='RS:(-?\d+)'
$csv=New-Object System.Collections.ArrayList
[void]$csv.Add("t_ms,H,T,P,D,RPM,RAW,age,RS")
$n=0
foreach($rec in $lines){
  if($rec.txt -match $rx){
    $rs=0; if($rec.txt -match $rxRS){ $rs=[int]$matches[1] }
    # note: $matches got overwritten by rxRS; re-match main to be safe
    if($rec.txt -match $rx){
      $rs2=0; if($rec.txt -match $rxRS){ $rs2=[int]$matches[1] }
    }
  }
}
# simpler robust second pass
$n=0
foreach($rec in $lines){
  $m1=[regex]::Match($rec.txt,$rx)
  if($m1.Success){
    $rs=0; $m2=[regex]::Match($rec.txt,$rxRS); if($m2.Success){ $rs=[int]$m2.Groups[1].Value }
    [void]$csv.Add(("{0},{1},{2},{3},{4},{5},{6},{7},{8}" -f $rec.ms,$m1.Groups[1].Value,$m1.Groups[2].Value,$m1.Groups[4].Value,$m1.Groups[5].Value,$m1.Groups[6].Value,$m1.Groups[7].Value,$m1.Groups[8].Value,$rs))
    $n++
  }
}
$logdir=Join-Path $PSScriptRoot "logs"; if(-not(Test-Path $logdir)){ New-Item -ItemType Directory -Path $logdir|Out-Null }
$stamp=Get-Date -Format "yyyyMMdd_HHmmss"
$out=Join-Path $logdir ("{0}_{1}.csv" -f $Tag,$stamp)
[System.IO.File]::WriteAllLines($out,$csv)
Write-Output ("WATCH parsed=$n  csv=$out")
