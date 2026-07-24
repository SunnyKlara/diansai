# pidcalc.ps1 - model-based PID gain CALCULATOR (pole placement), not guessing.
# Feeds the MEASURED plant model + design targets -> prints concrete values for
# every knob, each with the formula used so it is auditable.
#
# Plant model (near hover): double-integrator  y'' = g * du   (du = PWM - u_hover)
#   PD law: du = Kp*e - Kd*y'     => closed loop  s^2 + g*Kd*s + g*Kp = 0
#   match  s^2 + 2*zeta*wn*s + wn^2  =>  Kp = wn^2/g ,  Kd = 2*zeta*wn/g
#
# g (dynamic gain, cm/s^2 per PWM-count) is the linchpin. Either pass -G directly,
# or pass -StepCsv (a logged closed-loop step) + -KpRun to estimate g from the
# step's overshoot & oscillation period (log-decrement), which is robust.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File pidcalc.ps1 -StepCsv logs\sysid_step_15to18_*.csv -KpRun 20 -Target 18
#   powershell -ExecutionPolicy Bypass -File pidcalc.ps1 -G 0.9
param(
  [double]$G = 0,                 # plant dynamic gain (cm/s^2 / PWM-count); 0 => estimate from CSV
  [string]$StepCsv = "",          # logged step CSV to estimate G from
  [double]$KpRun = 20,            # Kp that was active during the logged step
  [double]$Target = 18,           # the step's target (cm), for settle reference
  # --- design targets (engineering choices, stated not guessed) ---
  [double]$Zeta = 0.75,           # damping ratio (0.7-0.8 = minimal overshoot)
  [double]$SettleTs = 2.0,        # desired settle time (s); ts ~= 4/(zeta*wn)
  # --- measured model constants (our rig) ---
  [double]$TauFan = 0.42,         # fan time constant (s)
  [double]$Sigma = 0.26,          # height noise floor (cm)
  [double]$Tloop = 0.024,         # control loop period (s) ~42Hz
  [double]$Hover = 3420,          # measured u_hover (PWM)
  [double]$StaticGain = 22.7      # PWM per cm (static)
)

# ---- estimate G from a logged step via log-decrement, if requested ----
if($G -le 0 -and $StepCsv -ne ""){
  $path = $StepCsv
  if(-not (Test-Path $path)){
    $cand = Get-ChildItem -Path (Join-Path $PSScriptRoot "logs") -Filter (Split-Path $StepCsv -Leaf) -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if($cand){ $path = $cand.FullName }
  }
  if(-not (Test-Path $path)){ Write-Output ("StepCsv not found: "+$StepCsv); exit 1 }
  $rows = Import-Csv $path
  $t=@(); $h=@()
  foreach($r in $rows){
    $t += [double]$r.t_ms/1000.0
    $hv = if($r.PSObject.Properties.Name -contains 'H_cm'){ $r.H_cm } else { $r.H }
    $h += [double]$hv
  }
  $n=$h.Count
  if($n -lt 10){ Write-Output "step CSV too short"; exit 1 }
  $y0 = $h[0]
  $yf = ($h[([int]($n*0.75))..($n-1)] | Measure-Object -Average).Average
  $peak = ($h | Measure-Object -Maximum).Maximum
  $Mp = if(($yf-$y0) -ne 0){ ($peak-$yf)/($yf-$y0) } else { 0 }
  # zero-crossings of (h-yf) -> half-periods
  $zc=0; $tprev=$null; $tfirst=$null; $tlast=$null; $prev=$h[0]-$yf
  for($i=1;$i -lt $n;$i++){
    $cur=$h[$i]-$yf
    if(($prev -le 0 -and $cur -gt 0) -or ($prev -ge 0 -and $cur -lt 0)){
      if($tfirst -eq $null){ $tfirst=$t[$i] }; $tlast=$t[$i]; $zc++
    }
    $prev=$cur
  }
  $Td = if($zc -ge 2){ 2.0*($tlast-$tfirst)/($zc-1) } else { 0 }
  Write-Output ("--- G estimate from $path ---")
  Write-Output ("y0={0:N2} yf={1:N2} peak={2:N2}  Mp={3:P0}  zc={4}  Td={5:N2}s" -f $y0,$yf,$peak,$Mp,$zc,$Td)
  if($Mp -gt 0.02 -and $Mp -lt 0.95 -and $Td -gt 0){
    $lnMp=[math]::Log($Mp)
    $zEst = -$lnMp/[math]::Sqrt([math]::PI*[math]::PI+$lnMp*$lnMp)
    $wd = 2.0*[math]::PI/$Td
    $wnEst = $wd/[math]::Sqrt(1.0-$zEst*$zEst)
    $G = $wnEst*$wnEst/$KpRun
    Write-Output ("zeta_meas={0:N2}  wd={1:N2}  wn_meas={2:N2} rad/s  => g = wn^2/KpRun = {3:N3} cm/s^2 per count" -f $zEst,$wd,$wnEst,$G)
  } else {
    Write-Output "overshoot/period not clean enough for log-decrement; pass -G explicitly or re-run a cleaner step"
    exit 1
  }
}

if($G -le 0){ Write-Output "need plant gain G (>0). Pass -G or -StepCsv."; exit 1 }

# ---- pole placement: outer-loop PD ----
$wn = 4.0/($Zeta*$SettleTs)
$Kp = $wn*$wn/$G
$Kd = 2.0*$Zeta*$wn/$G
# weak integral: place integral corner ~ wn/5 (slow, just kills static drift)
$Ki = ($Kp*$wn/5.0)/10.0    # conservative; verify drift slope ~0 then trim
# velocity EMA: cutoff wf ~ 5*wn (above control band so little phase loss);
# EMA y=a*prev+(1-a)*x has corner wc=(1-a)/(a*Tloop) => solve a
$wf = 5.0*$wn
$alpha = 1.0/(1.0+$wf*$Tloop)
# slew: let PWM traverse the working band within ~one fan tau (don't outrun the fan)
$bandPWM = 300.0
$slew = [math]::Round($bandPWM*$Tloop/$TauFan,0)
# run band: hover +/- (Kp*maxErr + margin); maxErr ~ 6cm capture
$nlo = [math]::Round($Hover - ($Kp*6 + 60),0)
$xhi = [math]::Round($Hover + ($Kp*6 + 120),0)
# cascade inner (RPM) loop: bandwidth ~5x outer; first-order fan => yp ~ (wi*tau)/Kfan_norm
$wi = 5.0*$wn
$yp = [math]::Round(($wi*$TauFan)/2.0,3)   # /2.0 ~ PWM->RPM slope normalization, refine on rig

Write-Output ""
Write-Output "=================== COMPUTED PARAMETERS ==================="
Write-Output ("MODEL : g={0:N3} cm/s^2/count  tau_fan={1:N2}s  sigma={2:N2}cm  Tloop={3:N3}s  hover={4:N0}" -f $G,$TauFan,$Sigma,$Tloop,$Hover)
Write-Output ("DESIGN: zeta={0:N2}  settle_target={1:N1}s  => wn={2:N2} rad/s" -f $Zeta,$SettleTs,$wn)
Write-Output "-----------------------------------------------------------"
Write-Output ("uh = {0:N0}          (measured hover; trim by err_avg*22.7)" -f $Hover)
Write-Output ("kp = {0:N1}          (= wn^2/g)" -f $Kp)
Write-Output ("kd = {0:N1}          (= 2*zeta*wn/g)" -f $Kd)
Write-Output ("ki = {0:N2}          (weak, corner~wn/5; verify drift~0 then trim)" -f $Ki)
Write-Output ("f  = {0:N2}          (vel EMA, corner~5*wn={1:N1}rad/s; raise if D std still high)" -f $alpha,$wf)
Write-Output ("l  = {0:N0}            (slew: traverse {1:N0}-count band in ~1 tau_fan)" -f $slew,$bandPWM)
Write-Output ("n  = {0:N0} / x = {1:N0}   (run band = hover +/- control authority)" -f $nlo,$xhi)
Write-Output ("cascade: ya2.0 yb-167 (measured) ; yp = {0:N3} (inner bw~5x outer)" -f $yp)
Write-Output "==========================================================="
Write-Output "NOTE: these are computed STARTING values. Flash, run tune_step, verify"
Write-Output "      std/D-std/drift; expect <=1-2 trim rounds (model is nonlinear+drifts)."
