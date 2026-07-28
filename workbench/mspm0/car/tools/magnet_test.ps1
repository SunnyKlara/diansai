# magnet_test.ps1 - first bring-up of the electromagnet channel (ladder 7).
#
# The hardware is already on the v1.3 board (third DRV8231: IN1=PB0, IN2 tied to GND, 12 V,
# current sense on PA24) but PB0 has NEVER been driven before, so this is a first-light test.
#
# BEFORE YOU RUN IT  -- read, this one can destroy hardware
#   1. CHECK THE COIL VOLTAGE RATING. The channel is fed from 12 V with NO current limiting; the
#      only throttle is PWM duty. If the coil is a 6 V or 9 V part, set CFG_MAG_PWM_CAP to
#      (rated / 12) * 100 in config.h and REFLASH before running this. A 6 V coil at 100 % duty
#      on 12 V draws 4x its rated power.
#   2. Nothing ferrous loose near the coil, and keep fingers clear - it will snatch.
#   3. The coil gets hot. The firmware cuts power after CFG_MAG_MAX_ON_MS, this script keeps the
#      on-times short on top of that.
#
# WHAT IT PROVES / DOES NOT PROVE
#   PROVES  : the PWM reaches the driver and the coil is energised (current goes from ~0 to >0).
#   DOES NOT: whether a ball is actually held. An electromagnet's steady current is ~V/R and barely
#             changes with load - what changes is inductance, not the DC average. "Is it holding?"
#             needs the photo-interrupter from the parts list, not the current reading. Do not
#             spend field time trying to read grip strength out of milliamps.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File magnet_test.ps1 -Port COM4
#   powershell -NoProfile -ExecutionPolicy Bypass -File magnet_test.ps1 -Port COM4 -HoldPct 60
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose.
param(
    [string]$Port   = "COM4",
    [int]$Baud      = 115200,
    [int]$HoldPct   = 0,        # >0: also test this fixed duty (for tuning the hold level)
    [double]$OnSec  = 1.5,      # how long to keep it energised per step
    [switch]$Yes,               # skip the interactive confirmation
    [string]$Out    = "magnet_test_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }
function Finish([string]$v, [int]$c) {
    L ""; L "RESULT: $v"
    Set-Content $Out $log.ToString() -Encoding ASCII
    try { $sp.Write("E0`n"); Start-Sleep -Milliseconds 200; $sp.Close(); $sp.Dispose() } catch {}
    exit $c
}

if (-not $Yes) {
    Write-Host "This drives the electromagnet from 12 V with no current limit." -ForegroundColor Yellow
    Write-Host "Confirm you checked the coil voltage rating vs CFG_MAG_PWM_CAP. Type YES to continue:" -ForegroundColor Yellow
    if ((Read-Host) -ne "YES") { Write-Host "aborted"; exit 2 }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }
function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

$script:rxbuf = ''
$script:mag   = New-Object System.Collections.Generic.List[string]
function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i = $script:rxbuf.IndexOf("`n")
        $ln = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)
        if ($ln -match '^\[mag\]\s+state=') { $script:mag.Add($ln) }
    }
}
function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 20 } }
# Every E<n> command makes the firmware print one [mag] line, so "ask and wait for the answer"
# is the confirmation - not the fact that we called Send().
function MagCmd([string]$c, [double]$settle = 0.5) {
    $script:mag.Clear()
    Send $c
    $sw=[System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 2.0 -and $script:mag.Count -eq 0) { Drain; Start-Sleep -Milliseconds 20 }
    Wait $settle
    if ($script:mag.Count -eq 0) { return $null }
    return $script:mag[$script:mag.Count-1]
}
function MagMa([string]$line) { if ($line -match 'I=(-?\d+)mA') { return [int]$Matches[1] } return $null }

L "================ magnet_test  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port"
L "KEEP FINGERS AND LOOSE FERROUS PARTS CLEAR."
L ""

Send "z"; Start-Sleep -Milliseconds 400

# 1) baseline: OFF must read essentially no current
$off = MagCmd "E0" 0.4
if (-not $off) { Finish "INCONCLUSIVE - no [mag] reply. Is this the right port, and is the firmware the new build (boot banner must mention 'MAG: E0')?" 2 }
L ("OFF baseline : {0}" -f $off)
$maOff = MagMa $off
if ($maOff -eq $null) { Finish "INCONCLUSIVE - could not parse the current from the [mag] line." 2 }

# 2) pull in, then let it fall back to the hold duty on its own
L ""
L ("-- E1 : pull in at full duty, then auto-drop to the hold duty ({0}s) --" -f $OnSec)
$on = MagCmd "E1" 0.3
if (-not $on) { Finish "INCONCLUSIVE - no reply to E1." 2 }
L ("PULL         : {0}" -f $on)
$maPull = MagMa $on
Wait $OnSec
$hold = MagCmd "E1" 0.2   # re-ask: by now the state machine should be in HOLD
L ("after {0}s   : {1}" -f $OnSec, $hold)
$maHold = MagMa $hold
Send "E0"; Wait 0.5

# 3) optional fixed duty, for finding the lowest duty that still holds
if ($HoldPct -gt 0) {
    L ""
    L ("-- E{0} : fixed duty ({1}s) --" -f $HoldPct, $OnSec)
    $fx = MagCmd ("E" + $HoldPct) 0.3
    L ("FIXED        : {0}" -f $fx)
    Wait $OnSec
    Send "E0"; Wait 0.5
}

L ""
L "---- result ----"
L ("current OFF / PULL / HOLD : {0} / {1} / {2} mA" -f $maOff, $maPull, $maHold)
L ("noise floor of this ADC channel is +/-40..100 mA (see config.h CUR_AVG_N)")

if ($maPull -eq $null) { Finish "INCONCLUSIVE - no current reading while energised." 2 }
if ($maPull -lt 150) {
    L ""
    L "Energised current is down in the noise => the coil is probably not being driven at all."
    L "Check, in this order:"
    L "  1) is the coil actually connected to the third DRV8231 output (the 'magnet' connector)?"
    L "  2) is the 12 V rail up? (that driver is fed from 12 V directly, not from 3.3 V)"
    L "  3) does the boot banner show the new firmware (the line with 'MAG: E0 ...')?"
    L "  4) PA24 sense: if the coil clearly grabs but current reads ~0, it is the SENSE that is"
    L "     wrong, not the drive - that is a wiring/scaling issue, not a firmware one."
    Finish "FAIL - no meaningful coil current when energised." 1
}
if ($maOff -gt 150) {
    Finish "FAIL - current is high with the magnet OFF ($maOff mA). Something is driving the coil, or the sense channel is reading the wrong thing." 1
}
if ($maHold -ne $null -and $maHold -ge $maPull) {
    L ""
    L "Hold current is not lower than pull current. Either the auto drop to hold duty did not"
    L "happen (check CFG_MAG_PULL_MS / CFG_MAG_HOLD_PCT), or the reading is dominated by noise."
    Finish "INCONCLUSIVE - energised OK, but the pull->hold de-rating is not visible in the current." 2
}
L ""
L "The channel works electrically. What is NOT proven, and must be checked by hand next:"
L "  * does it actually pick up the steel ball, and from what gap"
L "  * does it RELEASE (this channel is single-direction - no reverse pulse to kick the ball off,"
L "    so residual magnetism can keep a small ball stuck; the fix is mechanical, not firmware)"
L "  * the lowest hold duty that still holds: re-run with -HoldPct 40, 30, 25 ... until it drops,"
L "    then go back up by 10-15 % and put that number in CFG_MAG_HOLD_PCT"
Finish "PASS - coil energises and de-rates to hold. Grip/release still needs a hands-on check." 0
