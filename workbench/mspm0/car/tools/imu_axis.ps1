# imu_axis.ps1 - IMU yaw-axis bring-up for the car firmware (MSPM0, COM30).
#
# WHY: attitude.c assumes "slot2 = vertical axis". On the real board the gravity vector
#   does NOT necessarily land on Z (2026-07-27 measurement: mostly +Y). Picking the wrong
#   axis makes the heading loop silently dead while LOOKING like "PID won't tune".
#   So the axis and its sign must be measured on the board, in this order:
#
#   L1  define axis   : car FLAT and STILL   -> reads accel, tells you which axis is vertical
#   L2  define sign   : car FLAT, you rotate it LEFT (CCW) ~90 deg by hand -> checks yaw sign
#   L3  static drift  : car FLAT and STILL for N seconds -> yaw drift deg/min (deadband need)
#
# The firmware exposes a<0|1|2> (axis) and s<1|-1> (sign) at RUNTIME, so the whole
# procedure needs ONE flash. Do not re-flash to try axes: back-to-back flashing is what
# pushed this MCU into lockup before (see steering SSOT section D2).
#
# Usage (one transaction per call, port is released on exit):
#   powershell -NoProfile -ExecutionPolicy Bypass -File imu_axis.ps1 -Step L1
#   powershell -NoProfile -ExecutionPolicy Bypass -File imu_axis.ps1 -Step L2 -Axis 1 -Sec 10
#   powershell -NoProfile -ExecutionPolicy Bypass -File imu_axis.ps1 -Step L3 -Sec 60
#
# Output: console + imu_axis_out.txt. Last line is always
#   RESULT: PASS | FAIL | INCONCLUSIVE  <detail>
# Exit code: 0=PASS 1=FAIL 2=INCONCLUSIVE (INCONCLUSIVE means "I could not measure it",
#   which is NOT the same as "it is fine" - see steering pit-library on the third state).
#
# NOTE: exclusive port. Close watch_serial / other serial tools first.

param(
    [ValidateSet("L1","L2","L3")][string]$Step = "L1",
    [int]$Axis = -1,          # -1 = leave firmware default / current
    [int]$Sign = 0,           #  0 = leave current, else +1 / -1
    [int]$Sec  = 0,           #  0 = per-step default (L2=10s, L3=60s)
    [string]$Port = "COM30",
    [int]$Baud = 115200
)

$out = "imu_axis_out.txt"
function Log([string]$s) { Write-Output $s; Add-Content -Path $out -Value $s }
Set-Content -Path $out -Value ("== imu_axis " + $Step + " " + (Get-Date -Format "yyyy-MM-dd HH:mm:ss") + " ==")

function Send-Cmd($p, [string]$cmd) {
    # one char at a time + 25ms: a single burst write overruns the small MCU RX FIFO
    foreach ($ch in $cmd.ToCharArray()) { $p.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    $p.Write("`n"); Start-Sleep -Milliseconds 150
}
function Read-For($p, [int]$ms) {
    $sb = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.ElapsedMilliseconds -lt $ms) {
        try { [void]$sb.Append($p.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 40
    }
    return $sb.ToString()
}
function Finish([string]$verdict, [string]$detail) {
    Log ("RESULT: " + $verdict + "  " + $detail)
    switch ($verdict) { "PASS" { exit 0 } "FAIL" { exit 1 } default { exit 2 } }
}

try {
    $p = New-Object System.IO.Ports.SerialPort $Port, $Baud, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $p.ReadTimeout = 300; $p.WriteTimeout = 300; $p.Open()
} catch {
    Log ("OPEN_FAIL: " + $_.Exception.Message)
    Finish "INCONCLUSIVE" "serial port could not be opened - firmware state unknown"
}
Start-Sleep -Milliseconds 250
try { $p.DiscardInBuffer() } catch {}

# make sure nothing is driving the wheels while we handle the car
Send-Cmd $p "z"
if ($Axis -ge 0) { Send-Cmd $p ("a" + $Axis) }
if ($Sign -ne 0) { if ($Sign -ge 0) { Send-Cmd $p "s1" } else { Send-Cmd $p "s-1" } }

# ---------------------------------------------------------------- L1: which axis is vertical
if ($Step -eq "L1") {
    Log "L1: put the car FLAT in its real mounting attitude and keep it STILL. Reading 'g'..."
    Send-Cmd $p "g"
    $txt = Read-For $p 1500
    Log "---- raw ----"; Log $txt
    try { $p.Close() } catch {}

    $mWho = [regex]::Match($txt, 'WHOAMI=(\d+)')
    if (-not $mWho.Success) { Finish "INCONCLUSIVE" "no [imu] WHOAMI line - old firmware or no telemetry" }
    if ([int]$mWho.Groups[1].Value -ne 71) {
        Finish "FAIL" ("WHOAMI=" + $mWho.Groups[1].Value + " (expect 71=0x47) - check CS PB6 / MISO PB7 / supply")
    }
    $mAcc = [regex]::Match($txt, 'accel mg:(-?\d+),(-?\d+),(-?\d+)')
    if (-not $mAcc.Success) { Finish "INCONCLUSIVE" "could not parse 'accel mg:' from the dump" }
    $a = @([int]$mAcc.Groups[1].Value, [int]$mAcc.Groups[2].Value, [int]$mAcc.Groups[3].Value)
    $mag = [math]::Sqrt($a[0]*$a[0] + $a[1]*$a[1] + $a[2]*$a[2])
    Log ("accel mg  X=" + $a[0] + "  Y=" + $a[1] + "  Z=" + $a[2] + "   |a|=" + [int]$mag + " mg")

    if ($mag -lt 850 -or $mag -gt 1150) {
        Finish "FAIL" ("|a|=" + [int]$mag + " mg, expected ~1000 - accel scaling (4g/8192LSB) or the reading itself is wrong")
    }
    $best = 0; for ($i = 1; $i -lt 3; $i++) { if ([math]::Abs($a[$i]) -gt [math]::Abs($a[$best])) { $best = $i } }
    $name = @("X","Y","Z")[$best]
    $others = 0; for ($i = 0; $i -lt 3; $i++) { if ($i -ne $best) { $others = [math]::Max($others, [math]::Abs($a[$i])) } }
    if ([math]::Abs($a[$best]) -lt 900 -or $others -gt 250) {
        Log ("dominant axis " + $name + "=" + $a[$best] + " mg, largest other=" + $others + " mg")
        Finish "INCONCLUSIVE" "car is not flat enough (gravity is split across axes) - level it and rerun L1"
    }
    $downNote = ""
    if ($a[$best] -lt 0) { $downNote = " NOTE: that axis reads NEGATIVE => it points DOWN; yaw sign will absorb it, but pitch/roll become mirrored (unused on the car)." }
    Log ("vertical axis = " + $name + " (index " + $best + "), " + $a[$best] + " mg" + $downNote)
    Log ("NEXT: run  -Step L2 -Axis " + $best + "   (this also sends a" + $best + " for you)")
    Log ("THEN : write CFG_YAW_AXIS = " + $best + " into config.h and commit (do not leave it only in RAM)")
    Finish "PASS" ("vertical axis = " + $name + " -> CFG_YAW_AXIS=" + $best)
}

# ---------------------------------------------------------------- L2: yaw sign
if ($Step -eq "L2") {
    if ($Sec -le 0) { $Sec = 10 }
    Log "L2: car FLAT and STILL - calibrating gyro bias (k, ~2s). DO NOT TOUCH IT."
    Send-Cmd $p "k"
    $cal = Read-For $p 3500
    Log $cal
    if ($cal -notmatch 'bias done') {
        try { $p.Close() } catch {}
        if ($cal -match 'not ready') { Finish "FAIL" "firmware says IMU not ready (imu_init did not see 0x47)" }
        Finish "INCONCLUSIVE" "no 'bias done' line within 3.5s - old firmware (no k command) or IMU tick not running"
    }
    Send-Cmd $p "o"
    Send-Cmd $p "f50"
    try { $p.DiscardInBuffer() } catch {}
    Log ("NOW: rotate the car LEFT (counter-clockwise, seen from above) by roughly 90 deg, within " + $Sec + "s. GO.")
    $txt = Read-For $p ($Sec * 1000)
    Send-Cmd $p "f100"
    try { $p.Close() } catch {}

    $ys = [regex]::Matches($txt, 'Y:(-?\d+)')
    if ($ys.Count -lt 3) { Log "---- raw ----"; Log $txt; Finish "INCONCLUSIVE" "fewer than 3 telemetry 'Y:' samples - is telemetry running (mode/print period)?" }
    $yaw = [int]$ys[$ys.Count - 1].Groups[1].Value / 10.0
    $peak = 0.0
    foreach ($m in $ys) { $v = [math]::Abs([int]$m.Groups[1].Value / 10.0); if ($v -gt $peak) { $peak = $v } }
    Log ("samples=" + $ys.Count + "  final yaw=" + $yaw + " deg  peak|yaw|=" + $peak + " deg")

    if ($peak -lt 20.0) {
        Finish "INCONCLUSIVE" ("|yaw| never exceeded 20 deg (peak " + $peak + ") - either the car was not rotated, or the AXIS is wrong (rerun L1)")
    }
    if ($yaw -gt 0) {
        Log "THEN: write CFG_YAW_SIGN = (+1) into config.h (and CFG_YAW_AXIS from L1), then commit."
        Finish "PASS" ("left turn gives POSITIVE yaw (+" + $yaw + " deg) -> CFG_YAW_SIGN=+1 is correct")
    }
    Log "FIX: send  s-1  (runtime, no re-flash) and rerun L2 to confirm; then write CFG_YAW_SIGN = (-1) into config.h."
    Finish "FAIL" ("left turn gives NEGATIVE yaw (" + $yaw + " deg) -> sign is inverted, use CFG_YAW_SIGN=-1")
}

# ---------------------------------------------------------------- L3: static drift
if ($Step -eq "L3") {
    if ($Sec -le 0) { $Sec = 60 }
    Log "L3: car FLAT and STILL for the whole run. Calibrating bias (k, ~2s)..."
    Send-Cmd $p "k"
    $cal = Read-For $p 3500
    Log $cal
    if ($cal -notmatch 'bias done') {
        try { $p.Close() } catch {}
        Finish "INCONCLUSIVE" "no 'bias done' line - cannot separate drift from an uncalibrated bias"
    }
    Send-Cmd $p "o"
    Send-Cmd $p "f200"
    try { $p.DiscardInBuffer() } catch {}
    Log ("Measuring static drift for " + $Sec + "s. DO NOT TOUCH THE CAR OR THE TABLE.")
    $txt = Read-For $p ($Sec * 1000)
    Send-Cmd $p "f100"
    try { $p.Close() } catch {}

    $ys = [regex]::Matches($txt, 'Y:(-?\d+)')
    $ws = [regex]::Matches($txt, 'W:(-?\d+)')
    if ($ys.Count -lt 5) { Log "---- raw ----"; Log $txt; Finish "INCONCLUSIVE" "not enough telemetry samples to measure drift" }
    $yaw = [int]$ys[$ys.Count - 1].Groups[1].Value / 10.0
    $rate = $yaw * 60.0 / $Sec
    $wmax = 0.0
    foreach ($m in $ws) { $v = [math]::Abs([int]$m.Groups[1].Value / 100.0); if ($v -gt $wmax) { $wmax = $v } }
    Log ("samples=" + $ys.Count + "  yaw after " + $Sec + "s = " + $yaw + " deg  => drift " + [math]::Round($rate,2) + " deg/min  (peak |wz| " + $wmax + " dps)")

    if ([math]::Abs($rate) -le 2.0) {
        Log "GOOD ENOUGH for short manoeuvres (a 90 deg turn takes ~1s, so this drift is negligible)."
        Log "Record this number in the debug log; keep CFG_GYRO_DEADBAND_DPS at 0 unless you have a reason."
        Finish "PASS" ("static drift " + [math]::Round($rate,2) + " deg/min")
    }
    Log ("FIX candidates, one variable at a time: (1) set CFG_GYRO_DEADBAND_DPS just above peak|wz| (" + $wmax + " dps) ")
    Log  "                                        (2) re-run k with the car really still (table vibration counts)"
    Log  "                                        (3) let the chip warm up 1-2 min, bias drifts with temperature"
    Finish "FAIL" ("static drift " + [math]::Round($rate,2) + " deg/min is too high for open-loop heading holds")
}
