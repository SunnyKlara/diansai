# sweep_rpm.ps1 - characterize the fan actuator: PWM -> RPM (for the cascade inner loop)
# Usage:
#   powershell -ExecutionPolicy Bypass -File sweep_rpm.ps1 -From 1500 -To 6000 -Step 250
#
# Why: the cascade inner loop (firmware 'y1') needs the PWM->RPM map
#   rpm_sp = A*PWM + B  (set on-board with 'ya<A>' / 'yb<B>').
#   This script drives the fan open-loop (manual 'm') across the closed-loop PWM
#   band, lets each step settle (4-wire fans take ~3s to reach speed), averages
#   the tach RPM, then linear-fits A and B and prints the exact commands to paste.
#
# IMPORTANT: this is a BENCH characterization of the fan, independent of the ball.
#   Run with the ball removed (or expect it to ride the airflow) -- we only care
#   about how RPM tracks PWM. The fan is always stopped at the end.
param(
  [string]$Port = "COM8",
  [int]$Baud = 115200,
  [int]$From = 1500,          # sweep start PWM (~ idle floor g_pwm_min)
  [int]$To = 6000,            # sweep end PWM   (~ thrust ceiling g_pwm_max)
  [int]$Step = 250,           # PWM increment
  [int]$SettleMs = 3000,      # dwell at each step before sampling (fan spin-up)
  [int]$SampleMs = 1200       # RPM averaging window per step
)

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName = $Port; $sp.BaudRate = $Baud
$sp.Parity = [System.IO.Ports.Parity]::None; $sp.DataBits = 8; $sp.StopBits = [System.IO.Ports.StopBits]::One
$sp.ReadTimeout = 500; $sp.WriteTimeout = 500
try { $sp.Open() } catch { Write-Output ("OPEN FAIL: " + $_.Exception.Message); exit 1 }
function Send($c){ $sp.Write($c + [char]10); Start-Sleep -Milliseconds 120 }

Start-Sleep -Milliseconds 150
Send "s"

$pwmArr=@(); $rpmArr=@()
Write-Output ("PWM`tRPM_avg`tRPM_std`tn")
for($pwm=$From; $pwm -le $To; $pwm+=$Step){
  Send ("m" + $pwm)                     # manual open-loop PWM
  Start-Sleep -Milliseconds $SettleMs   # let speed settle
  # sample RPM for SampleMs
  $rs=@(); $buf=""; $sw=[System.Diagnostics.Stopwatch]::StartNew()
  while($sw.Elapsed.TotalMilliseconds -lt $SampleMs){
    try { $buf += $sp.ReadExisting() } catch {}
    while($buf.Contains("`n")){
      $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
      if($ln -match 'R:(\d+)'){ $rs += [double]$matches[1] }
    }
    Start-Sleep -Milliseconds 10
  }
  if($rs.Count -gt 0){
    $ra=($rs|Measure-Object -Average).Average
    $rsd=[math]::Sqrt((($rs|%{($_-$ra)*($_-$ra)})|Measure-Object -Sum).Sum/$rs.Count)
    $pwmArr += $pwm; $rpmArr += $ra
    Write-Output ("{0}`t{1:N0}`t{2:N0}`t{3}" -f $pwm,$ra,$rsd,$rs.Count)
  } else {
    Write-Output ("{0}`tNO-TACH`t-`t0" -f $pwm)
  }
}
$sp.Write("s" + [char]10); Start-Sleep -Milliseconds 100; $sp.Close()

# --- linear fit RPM = A*PWM + B over points where tach responded ---
$m=$pwmArr.Count
if($m -lt 2){ Write-Output "not enough tach points -> check PC6 wiring / TACH_ENABLE"; exit 0 }
$sx=0.0;$sy=0.0;$sxx=0.0;$sxy=0.0
for($i=0;$i -lt $m;$i++){ $x=$pwmArr[$i];$y=$rpmArr[$i];$sx+=$x;$sy+=$y;$sxx+=$x*$x;$sxy+=$x*$y }
$den=($m*$sxx-$sx*$sx)
if([math]::Abs($den) -lt 1e-9){ Write-Output "degenerate fit"; exit 0 }
$A=($m*$sxy-$sx*$sy)/$den
$B=($sy-$A*$sx)/$m
# R^2
$ybar=$sy/$m; $ssr=0.0;$sst=0.0
for($i=0;$i -lt $m;$i++){ $f=$A*$pwmArr[$i]+$B; $ssr+=($rpmArr[$i]-$f)*($rpmArr[$i]-$f); $sst+=($rpmArr[$i]-$ybar)*($rpmArr[$i]-$ybar) }
$r2 = if($sst -gt 0){ 1.0-$ssr/$sst } else { 0 }
Write-Output ("=== fit: RPM = {0:N3}*PWM + {1:N0}   R^2={2:N4}  (n={3}) ===" -f $A,$B,$r2,$m)
Write-Output ("paste on-board:  ya{0:N3}   yb{1:N0}   (then y1 to enable cascade)" -f $A,$B)
