# tune_step.ps1 - one-shot closed-loop tuning run (open->setup->prespin->engage->capture->stop->analyze)
# Usage:
#   powershell -ExecutionPolicy Bypass -File tune_step.ps1 -Setup "uh3400;kp30;ki0.5;kd53;f0.4;c3000;l750;r6;n3100;x3700" -Target 15 -CaptureS 30
# All Start-Sleep in MILLISECONDS. Always stops the fan at the end.
#
# What's new vs the quick version:
#   * longer default capture (30s) for richer steady-state statistics
#   * every parsed sample is logged to a timestamped CSV under tools/logs/ for
#     offline analysis (open in Excel / re-plot / FFT)
#   * steady-state report adds: drift slope (cm/s), residual oscillation period &
#     amplitude, and RPM(tach) statistics -- the levers that matter once the
#     single-loop hover is "good enough" and we chase the last +/-1cm.
param(
  [string]$Port = "COM8",
  [int]$Baud = 115200,
  [string]$Setup = "",        # semicolon-separated param commands, sent once while stopped
  [int]$PreSpin = 1800,       # manual PWM to pre-spin the fan (eliminate cold-start), 0 = skip
  [int]$PreSpinMs = 2500,     # pre-spin duration (ms)
  [double]$Target = 15,       # closed-loop target height (cm); engaged via t<Target>
  [int]$CaptureS = 30,        # capture seconds after engage (longer = better stats)
  [string]$CsvOut = "",       # CSV path; "" => auto tools/logs/run_<target>_<stamp>.csv
  [double]$TailFrac = 0.5,    # fraction of the run (from the end) treated as steady-state
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

# --- engage closed loop, timestamp every arriving heartbeat line from t=0 ---
$sp.Write(("t{0:N0}" -f $Target) + [char]10)
$lines = New-Object System.Collections.ArrayList   # @{ms=...; txt=...}
$buf = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $CaptureS) {
  try { $buf += $sp.ReadExisting() } catch {}
  while ($buf.Contains("`n")) {
    $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
    if($ln.Trim() -ne ""){ [void]$lines.Add(@{ ms = [int]$sw.Elapsed.TotalMilliseconds; txt = $ln }) }
  }
  Start-Sleep -Milliseconds 10
}
if (-not $NoStop) { $sp.Write("s" + [char]10) }
Start-Sleep -Milliseconds 100
$sp.Close()

$acks = $lines | Where-Object { $_.txt -match '>>' }
Write-Output ("ACKS: " + ((($acks | Select-Object -Last 5) | ForEach-Object { $_.txt }) -join '  |  '))

# --- parse telemetry: H T E P M D R RAW A (CAS/RS optional, ignored here) ---
$T_ms=@(); $H=@(); $P=@(); $D=@(); $R=@(); $RAW=@(); $A=@()
$rx = 'H:(-?[\d.]+) T:[-\d.]+ E:(-?[\d.]+) P:(\d+) M:\d+ D:(-?[\d.]+) R:(\d+) RAW:(-?[\d.]+) A:(\d+)'
foreach($rec in $lines){
  if($rec.txt -match $rx){
    $T_ms += $rec.ms; $H += [double]$matches[1]; $P += [double]$matches[3]
    $D += [double]$matches[4]; $R += [double]$matches[5]; $RAW += [double]$matches[6]; $A += [double]$matches[7]
  }
}
$n=$H.Count

# --- dump per-sample CSV for offline analysis ---
if ($CsvOut -eq "") {
  $logdir = Join-Path $PSScriptRoot "logs"
  if (-not (Test-Path $logdir)) { New-Item -ItemType Directory -Path $logdir | Out-Null }
  $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $CsvOut = Join-Path $logdir ("run_t{0:N0}_{1}.csv" -f $Target, $stamp)
}
$csv = New-Object System.Collections.ArrayList
[void]$csv.Add("t_ms,H_cm,P,D_cms,RPM,RAW_cm,age_ms")
for($i=0;$i -lt $n;$i++){ [void]$csv.Add(("{0},{1},{2},{3},{4},{5},{6}" -f $T_ms[$i],$H[$i],$P[$i],$D[$i],$R[$i],$RAW[$i],$A[$i])) }
[System.IO.File]::WriteAllLines($CsvOut, $csv)
Write-Output ("=== parsed=$n  target=$Target  csv=$CsvOut ===")

# --- feedback-health gate: stalled tach/laser => tuning PID is the 'circles' trap ---
if($A.Count -gt 0){
  $amax=($A|Measure-Object -Maximum).Maximum
  $stall=($A | Where-Object { $_ -gt 150 }).Count
  $pct = if($A.Count){ [math]::Round(100.0*$stall/$A.Count,0) } else { 0 }
  $verdict = if($amax -gt 200){ "*** FEEDBACK STALLING -- fix sensor before tuning ***" } else { "feedback OK" }
  Write-Output ("FEEDBACK: maxAge={0}ms  stalled(>150ms)={1}/{2} ({3}%)  -> {4}" -f $amax,$stall,$A.Count,$pct,$verdict)
}

if($n -le 8){ Write-Output "too few samples"; exit 0 }

# --- coarse traces (downsampled) ---
$tr=@(); $pr=@(); for($i=0;$i -lt $n;$i+=[math]::Max(1,[int]($n/28))){ $tr+=("{0:N1}" -f $H[$i]); $pr+=("{0:N0}" -f $P[$i]) }
Write-Output ("H trace: " + ($tr -join ' '))
Write-Output ("P trace: " + ($pr -join ' '))

# --- steady-state window = last TailFrac of samples ---
$k0=[int]($n*(1.0-$TailFrac)); if($k0 -lt 0){$k0=0}
$tail=$H[$k0..($n-1)]; $tailT=$T_ms[$k0..($n-1)]; $tailR=$R[$k0..($n-1)]
$av=($tail|Measure-Object -Average).Average
$mn=($tail|Measure-Object -Minimum).Minimum; $mx=($tail|Measure-Object -Maximum).Maximum
$sd=[math]::Sqrt((($tail|%{($_-$av)*($_-$av)})|Measure-Object -Sum).Sum/$tail.Count)
Write-Output ("tail H: avg={0:N2} min={1:N1} max={2:N1} std={3:N2} ptp={4:N1}  err_avg={5:N2}  (target={6})" -f $av,$mn,$mx,$sd,($mx-$mn),($av-$Target),$Target)

# --- D-term (ball velocity) stats over tail: is the derivative NOISE-dominated? ---
# At ~42Hz a naked diff of 2.6mm position noise => ~15cm/s velocity noise; x Kd this
# can exceed the whole control band. If the ball is ~still (low H std) but D std is
# large, the D-term is feeding noise into PWM -> raise 'f' (velocity EMA). This block
# turns that diagnosis from estimate into measured fact.
$tailD=$D[$k0..($n-1)]
$dav=($tailD|Measure-Object -Average).Average
$dmn=($tailD|Measure-Object -Minimum).Minimum; $dmx=($tailD|Measure-Object -Maximum).Maximum
$dsd=[math]::Sqrt((($tailD|%{($_-$dav)*($_-$dav)})|Measure-Object -Sum).Sum/$tailD.Count)
Write-Output ("tail D(vel cm/s): avg={0:N2} std={1:N2} ptp={2:N1} min={3:N1} max={4:N1}" -f $dav,$dsd,($dmx-$dmn),$dmn,$dmx)
# crude noise-vs-authority flag: Kd unknown here, but if |D| swings while H is flat -> noise
if($dsd -gt 5.0 -and $sd -lt 2.0){
  Write-Output ("  -> D NOISE-DOMINATED (vel std {0:N1}cm/s while H std only {1:N2}cm): filter velocity (raise 'f') before adding Kd" -f $dsd,$sd)
}

# --- drift: linear fit H vs t over tail (cm/s); flags slow integral wind/leak ---
$m=$tail.Count; $sx=0.0;$sy=0.0;$sxx=0.0;$sxy=0.0
for($i=0;$i -lt $m;$i++){ $x=$tailT[$i]/1000.0; $y=$tail[$i]; $sx+=$x;$sy+=$y;$sxx+=$x*$x;$sxy+=$x*$y }
$den=($m*$sxx-$sx*$sx)
$slope = if([math]::Abs($den) -gt 1e-9){ ($m*$sxy-$sx*$sy)/$den } else { 0 }
$durS=($tailT[$m-1]-$tailT[0])/1000.0
Write-Output ("drift: slope={0:N3} cm/s  (over {1:N1}s tail => {2:N2} cm)" -f $slope,$durS,($slope*$durS))

# --- residual oscillation: detrend (remove mean), count zero-crossings -> period ---
$zc=0; $prev=$tail[0]-$av
for($i=1;$i -lt $m;$i++){ $cur=$tail[$i]-$av; if(($prev -le 0 -and $cur -gt 0) -or ($prev -ge 0 -and $cur -lt 0)){ $zc++ }; $prev=$cur }
if($zc -ge 2 -and $durS -gt 0){
  $period = 2.0*$durS/$zc
  Write-Output ("oscillation: ~{0:N2}s period  amp~{1:N2}cm (std*1.41)  zc={2}" -f $period,($sd*1.414),$zc)
} else { Write-Output "oscillation: none/flat (good)" }

# --- RPM(tach) stats over tail: actuator-speed spread is the cascade-loop lever ---
if(($tailR | Measure-Object -Maximum).Maximum -gt 0){
  $rav=($tailR|Measure-Object -Average).Average
  $rsd=[math]::Sqrt((($tailR|%{($_-$rav)*($_-$rav)})|Measure-Object -Sum).Sum/$tailR.Count)
  Write-Output ("RPM tail: avg={0:N0} std={1:N0} min={2} max={3}" -f $rav,$rsd,($tailR|Measure-Object -Minimum).Minimum,($tailR|Measure-Object -Maximum).Maximum)
} else { Write-Output "RPM tail: tach reads 0 (check PC6 wiring before enabling cascade)" }

Write-Output ("P range: {0}..{1}   RPM range: {2}..{3}" -f ($P|Measure-Object -Minimum).Minimum,($P|Measure-Object -Maximum).Maximum,($R|Measure-Object -Minimum).Minimum,($R|Measure-Object -Maximum).Maximum)
