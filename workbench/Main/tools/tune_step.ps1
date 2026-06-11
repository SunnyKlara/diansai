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
  [string]$Port = "COM9",
  [int]$Baud = 115200,
  [string]$Setup = "",        # semicolon-separated param commands, sent once while stopped
  [int]$PreSpin = 1800,       # manual PWM to pre-spin the fan (eliminate cold-start), 0 = skip
  [int]$PreSpinMs = 2500,     # pre-spin duration (ms)
  [double]$Target = 15,       # closed-loop target height (cm); engaged via t<Target>
  [int]$CaptureS = 30,        # capture seconds after engage (longer = better stats)
  [string]$CsvOut = "",       # CSV path; "" => auto tools/logs/run_<target>_<stamp>.csv
  [double]$TailFrac = 0.5,    # fraction of the run (from the end) treated as steady-state
  [int]$SerialMs = 0,         # >0 => set heartbeat period (ms) via 'w' before capture (e.g. 20=50Hz fast log); 0 = leave firmware default
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
if ($SerialMs -gt 0) { Send ("w" + $SerialMs) }   # bump heartbeat rate for richer/faster capture
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
if ($SerialMs -gt 0) { Start-Sleep -Milliseconds 60; $sp.Write("w80" + [char]10) }  # restore default heartbeat
Start-Sleep -Milliseconds 100
$sp.Close()

$acks = $lines | Where-Object { $_.txt -match '>>' }
Write-Output ("ACKS: " + ((($acks | Select-Object -Last 5) | ForEach-Object { $_.txt }) -join '  |  '))

# --- parse telemetry: GENERIC key:value capture, keeps EVERY field the firmware sends ---
# Firmware heartbeat: H T E P M D R RAW A F PRE CAS RS CM FH [TID TIN TST]
# Old parser only kept 8 of these and silently dropped F/RS/FH/CM -> half-blind on
# cascade + laser-rate. Now we capture all tokens generically into per-sample maps.
# Canonical analysis arrays (consumed by stats below + analyze.py/plot scripts) are
# filled from the map so downstream column names (H_cm,D_cms,RPM,...) stay stable.
$samples = New-Object System.Collections.ArrayList   # @{ t=ms ; map=@{KEY=val} }
$tok = [regex]'([A-Za-z]+):(0x[0-9A-Fa-f]+|-?[\d.]+)'
foreach($rec in $lines){
  if($rec.txt -notmatch 'H:' -or $rec.txt -notmatch 'P:'){ continue }  # skip ACKs/garbage
  $m = [ordered]@{}
  foreach($mm in $tok.Matches($rec.txt)){ $m[$mm.Groups[1].Value] = $mm.Groups[2].Value }
  if($m.Contains('H')){ [void]$samples.Add(@{ t = $rec.ms; map = $m }) }
}
$n = $samples.Count

function Col($key){ # numeric column from sample maps; missing -> NaN (0x.. hex parsed too)
  $a = New-Object 'System.Collections.Generic.List[double]'
  foreach($s in $samples){
    if($s.map.Contains($key)){
      $v = $s.map[$key]
      if($v -like '0x*'){ [void]$a.Add([Convert]::ToInt64($v,16)) }
      else { [void]$a.Add([double]$v) }
    } else { [void]$a.Add([double]::NaN) }
  }
  return $a
}
$T_ms = @($samples | ForEach-Object { $_.t })
$H = Col 'H'; $P = Col 'P'; $D = Col 'D'; $R = Col 'R'; $RAW = Col 'RAW'; $A = Col 'A'
$RS = Col 'RS'; $Fhz = Col 'F'   # rpm setpoint (cascade) + measured sample rate (laser)

# --- dump per-sample CSV: canonical columns FIRST (back-compat with analyze.py /
#     plot_report_figs.py), then EVERY remaining field discovered in the stream. ---
if ($CsvOut -eq "") {
  $logdir = Join-Path $PSScriptRoot "logs"
  if (-not (Test-Path $logdir)) { New-Item -ItemType Directory -Path $logdir | Out-Null }
  $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
  $CsvOut = Join-Path $logdir ("run_t{0:N0}_{1}.csv" -f $Target, $stamp)
}
# canonical name map (firmware key -> CSV header expected by existing tools)
$canon = [ordered]@{ H='H_cm'; T='T_cm'; E='E_cm'; P='P'; M='M'; D='D_cms'; R='RPM'; RAW='RAW_cm'; A='age_ms' }
# discover any extra keys present in the stream but not in $canon, keep stable order
$extra = New-Object System.Collections.ArrayList
foreach($s in $samples){ foreach($k in $s.map.Keys){ if(-not $canon.Contains($k) -and -not $extra.Contains($k)){ [void]$extra.Add($k) } } }
$keyOrder = @($canon.Keys) + @($extra)              # firmware keys in CSV column order
$hdr = @('t_ms') + @($canon.Values) + @($extra)     # CSV header row
$csv = New-Object System.Collections.ArrayList
[void]$csv.Add(($hdr -join ','))
foreach($s in $samples){
  $row = New-Object System.Collections.ArrayList
  [void]$row.Add($s.t)
  foreach($k in $keyOrder){ [void]$row.Add( ($(if($s.map.Contains($k)){ $s.map[$k] } else { '' })) ) }
  [void]$csv.Add(($row -join ','))
}
[System.IO.File]::WriteAllLines($CsvOut, $csv)
Write-Output ("=== parsed=$n  fields=[" + (($keyOrder) -join ' ') + "]  target=$Target  csv=$CsvOut ===")

# Auto-run the offline analyzer -> writes <csv>.summary.txt (clean, read via read_file).
# This is the authoritative metrics output; the stdout below can be ignored if it wraps.
$py = Join-Path $PSScriptRoot "..\..\..\.venv\Scripts\python.exe"
$an = Join-Path $PSScriptRoot "analyze.py"
if ((Test-Path $py) -and (Test-Path $an) -and $n -gt 8) {
  try { & $py $an $CsvOut --target $Target --tail $TailFrac | ForEach-Object { Write-Output $_ } }
  catch { Write-Output ("analyze.py failed: " + $_.Exception.Message) }
  Write-Output ("SUMMARY FILE: " + $CsvOut + ".summary.txt")
}

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

# --- NEW: cascade inner-loop tracking (was invisible before full-field capture) ---
# RS = rpm_setpoint (outer loop's demand), R = measured rpm. In CTRL_MODE=2 the inner
# loop should drive (RS - R) -> 0; a large persistent gap means the PWM->RPM feedforward
# (ya/yb) is mis-calibrated or the inner PI (yp/yi) is too soft. This is the lever the
# old 8-field parser threw away.
$tailRS=$RS[$k0..($n-1)]
$rsValid = ($tailRS | Where-Object { -not [double]::IsNaN($_) -and $_ -gt 0 })
if($rsValid.Count -gt 0){
  $rsav=($rsValid|Measure-Object -Average).Average
  $gap = $rsav - $rav    # setpoint minus measured (rav from RPM tail above)
  Write-Output ("cascade: rpm_sp avg={0:N0}  rpm_meas avg={1:N0}  inner-loop gap={2:N0} rpm" -f $rsav,$rav,$gap)
  if([math]::Abs($gap) -gt 150){
    Write-Output ("  -> INNER LOOP not tracking (gap {0:N0} rpm): re-cal PWM->RPM (ya/yb) or stiffen inner PI (yp/yi)" -f $gap)
  }
}

# --- NEW: measured sample rate (F:) -- is the laser actually running fast enough? ---
# If you ran with -SerialMs 20 expecting 50Hz logging but F: reports ~42, the ToF/loop
# (not the UART) is the bottleneck; if F: is high but few rows landed, the UART/host is.
$Fv = $Fhz | Where-Object { -not [double]::IsNaN($_) -and $_ -gt 0 }
if($Fv.Count -gt 0){
  $fav=($Fv|Measure-Object -Average).Average
  $loghz = if($durS -gt 0){ $tail.Count/$durS } else { 0 }
  Write-Output ("sample rate: firmware F avg={0:N1} Hz   logged rows ~{1:N1} Hz over tail" -f $fav,$loghz)
}
