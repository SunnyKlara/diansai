# find_hover.ps1 - auto-calibrate the lift-off / hover PWM for the CURRENT battery state.
# The fan is PWM-driven from a battery: actual thrust = duty * battery_voltage, so the
# hover PWM (and lift-curve steepness) shift with battery charge. This sweeps PWM upward
# from a low idle until the ball just leaves the bottom (lift-off), which is the hover
# region for the current battery. Output -> set uh + run band around it.
#
# Usage: powershell -ExecutionPolicy Bypass -File find_hover.ps1 -Port COM10
param(
  [string]$Port = "COM10",
  [int]$Baud = 115200,
  [int]$WarmPwm = 3000,      # idle warm to clear cold-start deadband
  [int]$WarmMs = 6000,
  [int]$Start = 3050,        # sweep start (below any plausible lift-off)
  [int]$Stop = 3600,         # sweep ceiling (safety)
  [int]$Step = 15,           # PWM step
  [int]$DwellMs = 1300,      # hold each step (let ball respond)
  [double]$LiftCm = 3.0      # H above this => ball has left the bottom => lift-off
)
$rx = 'H:(-?[\d.]+).*?R:(\d+) RAW:(-?[\d.]+)'
$sp = $null
function OpenPort(){ if($script:sp){ try{$script:sp.Close()}catch{} } ; $script:sp=New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One'); $script:sp.ReadTimeout=400; $script:sp.WriteTimeout=400; $script:sp.Open() }
try { OpenPort } catch { Write-Output ("OPEN FAIL: "+$_.Exception.Message); exit 1 }
function Send($c){ try{$script:sp.Write($c+[char]10)}catch{Start-Sleep -Milliseconds 250; try{OpenPort;$script:sp.Write($c+[char]10)}catch{}}; Start-Sleep -Milliseconds 120 }
# read latest H over a dwell window, return max H seen
function DwellMaxH([int]$ms){
  $buf=""; $sw=[System.Diagnostics.Stopwatch]::StartNew(); $maxh=-99.0; $lastr=0
  while($sw.Elapsed.TotalMilliseconds -lt $ms){
    try{ $buf += $script:sp.ReadExisting() }catch{ Start-Sleep -Milliseconds 150; try{OpenPort}catch{}; continue }
    while($buf.Contains("`n")){ $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i); $buf=$buf.Substring($i+1)
      if($ln -match $rx){ $h=[double]$matches[1]; if($h -gt $maxh){$maxh=$h}; $lastr=[int]$matches[2] } }
    Start-Sleep -Milliseconds 10
  }
  return @($maxh,$lastr)
}

Start-Sleep -Milliseconds 150
Send "s"; Start-Sleep -Milliseconds 300
Write-Output "=== FIND HOVER (current battery) ==="
Send ("m"+$WarmPwm); Start-Sleep -Milliseconds $WarmMs    # warm

$liftoff = 0
for($p=$Start; $p -le $Stop; $p+=$Step){
  Send ("m"+$p)
  $r = DwellMaxH $DwellMs
  $maxh = $r[0]; $rpm = $r[1]
  Write-Output ("PWM={0}  maxH={1:N1}cm  RPM={2}" -f $p,$maxh,$rpm)
  if($maxh -ge $LiftCm){ $liftoff = $p; break }
}
Send "s"
$sp.Close()
if($liftoff -gt 0){
  Write-Output "-----------------------------------------"
  Write-Output ("LIFT-OFF PWM = {0}  (= current hover region)" -f $liftoff)
  Write-Output ("=> suggest: uh{0}  n{1}  x{2}" -f $liftoff, ($liftoff-80), ($liftoff+70))
} else {
  Write-Output "no lift-off detected up to ceiling; raise -Stop or check fan/battery"
}
Write-Output "=== done ==="
