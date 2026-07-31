# _serial_ball.ps1 - shared serial plumbing for the ball/beam bring-up scripts. Dot-source it.
#
# WHY A SEPARATE FILE
#   ball_ident.ps1 and ball_signs.ps1 both need: the BALL: line regex, per-frame de-duplication, the
#   id=-1 filter, and command pacing. Those are correctness-critical, not boilerplate:
#     * miss the de-dupe  -> the same camera frame is counted several times as "zero velocity" samples
#     * miss the id filter -> "no ball in view" (cx=0) is fitted as "ball parked at the centre"
#     * miss the pacing    -> a burst write overruns the MCU's 4-byte RX FIFO and drops characters
#   Copying that into a second script is how the two quietly drift apart. One implementation instead.
#
# The caller must define $sp (an open SerialPort) before dot-sourcing, and provide an L() logger.
# ASCII only on purpose.

# 25 ms/char: a single burst write overruns the MCU RX FIFO (see uart_send.ps1 in the SSOT notes).
function Send([string]$cmd) { foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 } }

$script:rxbuf   = ''
$script:samples = New-Object System.Collections.Generic.List[object]
$script:lastStamp = -1
$script:notSeen = 0
$script:lines   = 0
$script:imu     = New-Object System.Collections.Generic.List[object]
$script:caught  = New-Object System.Collections.Generic.List[object]   # AskLines: raw diagnostic lines
$script:catchRe = $null                                                # set only while AskLines is active
$script:lastGuardHit = $false

# BALL:<cx>,<us>,<age_ms>,<stamp_ms>,<id>   appended by car.c whenever the vision link has had a frame.
$script:reBall  = [regex]'BALL:(?<cx>-?\d+),(?<us>-?\d+),(?<age>\d+),(?<st>\d+),(?<id>-?\d+)'
# [imu] pitch0.1deg=<n> roll0.1deg=<n>      printed by the `g` command (added 2026-07-31)
$script:reImu   = [regex]'pitch0\.1deg=(?<p>-?\d+)\s+roll0\.1deg=(?<r>-?\d+)'

# Throw away whatever the OS driver has queued, plus our own half-assembled line.
#
# 🔴 This is the step the pit-list (跨题坑库 L173) already prescribed and these scripts were missing:
#   "开口后先 DiscardInBuffer + 丢弃首行再采; 别把陈缓冲里的旧读数当成当前测量"
# Every serial script in this repo that has ever been validated on hardware (probe_serial.ps1,
# read_serial.ps1) calls it; all four of the new ball scripts did not. The cost of the omission was
# three separate false failures in one bring-up session, each of which I then tried to explain with a
# fresh theory instead of looking the answer up. At 40 Hz telemetry the driver holds ~16 lines per
# 400 ms pause, and every one of them describes the state BEFORE whatever we just did.
function FlushRx() {
    try { $sp.DiscardInBuffer() } catch {}
    $script:rxbuf = ''
}

function Drain() {
    $txt = ""
    try { $txt = $sp.ReadExisting() } catch {}
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i  = $script:rxbuf.IndexOf("`n")
        $ln = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)

        $mi = $script:reImu.Match($ln)
        if ($mi.Success) {
            $script:imu.Add([pscustomobject]@{ pitch = [double]$mi.Groups['p'].Value / 10.0
                                               roll  = [double]$mi.Groups['r'].Value / 10.0 })
            continue
        }
        # Optional catch hook: telemetry parsing below throws away everything that is not [ctl], which is
        # right for sampling but wrong when we want to read a diagnostic reply ([vs], [srv], [ball]...).
        # Set catchRe before sending the command; the raw lines land in $script:caught.
        if ($script:catchRe -and $ln -match $script:catchRe) { [void]$script:caught.Add($ln) }
        if ($ln -notmatch '^\[ctl\]') { continue }
        $script:lines++
        $m = $script:reBall.Match($ln)
        if (-not $m.Success) { continue }
        $st = [int]$m.Groups['st'].Value
        # De-dupe on the frame stamp: telemetry runs faster than the camera, so the same frame appears
        # in several lines. Duplicates would be fitted as genuine "the ball did not move" samples.
        if ($st -eq $script:lastStamp) { continue }
        $script:lastStamp = $st
        # id == -1 means "link fine, nothing in view", and cx is 0 in that frame - it must NOT become a
        # sample. Counted apart: high notSeen with normal age points at ROI/lighting, high age points at
        # the link. Different faults, different fixes.
        if ([int]$m.Groups['id'].Value -eq -1) { $script:notSeen++; continue }
        $script:samples.Add([pscustomobject]@{ t   = $st / 1000.0
                                               cx  = [int]$m.Groups['cx'].Value
                                               us  = [int]$m.Groups['us'].Value
                                               age = [int]$m.Groups['age'].Value })
    }
}

function Wait([double]$s) { $sw=[System.Diagnostics.Stopwatch]::StartNew(); while ($sw.Elapsed.TotalSeconds -lt $s) { Drain; Start-Sleep -Milliseconds 15 } }

# Clear the SAMPLE LIST but deliberately KEEP lastStamp.
#
# 🔴 It used to reset lastStamp to -1, and that single line caused every false failure in this bring-up:
# the firmware holds g_uf.last until a new frame parses and re-reports it every 25 ms, so with lastStamp
# wiped the very next line - a repeat of the OLD frame - is accepted as brand new. It made check 4b read
# the previous check's cx=5230, and it made check 7 count a leftover injected frame as camera traffic
# (a FALSE PASS, which is worse). lastStamp is a monotonic frame identity; there is never a reason to
# forget it. Pairs with FlushRx, which drops the driver backlog (跨题坑库 L173).
function ClearSamples() { $script:samples.Clear() }

# Collect fresh camera samples for ms. $stopCx>0 = bail out once the ball has travelled that far;
# $guardCx>0 = bail out (and flag) if |x| exceeds it, so the ball never sits against an end stop.
function Collect([int]$ms, [double]$stopCx, [double]$guardCx) {
    FlushRx                     # start every capture from a clean driver buffer, not from history
    ClearSamples
    $script:lastGuardHit = $false
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        Drain
        if ($script:samples.Count -ge 3) {
            $cur = $script:samples[$script:samples.Count-1].cx
            # Guard first: staying off the end stop matters more than a full-length capture.
            if ($guardCx -gt 0 -and [Math]::Abs($cur) -ge $guardCx) { $script:lastGuardHit = $true; break }
            if ($stopCx -gt 0 -and $script:samples.Count -ge 8) {
                if ([Math]::Abs($cur - $script:samples[0].cx) -ge $stopCx) { break }
            }
        }
        Start-Sleep -Milliseconds 10
    }
    Drain
    return ,@($script:samples.ToArray())
}

# Latest ball position (mm) plus a velocity estimate (mm/s).
# Velocity over a 3-sample span, not 2: at 24 fps with +-0.2 mm noise a 2-sample difference carries
# ~7 mm/s of noise, which a kd term would turn into visible chatter.
function LatestXV([double]$scaleMm) {
    Drain
    $n = $script:samples.Count
    if ($n -lt 1) { return $null }
    $x = [double]$script:samples[$n-1].cx * $scaleMm
    $v = 0.0
    if ($n -ge 3) {
        $i0 = $n - 3
        $dt = $script:samples[$n-1].t - $script:samples[$i0].t
        if ($dt -gt 0.01) { $v = ([double]$script:samples[$n-1].cx - [double]$script:samples[$i0].cx) * $scaleMm / $dt }
    }
    return [pscustomobject]@{ x = $x; v = $v; n = $n }
}

# Send a command and collect every reply line matching $re for up to $timeoutS. Returns the raw lines.
# Used for the diagnostic dumps (V / ? / [srv] / [ball]) that Drain's telemetry filter would discard.
function AskLines([string]$cmd, [string]$re, [double]$timeoutS) {
    $script:caught.Clear()
    $script:catchRe = $re
    try {
        FlushRx                 # a reply must be read AFTER the request, never out of the backlog (L173)
        Send $cmd
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        # Keep draining for the whole window even after the first hit: these dumps are multi-line and
        # stopping at line 1 would silently drop the part that carries the verdict.
        while ($sw.Elapsed.TotalSeconds -lt $timeoutS) { Drain; Start-Sleep -Milliseconds 20 }
    } finally { $script:catchRe = $null }
    return ,@($script:caught.ToArray())
}

# Send a command and return as soon as the FIRST matching reply arrives. Unlike AskLines this is for
# single-line acknowledgements, not diagnostic dumps. Registering catchRe before Send closes the race
# where a fast board replies while the script is still switching into receive mode.
function SendAndWaitLine([string]$cmd, [string]$re, [double]$timeoutS) {
    $script:caught.Clear()
    $script:catchRe = $re
    try {
        FlushRx
        Send $cmd
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $timeoutS -and $script:caught.Count -eq 0) {
            Drain
            if ($script:caught.Count -eq 0) { Start-Sleep -Milliseconds 10 }
        }
        if ($script:caught.Count -eq 0) { return $null }
        return [string]$script:caught[0]
    } finally { $script:catchRe = $null }
}

# Ask the board for one IMU dump and return the parsed pitch/roll (degrees), or $null on timeout.
function ReadPitch([double]$timeoutS) {
    $script:imu.Clear()
    Send "g"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeoutS -and $script:imu.Count -eq 0) { Drain; Start-Sleep -Milliseconds 20 }
    if ($script:imu.Count -eq 0) { return $null }
    return $script:imu[$script:imu.Count-1]
}

# ---- frame injection: let the PC pretend to be the camera --------------------------------------
# The firmware routes any line starting with '$' straight into the frame parser instead of the command
# parser (car.c vision_grab), on BOTH the wired and the wireless port. So every link stage AFTER the
# UART - parser, unit conversion, BALL: telemetry, ball control - can be proven on real hardware with
# no camera attached. What that leaves unproven is exactly one thing: whether bytes physically arrive
# on PB16. Splitting the camera question into "protocol" and "wiring" is worth a lot, because those two
# have completely different fixes and the symptom (no ball data) is identical.

# XOR of everything between '$' and '*' - same rule as uf_checksum() in uart_frame.c.
function UfChecksum([string]$body) {
    $x = 0
    foreach ($ch in $body.ToCharArray()) { $x = $x -bxor [int][char]$ch }
    return $x
}
function WrapFrame([string]$body, [switch]$Corrupt) {
    $x = UfChecksum $body
    if ($Corrupt) { $x = $x -bxor 0xFF }
    return ('$' + $body + '*' + ('{0:X2}' -f $x))
}
# Our own format (pi_vision / vision_test.ps1 use it). cx carries x_mm*100.
function FrameV([int]$id, [int]$cx, [switch]$Corrupt) { return (WrapFrame ("V,$id,$cx,0,0") -Corrupt:$Corrupt) }
# The format the K230 delivery V4 actually sends: position in CENTIMETRES with 2 decimals.
# Built as a string on purpose - the whole point is to exercise the firmware's decimal parser.
function FrameBP([int]$valid, [double]$cm, [switch]$Corrupt) {
    # THREE format sections on purpose: positive;negative;ZERO. The device signs positives ('+5.23') and
    # negatives ('-12.00') but leaves zero bare ('0.00') - both spellings appear verbatim in the delivery
    # doc. A two-section format sends '+0.00' for zero, which is a DIFFERENT byte string and therefore a
    # different checksum, so the injected frame would stop being a faithful rehearsal of the real device.
    # Caught by comparing against the doc's literal checksums (*12 and *3C) instead of trusting our own
    # arithmetic - which is exactly why those two literals are worth keeping as a fixture.
    return (WrapFrame ("BP,{0},{1}" -f $valid, ($cm.ToString("+0.00;-0.00;0.00", [System.Globalization.CultureInfo]::InvariantCulture))) -Corrupt:$Corrupt)
}
# Frame injection pacing, in ms per character. 0 = one burst write.
#
# 🔴 REAL-MACHINE FINDING (2026-07-31): a burst write does NOT get through. The MCU's UART RX FIFO is
# only 4 bytes deep and one byte takes 87 us at 115200, so an 18-byte frame arrives in ~1.6 ms - while
# the main loop only polls about once per millisecond (delay_ms(1) plus the LCD redraw). Each poll drains
# the FIFO, but between polls more than 4 bytes land and the surplus is simply lost. The frame then never
# completes and the parser never fires. This is the SAME wall that killed the serial line-sensor link
# (rx=386308 but bad=6200, documented in the SSOT) - it was always going to apply here too.
# vision_test.ps1 uses a burst write and its comment claims a real camera would not pace either; that
# script has never actually run against hardware, so the claim was never tested.
$script:framePaceMs = 2
function SendFrame([string]$f) {
    $s = $f + "`n"
    if ($script:framePaceMs -le 0) { $sp.Write($s); return }
    foreach ($ch in $s.ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds $script:framePaceMs }
}

# Telemetry at 25 ms and ONE sink. Both matter:
#   period  - must be faster than the camera (~24 fps / 42 ms) or samples are lost before we see them
#   one sink - a calibration line is ~120 bytes = ~10.5 ms of TX at 115200; dual-send needs ~21 ms and
#              does not fit in a 25 ms period, so the UART falls behind and lines get truncated.
# Returns the sink actually used, or $null if no telemetry appeared at all.
function SetupTelemetry() {
    FlushRx                                    # 坑库 L173: discard whatever was queued before we opened
    Send "z"; Start-Sleep -Milliseconds 300
    Send "f25"; Start-Sleep -Milliseconds 250
    Send "l1"; Start-Sleep -Milliseconds 250
    $script:lines = 0
    [void](Collect 900 0 0)
    if ($script:lines -gt 0) { return "l1 (wired only)" }
    Send "l2"; Start-Sleep -Milliseconds 250
    $script:lines = 0
    [void](Collect 900 0 0)
    if ($script:lines -gt 0) { return "l2 (wireless only)" }
    return $null
}
