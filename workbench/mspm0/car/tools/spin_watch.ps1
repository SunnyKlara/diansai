# spin_watch.ps1 - drive ONE wheel in a slow on/off pattern long enough for a HUMAN to watch it,
#                  while proving from telemetry that it really turned.
#
# WHY THIS EXISTS
#   motor_probe.ps1 answers "does this channel drive?" in 1.2 s steps - too fast for a person to
#   see WHICH WAY the wheel goes. And the direction question cannot be answered from telemetry
#   alone: positive duty giving negative counts is equally consistent with
#     (a) wheel turns forward + encoder A/B swapped     -> fix the encoder sign
#     (b) wheel turns backward + encoder honest         -> fix the motor polarity
#   Those two need opposite fixes, and picking wrong makes the speed loop POSITIVE feedback
#   (see encoder.c header: that exact bug saturated the loop once already).
#   So: this script gives the human a long, repeating, unambiguous thing to look at, and at the
#   same time records the signed encoder delta so the "software view" is on record next to it.
#
# ONE-SHOT AND TRANSACTIONAL ON PURPOSE
#   Inlining SerialPort code into the agent's persistent shell has already leaked a handle twice
#   ("Access to the port is denied", only fixable by killing the process / replugging USB).
#   A script = its own process = the handle dies with it. Always ends with 'z'.
#
# SAFETY
#   WHEELS MUST BE OFF THE GROUND. Only one motor is driven; the other is held at 0.
#   Each burst re-enters m5, which restarts CFG_RUN_MS_HARDCAP (15 s) - so a burst must stay
#   well under that. Firmware silence timeout (CFG_RUN_MS_DUAL=4000 ms) is the backstop if this
#   script dies mid-burst.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\spin_watch.ps1 -Port COM4 -Motor L
#   ... -Motor R -Duty 25 -OnSec 3 -OffSec 2 -Cycles 20
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in .ps1).

param(
    [string]$Port   = 'COM4',
    [int]$Baud      = 115200,
    # L / R = one wheel only (the still one identifies which is which).
    # B = BOTH at the same positive duty - that is the "does +duty mean FORWARD for the whole car"
    #     check. L/R alone can only prove motor-vs-encoder self-consistency, never that both
    #     sides agree on which way 'forward' is (a car wired consistently BACKWARD looks identical).
    [ValidateSet('L', 'R', 'B')]
    [string]$Motor  = 'L',
    [int]$Duty      = 25,
    [double]$OnSec  = 3.0,
    [double]$OffSec = 2.0,
    [int]$Cycles    = 20,
    [string]$Out    = ''
)

$ErrorActionPreference = 'Continue'
# 'x' drives M1 (left), 'y' drives M2 (right) - direct duty, m5 DUAL mode (see car.c).
$both  = ($Motor -eq 'B')
$drive = if ($Motor -eq 'R') { 'y' } else { 'x' }
$idle  = if ($Motor -eq 'R') { 'x' } else { 'y' }

$log = New-Object System.Collections.ArrayList
function L([string]$s) { [void]$log.Add($s); Write-Host $s }

L ("================ spin_watch  " + (Get-Date -Format 'HH:mm:ss') + " ================")
L ("port $Port   motor $Motor ('$drive')   duty ${Duty}%   ${OnSec}s on / ${OffSec}s off   x$Cycles")
L ("total run time  : " + [math]::Round($Cycles * ($OnSec + $OffSec), 0) + " s")
L 'WHEELS MUST BE OFF THE GROUND.'
L ''

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200

# char-by-char with a gap: a single burst write overruns the MCU RX FIFO (repo pitfall).
function Send-Cmd($port, [string]$cmd) {
    foreach ($ch in $cmd.ToCharArray()) { $port.Write([string]$ch); Start-Sleep -Milliseconds 20 }
    $port.Write([string][char]10)
}
function Read-For($port, [double]$sec) {
    $sb = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        try { [void]$sb.Append($port.ReadExisting()) } catch { }
        Start-Sleep -Milliseconds 60
    }
    $sb.ToString()
}
# telemetry tail: ... | V:<rpm1>,<rpm2> | PWM:<d1>,<d2> | C:<c1>,<c2> | ...
function Last-Match([string]$text, [string]$pat) {
    $m = [regex]::Matches($text, $pat)
    if ($m.Count -eq 0) { return $null }
    return $m[$m.Count - 1]
}

$rc = 2
try {
    $sp.Open()
    Start-Sleep -Milliseconds 400
    try { $sp.DiscardInBuffer() } catch { }

    Send-Cmd $sp 'z'
    Send-Cmd $sp 'm5'
    Send-Cmd $sp ($idle + '0')
    Send-Cmd $sp ($drive + '0')
    $pre = Read-For $sp 0.9
    $mc  = Last-Match $pre 'C:(-?\d+),(-?\d+)'
    if (-not $mc) { L 'RESULT: INCONCLUSIVE - no telemetry on this port (link down? run esp_pc_up.ps1)'; exit 2 }
    $c1First = [int]$mc.Groups[1].Value
    $c2First = [int]$mc.Groups[2].Value
    L ("baseline        : C1=$c1First  C2=$c2First")
    L ''
    L 'cycle   PWM     |rpm| driven   dC1      dC2'

    $c1Prev = $c1First
    $c2Prev = $c2First
    $spun = 0
    for ($i = 1; $i -le $Cycles; $i++) {
        Send-Cmd $sp 'z'
        Send-Cmd $sp 'm5'
        if ($both) { Send-Cmd $sp ($idle + [string]$Duty) } else { Send-Cmd $sp ($idle + '0') }
        Send-Cmd $sp ($drive + [string]$Duty)
        $txt = Read-For $sp $OnSec
        if ($both) { Send-Cmd $sp ($idle + '0') }
        Send-Cmd $sp ($drive + '0')
        Send-Cmd $sp 'z'
        $off = Read-For $sp $OffSec

        $mv = Last-Match $txt 'V:(-?\d+),(-?\d+)'
        $mp = Last-Match $txt 'PWM:(-?\d+),(-?\d+)'
        $mc = Last-Match $off 'C:(-?\d+),(-?\d+)'
        $rpm = '?'
        if ($mv) {
            $v1 = [int]$mv.Groups[1].Value; $v2 = [int]$mv.Groups[2].Value
            $rpm = if ($both) { "$([math]::Abs($v1))/$([math]::Abs($v2))" }
                   elseif ($Motor -eq 'L') { [math]::Abs($v1) } else { [math]::Abs($v2) }
            $mag = if ($both) { [math]::Min([math]::Abs($v1), [math]::Abs($v2)) }
                   elseif ($Motor -eq 'L') { [math]::Abs($v1) } else { [math]::Abs($v2) }
            if ($mag -gt 5) { $spun++ }
        }
        $pwm = if ($mp) { "$($mp.Groups[1].Value),$($mp.Groups[2].Value)" } else { '?' }
        $d1 = ''; $d2 = ''
        if ($mc) {
            $c1 = [int]$mc.Groups[1].Value; $c2 = [int]$mc.Groups[2].Value
            $d1 = $c1 - $c1Prev; $d2 = $c2 - $c2Prev
            $c1Prev = $c1; $c2Prev = $c2
        }
        L ("{0,5}   {1,-7} {2,11}   {3,7}  {4,7}" -f $i, $pwm, $rpm, $d1, $d2)
    }

    $dTot1 = $c1Prev - $c1First
    $dTot2 = $c2Prev - $c2First
    L ''
    L '---- software view (what the firmware thinks) ----'
    if ($both) {
        L ("left  total dC : $dTot1")
        L ("right total dC : $dTot2")
        $ratio = if ($dTot2 -ne 0) { '{0:N3}' -f ($dTot1 / [double]$dTot2) } else { 'n/a' }
        L ("both positive? : " + $(if ($dTot1 -gt 0 -and $dTot2 -gt 0) { 'YES - matches the convention (+duty => +counts on both)' } else { 'NO (!) - convention broken' }) + "   L/R ratio $ratio")
    } else {
        $dDriven = if ($Motor -eq 'L') { $dTot1 } else { $dTot2 }
        $dOther  = if ($Motor -eq 'L') { $dTot2 } else { $dTot1 }
        L ("driven wheel ($Motor) total dC : $dDriven    -> sign is " + $(if ($dDriven -lt 0) { 'NEGATIVE at positive duty' } elseif ($dDriven -gt 0) { 'POSITIVE at positive duty' } else { 'ZERO (!)' }))
        L ("other wheel        total dC : $dOther     -> must be ~0 (cross-channel independence)")
    }
    L ("cycles that actually spun    : $spun / $Cycles")
    L ''
    L '---- what only a HUMAN can answer ----'
    if ($both) {
        L 'BOTH wheels are driven at the same positive duty. Watch them:'
        L '  both toward the FRONT -> convention confirmed end to end; go run m7'
        L '  both toward the REAR  -> flip BOTH CFG_MOT_SIGN_* in config.h and reflash'
        L '  one each way          -> the per-side signs are wrong; re-measure L and R separately'
    } else {
        L 'Watch the TOP of the driven wheel: does it move toward the FRONT or the REAR of the car?'
        L '  FRONT + negative dC  -> encoder A/B swapped   -> flip the sign in encoder.c'
        L '  REAR  + negative dC  -> motor polarity wrong  -> fix the motor (encoder is honest)'
    }
    if ($spun -eq 0) { L ''; L 'RESULT: FAIL - the wheel never reported rpm; nothing to look at'; $rc = 1 }
    elseif ($spun -lt $Cycles) { L ''; L "RESULT: INCONCLUSIVE - only $spun/$Cycles bursts spun"; $rc = 2 }
    else { L ''; L 'RESULT: PASS - wheel spun on every burst; direction is now a human call'; $rc = 0 }
}
catch {
    L "RESULT: FAIL - $($_.Exception.Message)"
    $rc = 1
}
finally {
    try { Send-Cmd $sp 'z' } catch { }
    try { $sp.Close() } catch { }
    if ($Out -ne '') { $log | Out-File -FilePath $Out -Encoding UTF8 }
}
exit $rc
