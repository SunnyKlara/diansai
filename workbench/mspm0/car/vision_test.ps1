# vision_test.ps1 - test the whole visual-servo chain WITHOUT a camera (ladder 6).
#
# WHY THIS EXISTS
#   The camera link is the weakest part of the project and the module is not even in hand. But the
#   half that lives on the car - frame parsing, freshness handling, and the servo control law - can
#   be verified today: the firmware routes any line starting with '$' into the frame parser instead
#   of the command parser, so THIS SCRIPT plays the part of the camera.
#   That turns "we have no idea if the vision path works" into "the car side is verified, only the
#   camera itself is unknown", which is a much smaller thing to do on the day.
#
# SAFETY
#   *** WHEELS OFF THE GROUND. *** This drives the motors from fake sensor data. Put the car on a
#   stand/box. The direction checks below only need the wheels to spin, not the car to move.
#
# WHAT IT CHECKS
#   1. frames are accepted at all (ok counter goes up, no checksum/format errors)
#   2. a bad checksum is rejected AND counted separately (proves the checksum is really checked)
#   3. target on the left  -> car turns LEFT   (left wheel slower / right faster)
#   4. target on the right -> car turns RIGHT
#   5. target centred and far -> car drives FORWARD
#   6. target centred and close (area >= stop) -> car STOPS and reports ALIGNED
#   7. stop sending frames -> car STOPS by itself (stale data must never be driven on)
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File vision_test.ps1 -Port COM4
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose.
param(
    [string]$Port     = "COM4",
    [int]$Baud        = 115200,
    [int]$CenterX     = 320,     # must match CFG_VS_CENTER_X
    [int]$AreaStop    = 4000,    # must match CFG_VS_AREA_STOP
    [double]$StepSec  = 1.5,
    [int]$FrameMs     = 50,      # 20 Hz, a plausible camera rate and well inside CFG_VS_LOST_MS
    [switch]$Yes,
    [string]$Out      = "vision_test_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }
function Finish([string]$v, [int]$c) {
    L ""; L "RESULT: $v"
    Set-Content $Out $log.ToString() -Encoding ASCII
    try { foreach ($ch in "z`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }; $sp.Close(); $sp.Dispose() } catch {}
    exit $c
}

if (-not $Yes) {
    Write-Host "WHEELS MUST BE OFF THE GROUND - this drives the motors from fake vision data." -ForegroundColor Yellow
    Write-Host "Type YES to continue:" -ForegroundColor Yellow
    if ((Read-Host) -ne "YES") { Write-Host "aborted"; exit 2 }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

# Same XOR checksum as uart_frame.c: XOR of everything between '$' and '*'.
function VFrame([int]$id, [int]$cx, [int]$cy, [int]$area, [switch]$Corrupt) {
    $body = "V,$id,$cx,$cy,$area"
    $x = 0
    foreach ($ch in $body.ToCharArray()) { $x = $x -bxor [int][char]$ch }
    if ($Corrupt) { $x = $x -bxor 0xFF }
    return ('$' + $body + '*' + ('{0:X2}' -f $x))
}
# Frames go out as one write: they are not commands, so the 25 ms/char pacing (which exists to
# protect the command path from FIFO overrun) is not needed, and a 20 Hz camera would not do it.
function SendFrame([string]$f) { $sp.Write($f + "`n") }

$script:rxbuf = ''
$script:vs    = New-Object System.Collections.Generic.List[string]
$script:evt   = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]
$reTelem = [regex]'\[ctl\]\s+(?<mode>\S+).*?\|\s*PWM:(?<p1>-?\d+),(?<p2>-?\d+).*?\|\s*D:(?<dv>-?\d+),(?<dw>-?\d+)'

function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i = $script:rxbuf.IndexOf("`n")
        $ln = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)
        if ($ln -match '^\[vs\]\s+status=')            { $script:vs.Add($ln) }
        if ($ln -match '^\[vs\]\s+(ALIGNED|TARGET LOST)') { $script:evt.Add($ln) }
        $m = $reTelem.Match($ln)
        if ($m.Success) {
            $g = $m.Groups
            $rows.Add([pscustomobject]@{ mode=$g['mode'].Value; pwm1=[int]$g['p1'].Value; pwm2=[int]$g['p2'].Value
                                          dv=[int]$g['dv'].Value; dw=[int]$g['dw'].Value })
        }
    }
}
function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 20 } }

# Stream a fixed target for N seconds at FrameMs, then report what the car did.
function Feed([int]$cx, [int]$area, [double]$sec) {
    $rows.Clear()
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        SendFrame (VFrame 1 $cx 240 $area)
        Wait ($FrameMs / 1000.0)
    }
    Drain
    if ($rows.Count -eq 0) { return $null }
    # Take the median of dw so one transient frame cannot decide the verdict
    $dws = ($rows | ForEach-Object { $_.dw } | Sort-Object)
    $dvs = ($rows | ForEach-Object { $_.dv } | Sort-Object)
    return [pscustomobject]@{ n=$rows.Count; mode=$rows[$rows.Count-1].mode
                              dw=$dws[[int]($dws.Count/2)]; dv=$dvs[[int]($dvs.Count/2)] }
}
function VStatus() {
    $script:vs.Clear(); Send "V"
    $sw=[System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 2.0 -and $script:vs.Count -eq 0) { Drain; Start-Sleep -Milliseconds 20 }
    if ($script:vs.Count -eq 0) { return $null }
    return $script:vs[$script:vs.Count-1]
}

$fails = 0
function Check([string]$name, [bool]$ok, [string]$detail) {
    if ($ok) { L ("  [PASS] {0,-42} {1}" -f $name, $detail) }
    else     { L ("  [FAIL] {0,-42} {1}" -f $name, $detail); $script:fails++ }
}

L "================ vision_test (no camera needed)  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port   center_x=$CenterX   area_stop=$AreaStop   frame rate ~$([int](1000/$FrameMs)) Hz"
L "WHEELS OFF THE GROUND."
L ""

Send "z"; Start-Sleep -Milliseconds 400
Send "f50"; Start-Sleep -Milliseconds 300

# --- 1. are frames accepted at all -------------------------------------------------
$before = VStatus
if (-not $before) { Finish "INCONCLUSIVE - no reply to the V command. Wrong port, or the firmware predates the vision support (boot banner must contain 'VIS: m10')." 2 }
L ("vision status before : {0}" -f $before)
$okBefore = if ($before -match 'ok=(\d+)') { [int]$Matches[1] } else { 0 }

for ($i = 0; $i -lt 10; $i++) { SendFrame (VFrame 1 $CenterX 240 1000); Start-Sleep -Milliseconds $FrameMs }
$after = VStatus
L ("vision status after 10 good frames : {0}" -f $after)
$okAfter  = if ($after -match 'ok=(\d+)')       { [int]$Matches[1] } else { 0 }
$csumBad  = if ($after -match 'bad_csum=(\d+)') { [int]$Matches[1] } else { -1 }
$formBad  = if ($after -match 'bad_form=(\d+)') { [int]$Matches[1] } else { -1 }
$ovf      = if ($after -match 'overflow=(\d+)') { [int]$Matches[1] } else { -1 }

L ""
L "---- checks ----"
Check "frames accepted" (($okAfter - $okBefore) -ge 8) "ok went $okBefore -> $okAfter"
Check "no format errors" ($formBad -eq 0) "bad_form=$formBad"
Check "no overflow (baud rate sane)" ($ovf -eq 0) "overflow=$ovf"
if (($okAfter - $okBefore) -lt 8) {
    L ""
    L "Frames are not getting in. The most likely causes, in order:"
    L "  1) firmware is an older build without the '\$' routing (check the boot banner)"
    L "  2) something else is holding this COM port"
    Finish "INCONCLUSIVE - the frame path itself does not work; the servo checks would be meaningless." 2
}

# --- 2. a corrupt checksum must be rejected and counted apart ----------------------
$c0 = $csumBad
for ($i = 0; $i -lt 5; $i++) { SendFrame (VFrame 1 100 240 1000 -Corrupt); Start-Sleep -Milliseconds $FrameMs }
$after2 = VStatus
$c1 = if ($after2 -match 'bad_csum=(\d+)') { [int]$Matches[1] } else { -1 }
$f1 = if ($after2 -match 'bad_form=(\d+)') { [int]$Matches[1] } else { -1 }
Check "bad checksum rejected" (($c1 - $c0) -ge 4) "bad_csum $c0 -> $c1"
Check "and NOT filed as a format error" ($f1 -eq $formBad) "bad_form still $f1"

# --- 3..6 servo direction and states ----------------------------------------------
Send "m10"; Start-Sleep -Milliseconds 300

$left = Feed ($CenterX - 200) 1000 $StepSec
if (-not $left) { Finish "INCONCLUSIVE - no telemetry while servoing." 2 }
Check "target LEFT  -> turns left (w>0)"  ($left.dw -gt 0)  ("D=" + $left.dv + "," + $left.dw + " mode=" + $left.mode)

$right = Feed ($CenterX + 200) 1000 $StepSec
Check "target RIGHT -> turns right (w<0)" ($right.dw -lt 0) ("D=" + $right.dv + "," + $right.dw)

$far = Feed $CenterX 1000 $StepSec
Check "centred + far -> drives forward (v>0)" ($far.dv -gt 0) ("D=" + $far.dv + "," + $far.dw)

$script:evt.Clear()
$near = Feed $CenterX ($AreaStop + 500) $StepSec
$aligned = ($script:evt | Where-Object { $_ -match 'ALIGNED' }).Count -gt 0
Check "centred + close -> ALIGNED and stops" ($aligned -and $near.dv -eq 0) ("aligned=" + $aligned + " D=" + $near.dv + "," + $near.dw)

# --- 7. stop feeding: the car must stop by itself ---------------------------------
Send "m10"; Start-Sleep -Milliseconds 200
for ($i = 0; $i -lt 10; $i++) { SendFrame (VFrame 1 ($CenterX - 200) 240 1000); Start-Sleep -Milliseconds $FrameMs }
$script:evt.Clear(); $rows.Clear()
Wait 2.0                     # send nothing at all
Drain
$lost = ($script:evt | Where-Object { $_ -match 'TARGET LOST' }).Count -gt 0
$lastRow = if ($rows.Count -gt 0) { $rows[$rows.Count-1] } else { $null }
$stopped = ($lastRow -ne $null -and $lastRow.pwm1 -eq 0 -and $lastRow.pwm2 -eq 0)
Check "frames stop -> car stops itself" ($lost -or $stopped) ("lost_msg=" + $lost + " pwm=" + $(if($lastRow){"$($lastRow.pwm1),$($lastRow.pwm2)"}else{"n/a"}))
# This is the one failure mode that drives the car into a wall: if the camera dies and the car
# keeps steering on the last coordinate it had, it will not stop on its own.

Send "z"; Wait 0.8
Drain
$final = if ($rows.Count -gt 0) { $rows[$rows.Count-1] } else { $null }
L ""
L ("final mode / PWM : {0}" -f $(if ($final) { "$($final.mode) / $($final.pwm1),$($final.pwm2)" } else { "no telemetry" }))

L ""
L "NOT proven by this script (it fakes the camera):"
L "  * that a real camera can produce these frames at a usable rate"
L "  * the pixel->RPM gain (CFG_KP_VS_W) and the stop distance (CFG_VS_AREA_STOP) - both need the"
L "    real camera and a real target; AREA_STOP in particular IS the stop distance, so park the car"
L "    where you want it to stop, read the area, and put that number in config.h"
if ($fails -gt 0) { Finish "FAIL - $fails check(s) failed (see above)." 1 }
Finish "PASS - frame path and servo law behave correctly on the car with faked camera data." 0
