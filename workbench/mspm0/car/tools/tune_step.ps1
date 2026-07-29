# tune_step.ps1 - cascade-control single-step tuning: set gains -> run one step -> capture series -> metrics.
#   Works with car.c firmware cmds (m/t/p/i/d/f). Sends char-by-char + 25ms (avoid MCU RX FIFO overflow).
#   ASCII-only on purpose (Win PowerShell 5.1 mangles UTF-8 -> keep this file ASCII).
# Example (speed loop step to 40 RPM, Kp=0.15 Ki=0.03, 3s capture):
#   .\tune_step.ps1 -Mode 2 -Target 40 -Kp 150 -Ki 30 -Kd 0 -Sec 3
# Gains are milli-units (match firmware p/i/d: 150 => 0.150). Pass -1 to skip a gain (keep firmware's).
# Mode: 1=current 2=speed 3=position.
param(
    [int]$Mode   = 2,
    [int]$Target = 40,
    [int]$Kp = -1,
    [int]$Ki = -1,
    [int]$Kd = -1,
    [double]$Sec = 3.0,
    [int]$TelemMs = 20,
    [string]$Port = "COM30",
    [int]$Baud = 115200,
    [string]$Log = ""
)
$sp = New-Object System.IO.Ports.SerialPort $Port,$Baud,None,8,one
$sp.ReadTimeout = 250
function Send($cmd){ foreach($ch in ($cmd+"`n").ToCharArray()){ $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }; Start-Sleep -Milliseconds 60 }
try { $sp.Open() } catch { Write-Output "OPEN_FAIL: $($_.Exception.Message)"; exit 1 }
Start-Sleep -Milliseconds 250; try{$sp.DiscardInBuffer()}catch{}
try {
    Send "f$TelemMs"                       # faster telemetry
    Send "m$Mode"                          # enter mode (target->0)
    if($Kp -ge 0){ Send "p$Kp" }           # gains (must be in target mode so loop_index matches)
    if($Ki -ge 0){ Send "i$Ki" }
    if($Kd -ge 0){ Send "d$Kd" }
    Start-Sleep -Milliseconds 200; try{$sp.DiscardInBuffer()}catch{}
    $t0 = Get-Date
    Send "t$Target"                        # === STEP ===
    $rows = New-Object System.Collections.ArrayList
    $end = (Get-Date).AddSeconds($Sec)
    while((Get-Date) -lt $end){
        try { $line = $sp.ReadLine() } catch { continue }
        if($line -notmatch 'V:(-?\d+),(-?\d+).*PWM:(-?\d+),(-?\d+).*C:(-?\d+),(-?\d+)'){ continue }
        $ms = [int]((Get-Date) - $t0).TotalMilliseconds
        [void]$rows.Add([pscustomobject]@{ ms=$ms; V1=[int]$matches[1]; V2=[int]$matches[2]; PWM1=[int]$matches[3]; PWM2=[int]$matches[4]; C1=[int]$matches[5]; C2=[int]$matches[6] })
    }
    Send "z"                               # stop
    Send "f100"                            # restore telemetry

    $out = New-Object System.Collections.ArrayList
    if($rows.Count -lt 3){
        [void]$out.Add("TOO_FEW_SAMPLES ($($rows.Count)): motor not spinning? battery off? port busy? check and retry.")
    } else {
        $n = $rows.Count
        $tailN = [Math]::Max(3,[int]($n*0.3))
        $tail = $rows | Select-Object -Last $tailN
        $ssV1 = ($tail | Measure-Object V1 -Average).Average
        $sdV1 = [Math]::Sqrt(( $tail | ForEach-Object { [Math]::Pow($_.V1-$ssV1,2) } | Measure-Object -Average ).Average)
        $ssV2 = ($tail | Measure-Object V2 -Average).Average
        $peakV1 = ($rows | Measure-Object V1 -Maximum).Maximum
        $over = if($Target -ne 0){ [Math]::Round(($peakV1-$ssV1)/[Math]::Abs($Target)*100,1) } else { 0 }
        $thr = 0.9*$ssV1
        $riseRow = $rows | Where-Object { $_.V1 -ge $thr } | Select-Object -First 1
        $rise = if($riseRow){ $riseRow.ms } else { -1 }
        [void]$out.Add("== step Mode=$Mode Target=$Target Kp=$Kp Ki=$Ki Kd=$Kd (milli) samples=$n ==")
        [void]$out.Add(("V1_ss={0:N1} std={1:N1} | V2_ss={2:N1} | V1_peak={3} overshoot={4}% rise90={5}ms" -f $ssV1,$sdV1,$ssV2,$peakV1,$over,$rise))
        [void]$out.Add("-- series: ms V1 V2 PWM1 PWM2 --")
        foreach($r in $rows){ [void]$out.Add(("{0,5} V {1,5} {2,5} PWM {3,4} {4,4}" -f $r.ms,$r.V1,$r.V2,$r.PWM1,$r.PWM2)) }
    }
    # write result FILE before the (possibly-hanging) Close, so output survives even if Close deadlocks
    $out | Out-File -FilePath (Join-Path $PSScriptRoot 'tune_out.txt') -Encoding ASCII
    $out | ForEach-Object { Write-Output $_ }
} finally {
    try{$sp.DiscardInBuffer()}catch{}
    try{$sp.DiscardOutBuffer()}catch{}
    try{$sp.ReadTimeout=0}catch{}
    try{$sp.Close()}catch{}
}
