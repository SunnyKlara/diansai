# tune_step.ps1 - one-shot closed-loop tuning run (open->setup->prespin->engage->capture->stop->analyze)
# Usage:
#   powershell -ExecutionPolicy Bypass -File tune_step.ps1 -Setup "uh3200;kp30;ki0;kd200;f0.5;c4000;l1500;r6" -Target 10 -CaptureS 14
# All Start-Sleep in MILLISECONDS. Always stops the fan at the end.
param(
  [string]$Port = "COM8",
  [int]$Baud = 115200,
  [string]$Setup = "",        # semicolon-separated param commands, sent once while stopped
  [int]$PreSpin = 1800,       # manual PWM to pre-spin the fan (eliminate cold-start), 0 = skip
  [int]$PreSpinMs = 2500,     # pre-spin duration (ms)
  [double]$Target = 10,       # closed-loop target height (cm); engaged via t<Target>
  [int]$CaptureS = 14,        # capture seconds after engage
  [switch]$NoStop             # leave loop running after capture (default: stop)
)

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName = $Port; $sp.BaudRate = $Baud
$sp.Parity = [System.IO.Ports.Parity]::None; $sp.DataBits = 8; $sp.StopBits = [System.IO.Ports.StopBits]::One
$sp.ReadTimeout = 500; $sp.WriteTimeout = 500
try { $sp.Open() } catch { Write-Output ("OPEN FAIL: " + $_.Exception.Message); exit 1 }
function Send($c){ $sp.Write($c + [char]10); Start-Sleep -Milliseconds 120 }

Start-Sleep -Milliseconds 150
Send "s"
if ($Setup -ne "") { foreach($c in $Setup.Split(';')){ if($c.Trim() -ne ""){ Send $c.Trim() } } }
if ($PreSpin -gt 0) { Send ("m" + $PreSpin); Start-Sleep -Milliseconds $PreSpinMs }
$sp.Write(("t{0:N0}" -f $Target) + [char]10)   # engage closed loop

$lines = New-Object System.Collections.ArrayList; $buf = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $CaptureS) {
  try { $buf += $sp.ReadExisting() } catch {}
  while ($buf.Contains("`n")) { $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1); if($ln.Trim() -ne ""){ [void]$lines.Add($ln) } }
  Start-Sleep -Milliseconds 15
}
if (-not $NoStop) { $sp.Write("s" + [char]10) }
Start-Sleep -Milliseconds 100
$sp.Close()

$acks = $lines | Where-Object { $_ -match '>>' }
Write-Output ("ACKS: " + (($acks | Select-Object -Last 4) -join '  |  '))
$H=@(); $P=@(); $R=@(); $A=@()
foreach($ln in $lines){ if($ln -match 'H:(-?[\d.]+) T:.* P:(\d+) M:\d+ D:-?[\d.]+ R:(\d+) RAW:-?[\d.]+ A:(\d+)'){ $H+=[double]$matches[1]; $P+=[double]$matches[2]; $R+=[double]$matches[3]; $A+=[double]$matches[4] } }
$n=$H.Count
Write-Output ("=== parsed=$n  target=$Target  stop=" + (-not $NoStop) + " ===")
# feedback-health gate: if the laser stalls fan-on, A (ms since last valid sample) spikes.
# Tuning PID on stalled feedback is the classic 'going in circles' trap -- flag it loud.
if($A.Count -gt 0){
  $amax=($A|Measure-Object -Maximum).Maximum
  $stall=($A | Where-Object { $_ -gt 150 }).Count
  $pct = if($A.Count){ [math]::Round(100.0*$stall/$A.Count,0) } else { 0 }
  $verdict = if($amax -gt 200){ "*** FEEDBACK STALLING -- fix sensor before tuning PID ***" } else { "feedback OK" }
  Write-Output ("FEEDBACK: maxAge={0}ms  stalled(>150ms)={1}/{2} ({3}%)  -> {4}" -f $amax,$stall,$A.Count,$pct,$verdict)
}
if($n -gt 8){
  $tr=@(); $pr=@(); for($i=0;$i -lt $n;$i+=5){ $tr+=("{0:N1}" -f $H[$i]); $pr+=("{0:N0}" -f $P[$i]) }
  Write-Output ("H trace(~0.5s): " + ($tr -join ' '))
  Write-Output ("P trace(~0.5s): " + ($pr -join ' '))
  $tail=$H[[int]($n*0.5)..($n-1)]
  $av=($tail|Measure-Object -Average).Average; $mn=($tail|Measure-Object -Minimum).Minimum; $mx=($tail|Measure-Object -Maximum).Maximum
  $sd=[math]::Sqrt((($tail|%{($_-$av)*($_-$av)})|Measure-Object -Sum).Sum/$tail.Count)
  Write-Output ("tail H: avg={0:N1} min={1:N1} max={2:N1} std={3:N2} ptp={4:N1}  (target={5})" -f $av,$mn,$mx,$sd,($mx-$mn),$Target)
  Write-Output ("P range: {0}..{1}   RPM range: {2}..{3}" -f ($P|Measure-Object -Minimum).Minimum,($P|Measure-Object -Maximum).Maximum,($R|Measure-Object -Minimum).Minimum,($R|Measure-Object -Maximum).Maximum)
} else { Write-Output "too few samples" }
