# sysid.ps1 - system identification for the levitating-ball plant.
# Goal: replace blind PID guessing with a MEASURED plant model. Drives the board
# over serial, runs an experiment, captures telemetry, fits a model, prints the
# numbers we need (tau_fan, plant gain g, u_hover) AND computed initial gains.
#
# This adds NO control knobs. It only commands existing serial verbs
# (s/m/a/t/uh/kp/ki/kd/f/l/n/x) and parses the existing heartbeat.
#
# Usage:
#   Fan dynamics (ball OUT or held at bottom):
#     powershell -ExecutionPolicy Bypass -File sysid.ps1 -Port COM10 -Mode fan
#   Plant gain via closed-loop step (ball IN tube, controller holds it safe):
#     powershell -ExecutionPolicy Bypass -File sysid.ps1 -Port COM10 -Mode plant
#
# Telemetry line (parsed):
#   H:.. T:.. E:.. P:.. M:.. D:.. R:.. RAW:.. A:.. F:.. ...
param(
  [string]$Port = "COM10",
  [int]$Baud = 115200,
  [ValidateSet("fan","plant")] [string]$Mode = "fan",
  # --- fan mode: warm at L0, step to each level in Levels, capture CaptureS each ---
  [int]$FanWarm = 3000,
  [int[]]$Levels = @(4000,5000,6000),
  [int]$FanCaptureS = 6,
  # --- plant mode: stable controller, then setpoint step From->To (cm) ---
  [string]$StableSetup = "uh3360;kp20;ki0.5;kd6;f0.4;l60;n3200;x3600",
  [double]$From = 15.0,
  [double]$To   = 18.0,
  [int]$SettleS = 6,
  [int]$StepCaptureS = 8
)

$rx = 'H:(-?[\d.]+) T:[-\d.]+ E:(-?[\d.]+) P:(\d+) M:\d+ D:(-?[\d.]+) R:(\d+) RAW:(-?[\d.]+) A:(\d+)'

$sp = $null
function OpenPort(){
  if($script:sp -ne $null){ try{ $script:sp.Close() }catch{} }
  $script:sp = New-Object System.IO.Ports.SerialPort($Port,$Baud,'None',8,'One')
  $script:sp.ReadTimeout=500; $script:sp.WriteTimeout=500
  $script:sp.Open()
}
try { OpenPort } catch { Write-Output ("OPEN FAIL: "+$_.Exception.Message); exit 1 }
function Send($c){
  try{ $script:sp.Write($c+[char]10) }
  catch{ Start-Sleep -Milliseconds 300; try{ OpenPort; $script:sp.Write($c+[char]10) }catch{} }
  Start-Sleep -Milliseconds 120
}

# capture for N seconds, return arrays of @{ms,H,P,D,R} timestamped from t=0.
# Reopens the port on transient USB/serial drops (CH340 quirk) so long runs survive.
function Capture([int]$secs){
  $rows = New-Object System.Collections.ArrayList
  $buf=""; $sw=[System.Diagnostics.Stopwatch]::StartNew()
  while($sw.Elapsed.TotalSeconds -lt $secs){
    try{ $buf += $script:sp.ReadExisting() }
    catch{ Start-Sleep -Milliseconds 200; try{ OpenPort }catch{} ; continue }
    while($buf.Contains("`n")){
      $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
      if($ln -match $rx){
        [void]$rows.Add(@{ ms=[int]$sw.Elapsed.TotalMilliseconds; H=[double]$matches[1];
                           P=[double]$matches[3]; D=[double]$matches[4]; R=[double]$matches[5] })
      }
    }
    Start-Sleep -Milliseconds 8
  }
  return $rows
}

$logdir = Join-Path $PSScriptRoot "logs"; if(-not(Test-Path $logdir)){ New-Item -ItemType Directory -Path $logdir|Out-Null }
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"

Start-Sleep -Milliseconds 150
Send "s"; Start-Sleep -Milliseconds 300

if($Mode -eq "fan"){
  Write-Output "=== SYSID: FAN DYNAMICS (ball OUT or held at bottom) ==="
  $fit = New-Object System.Collections.ArrayList
  Send ("m"+$FanWarm); Start-Sleep -Milliseconds 7000     # warm: four-wire fan cold-start needs ~6s to spin up (measured)
  foreach($L in $Levels){
    Send ("m"+$L)
    $rows = Capture $FanCaptureS
    if($rows.Count -lt 5){ Write-Output ("level $L : too few samples"); continue }
    # first-order fit: R(t) = Rinf - (Rinf-R0)*exp(-t/tau). Estimate Rinf = tail avg,
    # R0 = first sample, tau via 63% rise crossing (robust, no nonlinear solver).
    $R0 = $rows[0].R
    $tailN=[int]($rows.Count*0.3); if($tailN -lt 3){$tailN=3}
    $Rinf=($rows[($rows.Count-$tailN)..($rows.Count-1)] | %{ $_.R } | Measure-Object -Average).Average
    $target = $R0 + 0.632*($Rinf-$R0)
    $tau = 0.0
    for($i=1;$i -lt $rows.Count;$i++){
      $a=$rows[$i-1]; $b=$rows[$i]
      if((($a.R -le $target) -and ($b.R -ge $target)) -or (($a.R -ge $target) -and ($b.R -le $target))){
        $frac = if($b.R-$a.R -ne 0){ ($target-$a.R)/($b.R-$a.R) } else {0}
        $tau = ($a.ms + $frac*($b.ms-$a.ms))/1000.0
        break
      }
    }
    Write-Output ("level {0}: R0={1} Rinf={2:N0}  tau~={3:N2}s  (63% cross)" -f $L,$R0,$Rinf,$tau)
    [void]$fit.Add(@{ pwm=$L; rinf=$Rinf; tau=$tau })
    # log
    $csv=@("t_ms,R,P"); foreach($r in $rows){ $csv+=("{0},{1},{2}" -f $r.ms,$r.R,$r.P) }
    [System.IO.File]::WriteAllLines((Join-Path $logdir ("sysid_fan_$($L)_$stamp.csv")),$csv)
  }
  Send "s"
  # linear fit Rinf = A*PWM + B over the stepped levels
  if($fit.Count -ge 2){
    $m=$fit.Count;$sx=0;$sy=0;$sxx=0;$sxy=0
    foreach($p in $fit){ $sx+=$p.pwm;$sy+=$p.rinf;$sxx+=$p.pwm*$p.pwm;$sxy+=$p.pwm*$p.rinf }
    $A=($m*$sxy-$sx*$sy)/($m*$sxx-$sx*$sx); $B=($sy-$A*$sx)/$m
    $tauAvg=($fit|%{ $_.tau }|Measure-Object -Average).Average
    Write-Output "--- FAN MODEL ---"
    Write-Output ("PWM->RPM: RPM = {0:N3}*PWM + {1:N0}   (cascade feedforward: ya{0:N3} yb{1:N0})" -f $A,$B)
    Write-Output ("tau_fan ~= {0:N2}s (avg)" -f $tauAvg)
    if($tauAvg -gt 0){
      $slew=[math]::Round(($A* (1.0/$tauAvg) /40.0),0)  # rough: keep cmd within fan-trackable rate @40Hz
      Write-Output ("=> suggested slew l ~= matched to fan bandwidth (start l60 as sister rig; raise toward {0} if sluggish)" -f [math]::Max(60,$slew))
      Write-Output ("=> cascade inner Kp(yp) start ~= 0.2..0.5; inner loop bw should be ~5x outer")
    }
  } else { Write-Output "need >=2 levels for PWM->RPM fit" }
}
elseif($Mode -eq "plant"){
  Write-Output "=== SYSID: PLANT GAIN via closed-loop setpoint step (ball IN tube) ==="
  foreach($c in $StableSetup.Split(';')){ if($c.Trim()){ Send $c.Trim() } }
  Send ("m"+3360); Start-Sleep -Milliseconds 2000          # prespin to skip cold-start
  Send ("t{0:N0}" -f $From)                                # engage closed loop at From
  Write-Output ("settling at {0}cm for {1}s ..." -f $From,$SettleS)
  $null = Capture $SettleS
  # baseline just before step
  $base = Capture 2
  $h0 = ($base | %{ $_.H } | Measure-Object -Average).Average
  $p0 = ($base | %{ $_.P } | Measure-Object -Average).Average
  Write-Output ("baseline: H0={0:N2}cm  u_hover(P avg)={1:N0}" -f $h0,$p0)
  # STEP
  Send ("t{0:N0}" -f $To)
  $rows = Capture $StepCaptureS
  Send "s"
  if($rows.Count -lt 8){ Write-Output "too few samples"; $sp.Close(); exit 0 }
  $csv=@("t_ms,H,P,D"); foreach($r in $rows){ $csv+=("{0},{1},{2},{3}" -f $r.ms,$r.H,$r.P,$r.D) }
  [System.IO.File]::WriteAllLines((Join-Path $logdir ("sysid_step_$($From)to$($To)_$stamp.csv")),$csv)

  $step = $To - $From
  $Hfinal = ($rows[($rows.Count-[int]($rows.Count*0.25))..($rows.Count-1)] | %{ $_.H } | Measure-Object -Average).Average
  $Hpeak = ($rows | %{ $_.H } | Measure-Object -Maximum).Maximum
  # rise time 10%->90% of the commanded step (relative to h0)
  $t10=$null;$t90=$null
  foreach($r in $rows){
    $prog = $r.H - $h0
    if($t10 -eq $null -and $prog -ge 0.1*$step){ $t10=$r.ms }
    if($t90 -eq $null -and $prog -ge 0.9*$step){ $t90=$r.ms; break }
  }
  $tr = if($t10 -ne $null -and $t90 -ne $null){ ($t90-$t10)/1000.0 } else { 0 }
  $Mp = if($step -ne 0){ [math]::Max(0,($Hpeak-$h0-$step)/$step) } else {0}     # overshoot fraction
  # settling time to +/-1cm band around target To
  $tset=0
  for($i=$rows.Count-1;$i -ge 0;$i--){ if([math]::Abs($rows[$i].H-$To) -gt 1.0){ $tset=$rows[$i].ms/1000.0; break } }
  Write-Output "--- STEP RESPONSE ($From -> $To cm) ---"
  Write-Output ("rise(10-90%)={0:N2}s  overshoot={1:N0}%  settle(+/-1cm)={2:N2}s  Hfinal={3:N2}  steady_err={4:N2}" -f $tr,(100*$Mp),$tset,$Hfinal,($Hfinal-$To))
  # back out 2nd-order params from overshoot & rise (standard approx)
  if($Mp -gt 0.001 -and $Mp -lt 0.9){
    $lnMp=[math]::Log($Mp)
    $zeta = -$lnMp/[math]::Sqrt([math]::PI*[math]::PI + $lnMp*$lnMp)
  } else { $zeta = 0.9 }   # near-critically damped if no overshoot
  $wn = if($tr -gt 0){ 1.8/$tr } else { 0 }   # tr ~ 1.8/wn for 2nd order
  Write-Output ("=> closed-loop: zeta~={0:N2}  wn~={1:N2} rad/s" -f $zeta,$wn)
  Write-Output ("   (settle ~= 4/(zeta*wn) = {0:N2}s ; target spec <=5s for dynamic-tracking points)" -f $(if($zeta*$wn -gt 0){4.0/($zeta*$wn)}else{0}))
  Write-Output ("NOTE: with known Kp/Kd in this run, plant gain g ~= wn^2 / Kp. Re-run with -To for both directions; average.")
}

Start-Sleep -Milliseconds 100
$sp.Close()
Write-Output "=== done ==="
