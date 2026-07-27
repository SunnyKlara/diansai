# gyro_drift_test.ps1 - does running the motors degrade the gyro bias calibration (`k`)?
#
# WHY THIS EXISTS
#   Measured earlier: right after `k` the yaw drift was ~0.4 deg/min, but a 5-minute soak taken
#   LATER in the same session showed ~1.17 deg/min. Between the two we had run the motors.
#   Two candidate causes, and they demand OPPOSITE workflows:
#       motors (heat/vibration)  -> `k` must be redone BEFORE EVERY RUN
#       just time (thermal ramp) -> `k` at power-on is fine, drift is simply slow
#   A single before/after pair CANNOT tell them apart, so this script takes three windows:
#
#       A : `k`, then sit still                      (baseline)
#       C : run the motors, then sit still           (motor effect + time)
#       D : keep sitting still, same length as C     (time only)
#
#   Read it as:  C >> A and D back near A  =>  motors did it.
#                C and D both >> A         =>  it is time / thermal, not the motors.
#                C ~ A                     =>  no measurable degradation at all.
#
# SAFETY: the motor stage spins the wheels. WHEELS MUST BE OFF THE GROUND.
#   Each spin burst is kept under the firmware hard cap (15 s) and ends with `z`.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File gyro_drift_test.ps1 -Port COM4
#   powershell -NoProfile -ExecutionPolicy Bypass -File gyro_drift_test.ps1 -Port COM4 -WinSec 30 -SpinSec 10
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM4",
    [int]$Baud       = 115200,
    [int]$WinSec     = 60,     # length of each measurement window (A / C / D)
    [int]$SpinSec    = 12,     # one motor burst (keep < firmware hard cap 15 s)
    [int]$SpinBursts = 2,      # how many bursts
    [int]$SpinRpm    = 150,
    [string]$Out     = "gyro_drift_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L "OPEN_FAIL ($Port): $($_.Exception.Message)"; exit 1 }

function Send([string]$cmd) {
    foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
}

# Collect for $sec seconds, return yaw drift in deg/min measured with the FIRMWARE timestamp.
# Firmware t<ms> is used instead of the PC clock because arrivals are bursty over the UDP bridge.
function Measure-Drift([int]$sec, [string]$tag) {
    Start-Sleep -Milliseconds 300
    [void]$sp.ReadExisting()
    $rx = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 20
    }
    $re = [regex]'Y:(?<y>-?\d+)\s+W:(?<w>-?\d+).*?\|\s*#(?<seq>\d+)\s+t(?<t>\d+)'
    $ys = New-Object System.Collections.Generic.List[double]
    $ts = New-Object System.Collections.Generic.List[double]
    $ws = New-Object System.Collections.Generic.List[double]
    $sq = New-Object System.Collections.Generic.List[int]
    foreach ($ln in ($rx.ToString() -split "`r?`n")) {
        $m = $re.Match($ln)
        if (-not $m.Success) { continue }
        $ys.Add([double]$m.Groups['y'].Value / 10.0)
        $ts.Add([double]$m.Groups['t'].Value)
        $ws.Add([Math]::Abs([double]$m.Groups['w'].Value / 100.0))
        $sq.Add([int]$m.Groups['seq'].Value)
    }
    $n = $ys.Count
    if ($n -lt 20) {
        L ("  [$tag] INCONCLUSIVE - only $n samples")
        return $null
    }
    $dtS = ($ts[$n-1] - $ts[0]) / 1000.0
    $dY  = $ys[$n-1] - $ys[0]
    $perMin = if ($dtS -gt 0) { $dY * 60.0 / $dtS } else { 0.0 }
    $wPeak = ($ws | Measure-Object -Maximum).Maximum
    $expect = $sq[$n-1] - $sq[0] + 1
    $loss = 100.0 * ($expect - $n) / $expect
    L ("  [{0}] {1} samples over {2:N1}s | yaw {3:N2} deg => {4,7:N2} deg/min | peak |wz| {5:N1} dps | loss {6:N2}%" -f `
        $tag, $n, $dtS, $dY, $perMin, $wPeak, $loss)
    return $perMin
}

L "================ gyro_drift_test  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port   window ${WinSec}s x3   motor $SpinBursts x ${SpinSec}s @ ${SpinRpm}rpm"
L "WHEELS MUST BE OFF THE GROUND. Do not touch the car during the still windows."
L ""

L "-- stage A : calibrate (k), then sit still --"
Send "z"; Start-Sleep -Milliseconds 400
Send "k"; Start-Sleep -Seconds 3          # bias cal needs ~2 s of stillness
$A = Measure-Drift $WinSec "A after k"

# FAIL FAST. Learned the hard way 2026-07-27: the board died (brown-out / lockup) during stage A,
# and the original script happily went on to spin the motors and burn two more 60 s windows on a
# dead target. If we cannot even read telemetry, every later stage is meaningless AND driving the
# motors while the board is in an unknown state is exactly what you do not want to do.
if ($A -eq $null) {
    L ""
    L "RESULT: INCONCLUSIVE - stage A got no telemetry, so ABORTING before touching the motors."
    L "  Diagnose in this order (cheap -> expensive):"
    L "    1. read the WIRED port too (read_serial.ps1 -Port COM30). Both ports silent => it is the"
    L "       MCU, not the wireless link."
    L "    2. openocd init: 'SWD DPIDR ...' OK but 'Examination failed' => target half-dead."
    L "       #1 suspect after a session with lots of motor running is BATTERY BROWN-OUT."
    L "    3. Only if power is known-good and it still fails: unbrick_flash.ps1 (real lockup)."
    L "  Do NOT retry openocd in a loop - that is how the DAP USB got wedged before."
    Set-Content $Out $log.ToString() -Encoding ASCII
    try { $sp.Close(); $sp.Dispose() } catch {}
    exit 2
}

L ""
L "-- stage B : run the motors (no re-calibration afterwards - that is the point) --"
for ($i = 1; $i -le $SpinBursts; $i++) {
    Send "m7"; Start-Sleep -Milliseconds 300
    Send "v$SpinRpm"
    L ("  burst {0}/{1} : {2}s @ {3} rpm" -f $i, $SpinBursts, $SpinSec, $SpinRpm)
    Start-Sleep -Seconds $SpinSec
    Send "z"; Start-Sleep -Milliseconds 600
}

L ""
L "-- stage C : sit still right after the motors --"
$C = Measure-Drift $WinSec "C after motors"

L ""
L "-- stage D : keep sitting still (time-only control) --"
$D = Measure-Drift $WinSec "D time only"

try { $sp.Write("z`n") } catch {}
Start-Sleep -Milliseconds 200
try { $sp.Close(); $sp.Dispose() } catch {}

L ""
L "---- verdict ----"
if ($A -eq $null -or $C -eq $null -or $D -eq $null) {
    L "RESULT: INCONCLUSIVE - at least one window had too few samples."
    Set-Content $Out $log.ToString() -Encoding ASCII; exit 2
}
$aA = [Math]::Abs($A); $aC = [Math]::Abs($C); $aD = [Math]::Abs($D)
L ("|drift| A / C / D  =  {0:N2} / {1:N2} / {2:N2} deg/min" -f $aA, $aC, $aD)
# Thresholds: 2x is the "clearly bigger" bar; anything under 0.5 deg/min is small enough that a
# 20 s run drifts <0.2 deg, i.e. irrelevant for steering accuracy.
$bigC = ($aC -gt [Math]::Max(2.0 * $aA, $aA + 0.3))
$bigD = ($aD -gt [Math]::Max(2.0 * $aA, $aA + 0.3))
if ($bigC -and -not $bigD) {
    L "VERDICT: MOTORS degrade the calibration (C jumped, D came back)."
    L "  => workflow must be: re-send `k` BEFORE EVERY RUN, not once at power-on."
} elseif ($bigC -and $bigD) {
    L "VERDICT: TIME / thermal drift, not the motors (C and D both elevated)."
    L "  => `k` at power-on is fine, but its accuracy decays with uptime;"
    L "     re-`k` if the car has been sitting powered for a long while."
} elseif (-not $bigC) {
    L "VERDICT: no measurable degradation from the motors."
    L "  => `k` once per power-on is enough (still cheap to redo before a critical run)."
} else {
    L "VERDICT: unclear pattern (D elevated but C not) - suspect something touched the car."
}
L ("NOTE: at {0:N2} deg/min a 20 s run drifts {1:N2} deg - compare that against the heading" -f $aC, ($aC * 20.0 / 60.0))
L "      accuracy the task actually needs before spending any more time on this."
Set-Content $Out $log.ToString() -Encoding ASCII
exit 0
