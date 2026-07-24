# meas_noise.ps1 - quantify the laser measurement noise (the "0.x jump").
# Captures RAW (raw laser distance, cm) for N seconds with the fan at a given PWM
# (default 0 = off, ball resting at bottom = a true stationary target), then reports:
#   - RAW avg/std/ptp           (overall spread)
#   - |delta| sample-to-sample  (the high-freq jitter = the 0.x jumps)
#   - how many samples jump > 0.5cm, > 1.0cm
# This separates "measurement noise" from "real ball motion".
#
# Usage: powershell -ExecutionPolicy Bypass -File meas_noise.ps1 -Port COM10 -Pwm 0 -Sec 15
param([string]$Port="COM10",[int]$Baud=115200,[int]$Pwm=0,[double]$Sec=15)
$sp=New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One')
$sp.ReadTimeout=400; $sp.WriteTimeout=400
try{$sp.Open()}catch{Write-Output ("OPEN FAIL: "+$_.Exception.Message);exit 1}
function Send($c){$sp.Write($c+[char]10);Start-Sleep -Milliseconds 120}
Start-Sleep -Milliseconds 150
$sp.ReadExisting()|Out-Null
if($Pwm -gt 0){ Send "s"; Send ("m"+$Pwm); Start-Sleep -Milliseconds 6000 } else { Send "s" }
# capture RAW
$raw=@(); $buf=""; $sw=[System.Diagnostics.Stopwatch]::StartNew()
while($sw.Elapsed.TotalSeconds -lt $Sec){
  try{$buf+=$sp.ReadExisting()}catch{}
  while($buf.Contains("`n")){ $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i); $buf=$buf.Substring($i+1)
    if($ln -match 'RAW:(-?[\d.]+)'){ $raw+=[double]$matches[1] } }
  Start-Sleep -Milliseconds 8
}
Send "s"; $sp.Close()
$n=$raw.Count
if($n -lt 10){ Write-Output "too few samples ($n)"; exit 0 }
$avg=($raw|Measure-Object -Average).Average
$mn=($raw|Measure-Object -Minimum).Minimum; $mx=($raw|Measure-Object -Maximum).Maximum
$sd=[math]::Sqrt((($raw|%{($_-$avg)*($_-$avg)})|Measure-Object -Sum).Sum/$n)
# sample-to-sample jumps
$d=@(); for($i=1;$i -lt $n;$i++){ $d+=[math]::Abs($raw[$i]-$raw[$i-1]) }
$davg=($d|Measure-Object -Average).Average; $dmax=($d|Measure-Object -Maximum).Maximum
$g05=($d|Where-Object{$_ -gt 0.5}).Count; $g10=($d|Where-Object{$_ -gt 1.0}).Count
Write-Output ("=== MEAS NOISE  (PWM=$Pwm, n=$n) ===")
Write-Output ("RAW: avg={0:N2} std={1:N3}cm ptp={2:N2}cm  (min={3:N1} max={4:N1})" -f $avg,$sd,($mx-$mn),$mn,$mx)
Write-Output ("jump |dRAW|: avg={0:N3}cm max={1:N2}cm   >0.5cm: {2}/{3}  >1.0cm: {4}/{3}" -f $davg,$dmax,$g05,($n-1),$g10)
Write-Output ("interpretation: std<=~0.3 = clean; big jumps (>0.5) = spikes that fool the D-term")
