# ball_round.ps1 - run one controlled A/B/A hold round without manual script/log hand-offs.
#
# One invocation changes exactly ONE variable. A is repeated after B so bench drift is visible instead
# of being mistaken for a tuning improvement. Each child run keeps ball_hold's feedback-health gate,
# 15 s firmware hard cap, sample-rate gate, servo authority calculation, and finally stop.
#
# Quick defaults are 3 s settle + 5 s capture. They are intended for tuning direction, not final proof;
# confirm the winning value with a normal longer ball_hold run before copying it to config.h.
# Real-machine wall-clock improvement is PENDING HARDWARE VERIFICATION.
#
# EXAMPLE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_round.ps1 `
#       -Port COM30 -Variable Kd -A 8 -B 10
#
# EXIT CODES: 0 = all three measurements valid, 2 = a health gate/child run was inconclusive.
# ASCII only on purpose.
param(
    [string]$Port = "COM30",
    [int]$Baud = 115200,
    [Parameter(Mandatory=$true)]
    [ValidateSet("Kp","Kd","ThetaMaxDeg","Alpha100","FfMask")]
    [string]$Variable,
    [Parameter(Mandatory=$true)][double]$A,
    [Parameter(Mandatory=$true)][double]$B,
    [double]$Setpoint = 0.0,
    [int]$Sec = 5,
    [double]$SettleSec = 3.0,
    [string]$OutRoot = "_logs\ball",
    [string]$RunId = ""
)

$ErrorActionPreference = "Stop"
$sw = [System.Diagnostics.Stopwatch]::StartNew()
if ([string]::IsNullOrWhiteSpace($RunId)) { $RunId = "round-" + (Get-Date -Format "yyyyMMdd-HHmmss") }
if ($A -eq $B) { Write-Host "A and B must differ; otherwise this is not an A/B/A test." -ForegroundColor Red; exit 2 }
if ($Variable -eq "FfMask") {
    if ($A -lt 0 -or $A -gt 3 -or $B -lt 0 -or $B -gt 3 -or $A -ne [Math]::Truncate($A) -or $B -ne [Math]::Truncate($B)) {
        Write-Host "FfMask A/B must be integer values in 0..3." -ForegroundColor Red; exit 2
    }
} elseif ($Variable -in @("ThetaMaxDeg","Alpha100")) {
    if ($A -le 0 -or $B -le 0 -or $A -ne [Math]::Truncate($A) -or $B -ne [Math]::Truncate($B)) {
        Write-Host "$Variable A/B must be positive integers." -ForegroundColor Red; exit 2
    }
} elseif ($A -le 0 -or $B -le 0) {
    Write-Host "$Variable A/B must be > 0; zero means 'leave firmware value unchanged' in ball_hold." -ForegroundColor Red; exit 2
}
if ($Sec -lt 3) { Write-Host "Sec must be >= 3 for a useful tuning-direction measurement." -ForegroundColor Red; exit 2 }
if ($SettleSec -lt 1.0) { Write-Host "SettleSec must be >= 1.0." -ForegroundColor Red; exit 2 }

$carRoot = Split-Path $PSScriptRoot -Parent
if (-not [System.IO.Path]::IsPathRooted($OutRoot)) { $OutRoot = Join-Path $carRoot $OutRoot }
$runRoot = Join-Path ([System.IO.Path]::GetFullPath($OutRoot)) $RunId
New-Item -ItemType Directory -Path $runRoot -Force | Out-Null
$hold = Join-Path $PSScriptRoot "ball_hold.ps1"
$psExe = (Get-Process -Id $PID).Path
$culture = [System.Globalization.CultureInfo]::InvariantCulture
$runs = New-Object System.Collections.Generic.List[object]
$plan = @(
    [pscustomobject]@{ label="A1"; value=$A },
    [pscustomobject]@{ label="B";  value=$B },
    [pscustomobject]@{ label="A2"; value=$A }
)
$abortReason = $null

Write-Host ("================ ball_round {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
Write-Host ("run {0} | one variable only: {1}, A={2}, B={3}, plan A/B/A" -f $RunId, $Variable, $A, $B)
Write-Host ("quick window: settle {0:N1}s + capture {1}s; final winning value still needs a longer confirmation run" -f $SettleSec, $Sec)

foreach ($item in $plan) {
    $label = [string]$item.label
    $valueText = [Convert]::ToString([double]$item.value, $culture)
    $childId = "$RunId-$label"
    $out = Join-Path $runRoot ("hold_{0}.txt" -f $label)
    $json = Join-Path $runRoot ("hold_{0}.json" -f $label)
    $args = @(
        "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", $hold,
        "-Port", $Port, "-Baud", [string]$Baud,
        "-Setpoint", ([Convert]::ToString($Setpoint, $culture)),
        "-Sec", [string]$Sec,
        "-SettleSec", ([Convert]::ToString($SettleSec, $culture)),
        "-RunId", $childId, "-Label", $label,
        "-Out", $out, "-Json", $json,
        ("-" + $Variable), $valueText,
        "-Yes"
    )
    Write-Host ""
    Write-Host ("---- {0}: {1}={2} ----" -f $label, $Variable, $valueText) -ForegroundColor Cyan
    & $psExe @args
    $childCode = $LASTEXITCODE
    if (-not (Test-Path $json)) {
        $abortReason = "$label produced no JSON (child exit $childCode)"
        break
    }
    try { $jr = Get-Content $json -Raw | ConvertFrom-Json } catch {
        $abortReason = "$label JSON could not be parsed: $($_.Exception.Message)"
        break
    }
    [void]$runs.Add($jr)
    if ($childCode -eq 2 -or $jr.verdict -eq "INCONCLUSIVE") {
        $abortReason = "$label was inconclusive: $($jr.reason)"
        break
    }
    if ($childCode -notin @(0,1)) {
        $abortReason = "$label child exited with unexpected code $childCode"
        break
    }
    Start-Sleep -Milliseconds 150
}

$sw.Stop()
$result = [ordered]@{
    run_id = $RunId
    tool = "ball_round"
    elapsed_ms = [int][Math]::Round($sw.Elapsed.TotalMilliseconds)
    port = $Port
    parameters = [ordered]@{ variable=$Variable; a=$A; b=$B; setpoint_mm=$Setpoint; capture_s=$Sec; settle_s=$SettleSec; sequence="A/B/A" }
    health = [ordered]@{ completed_runs=$runs.Count; expected_runs=3 }
    metrics = [ordered]@{}
    verdict = $(if ($abortReason) { "INCONCLUSIVE" } else { "PASS" })
    reason = $(if ($abortReason) { $abortReason } else { "three valid A/B/A measurements completed; PASS here means measurement complete, not that the controller met its score budget" })
    next_action = ""
    raw_log = $runRoot
    runs = @($runs.ToArray())
}

if (-not $abortReason -and $runs.Count -eq 3) {
    $a1 = [double]$runs[0].metrics.error_peak_mm
    $bp = [double]$runs[1].metrics.error_peak_mm
    $a2 = [double]$runs[2].metrics.error_peak_mm
    $aMean = 0.5 * ($a1 + $a2)
    $drift = [Math]::Abs($a2 - $a1)
    $driftLimit = [Math]::Max(2.0, 0.20 * [Math]::Max($aMean, 0.1))
    $delta = $bp - $aMean
    $result.metrics = [ordered]@{
        a1_peak_mm = [Math]::Round($a1, 3)
        b_peak_mm = [Math]::Round($bp, 3)
        a2_peak_mm = [Math]::Round($a2, 3)
        a_mean_peak_mm = [Math]::Round($aMean, 3)
        b_minus_a_mean_mm = [Math]::Round($delta, 3)
        baseline_drift_mm = [Math]::Round($drift, 3)
        baseline_stable = ($drift -le $driftLimit)
    }
    if ($drift -gt $driftLimit) {
        $result.next_action = "A1/A2 drift is too large for attribution; fix the disturbance/feedback condition and repeat the same A/B/A round"
    } elseif ($delta -lt 0) {
        $result.next_action = "B reduced peak error; confirm B with a normal longer ball_hold run before changing config.h"
    } else {
        $result.next_action = "B did not reduce peak error; keep A or choose one new single-variable B value"
    }
    Write-Host ""
    Write-Host "================ A/B/A summary ================"
    Write-Host ("  A1 peak {0,7:N2} mm" -f $a1)
    Write-Host ("  B  peak {0,7:N2} mm" -f $bp)
    Write-Host ("  A2 peak {0,7:N2} mm" -f $a2)
    Write-Host ("  B - mean(A) = {0:+0.00;-0.00;0.00} mm | A drift {1:N2} mm | baseline {2}" -f $delta, $drift, $(if ($drift -le $driftLimit) { "STABLE" } else { "UNSTABLE" }))
    Write-Host ("  next: {0}" -f $result.next_action)
} else {
    $result.next_action = "fix the failed health gate; do not interpret or continue the parameter round"
}

$resultJson = Join-Path $runRoot "round_result.json"
$result | ConvertTo-Json -Depth 9 | Set-Content $resultJson -Encoding UTF8
Write-Host ""
Write-Host ("elapsed: {0:N1} s" -f $sw.Elapsed.TotalSeconds)
Write-Host ("RESULT: {0} - {1}" -f $result.verdict, $result.reason)
Write-Host ("RESULT_JSON: {0}" -f $resultJson)
if ($abortReason) { exit 2 }
exit 0
