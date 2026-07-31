# ball_bringup.ps1 - one command, 7 checks, three-state each. Run this FIRST after flashing.
#
# WHY THIS EXISTS
#   The image being flashed carries SIX new things at once: VIS_UART (UART2/PB15-16), the $BP parser,
#   the BALL:/BE: telemetry fields, the l0 sink switch, pitch/roll printing, and the RUN-page ball line.
#   Jumping straight to ball_ident.ps1 after flashing would break the repo's own single-variable rule:
#   if nothing works, six candidates are equally guilty and each is a different fix. This script walks
#   them one at a time so a failure names itself.
#
#   Five of the seven need NO CAMERA. The firmware routes '$'-prefixed lines into the frame parser
#   (car.c vision_grab), so the PC can pretend to be the camera and prove protocol, unit conversion and
#   telemetry before a single wire goes to PB16. That reduces the camera to one narrow question -
#   "do bytes physically arrive?" - instead of it blocking everything.
#
# WHAT EACH CHECK PROVES (and what its failure means)
#   1 firmware identity  - the chip really runs THIS build      -> fail: flash did not take
#   2 pitch/roll print   - assumption A7 becomes measurable     -> fail: old build, or IMU dead
#   3 $V  injection      - '$' routing + BALL: + cx passthrough  -> fail: telemetry field or routing
#   4 $BP injection      - K230 protocol + the x1000 conversion  -> fail: parser (NOT wiring)
#   5 l0 / l3            - Q62 compliance switch works          -> fail: sink logic
#   6 RUN page ball line - the only channel during a scored run  -> needs your eyes
#   7 real camera        - bytes actually arrive on PB16         -> fail: wiring / level / K230 not running
#
# SAFETY
#   Nothing here drives the motors, and the beam is only touched if you pass -UsCenter (it is parked
#   there at the end). Safe to run with the car on a bench.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_bringup.ps1 -Port COM30
#   powershell -NoProfile -ExecutionPolicy Bypass -File ball_bringup.ps1 -Port COM4 -SkipEyes
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE
# ASCII only on purpose.
param(
    [string]$Port     = "COM30",
    [int]$Baud        = 115200,
    [double]$CxPerMm  = 100.0,   # fixed unit conversion (cx carries x_mm*100), see config.h 7.12
    [int]$UsCenter    = 0,       # optional: park the beam here at the end
    [switch]$SkipEyes,           # skip check 6 (the one that needs a human to look at the LCD)
    [switch]$SkipCamera,         # skip check 7 (camera not wired yet)
    [string]$K230     = "",      # K230 console port - lets checks 3/4 pause the camera (see below)
    [string]$Out      = "_logs\ball_bringup_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = $null
$script:nPass = 0; $script:nFail = 0; $script:nSkip = 0; $script:nIncon = 0
# INCONCLUSIVE is deliberately NOT folded into SKIP. A skip is a decision ("-SkipEyes given"); an
# inconclusive is a check that ran and could not decide, which must never let the script exit PASS -
# that is exactly how a false PASS gets believed.
function Verdict([string]$name, [string]$state, [string]$detail) {
    switch ($state) {
        "PASS"         { L ("  [PASS] {0,-34} {1}" -f $name, $detail); $script:nPass++ }
        "FAIL"         { L ("  [FAIL] {0,-34} {1}" -f $name, $detail); $script:nFail++ }
        "INCONCLUSIVE" { L ("  [??? ] {0,-34} {1}" -f $name, $detail); $script:nIncon++ }
        default        { L ("  [----] {0,-34} {1}" -f $name, $detail); $script:nSkip++ }
    }
}

function Finish([string]$v, [int]$c) {
    L ""; L "RESULT: $v"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
    } catch { Write-Host "(could not write $Out)" }
    if ($sp) {
        # ALWAYS restore l3 - check 5 deliberately silences the board, and leaving it that way would make
        # the next script look at a dead port and blame the hardware.
        try {
            $cmds = @("l3","f100")
            if ($UsCenter -gt 0) { $cmds = @("U" + $UsCenter) + $cmds }
            foreach ($cmd in $cmds) {
                foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
                Start-Sleep -Milliseconds 150
            }
            $sp.Close(); $sp.Dispose()
        } catch {}
    }
    exit $c
}

# Dependencies FIRST, hardware second - a missing helper should fail before anyone wires anything up.
. (Join-Path $PSScriptRoot "_fit.ps1")
. (Join-Path $PSScriptRoot "_serial_ball.ps1")

# ---- pausing the camera for the injection checks -----------------------------------------------
# Checks 3/4/4b inject a fake frame and read back what the firmware made of it. Once the camera runs
# for real at ~25 fps, the injected frame is overwritten 40 ms later, so the check reads the REAL ball
# position and reports a mismatch - a harness race that looks exactly like a broken parser. Observed on
# 2026-07-31: "cx came back 9730, expected 7777", where 9730 was simply the ball's true position.
#
# So pause the camera while injecting. Resume is a soft reboot: /sdcard/main.py autostarts the vision
# script, so Ctrl-D brings it back with no further help.
$script:k230Paused = $false
function K230Pause() {
    if (-not $K230) { return $false }
    try {
        $k = New-Object System.IO.Ports.SerialPort $K230, 115200, None, 8, one
        $k.ReadTimeout = 200; $k.DtrEnable = $true; $k.RtsEnable = $true
        $k.Open()
        $k.Write([char]0x03); Start-Sleep -Milliseconds 300
        $k.Write([char]0x03); Start-Sleep -Milliseconds 600
        $k.Close(); $k.Dispose()
        $script:k230Paused = $true
        return $true
    } catch { L ("  (could not pause the camera on {0}: {1})" -f $K230, $_.Exception.Message); return $false }
}
function K230Resume() {
    if (-not $script:k230Paused) { return }
    try {
        $k = New-Object System.IO.Ports.SerialPort $K230, 115200, None, 8, one
        $k.ReadTimeout = 200; $k.DtrEnable = $true; $k.RtsEnable = $true
        $k.Open()
        $k.Write([char]0x04)                     # soft reboot -> main.py autostarts the vision script
        $b = ""
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt 30) {
            try { $b += $k.ReadExisting() } catch {}
            if ($b -match 'BALL_UART_V4|_READY conf=') { break }
            Start-Sleep -Milliseconds 100
        }
        $k.Close(); $k.Dispose()
        $script:k230Paused = $false
        if ($b -match 'BALL_UART_V4|_READY conf=') { L "  camera resumed (main.py autostart)" }
        else { L "  WARNING: camera did not report READY within 30 s after resume" }
    } catch { L ("  (could not resume the camera: {0})" -f $_.Exception.Message) }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
# The firmware's diagnostic hints are UTF-8 Chinese; SerialPort defaults to ASCII and turned every one
# of them into '?'. Commands we send are ASCII-only, so UTF-8 is byte-identical outbound - no risk.
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

$scale = 1.0 / $CxPerMm

L "================ ball_bringup  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ================"
L "port $Port   (nothing here drives the motors)"
L ""

$sink = SetupTelemetry
if (-not $sink) {
    L "No [ctl] telemetry at all. Before anything else:"
    L "  * is another program holding this COM port? (the port is exclusive)"
    L "  * did the flash actually take? judge on 'wrote N' AND 'verified N' both matching size's text+data"
    L "  * if flash.ps1 reported a diff, run verify_addr.ps1 before reflashing - that false failure has"
    L "    reproduced 11 times and a needless reflash costs 90 s plus one more brick risk"
    Finish "INCONCLUSIVE - no telemetry, nothing can be checked." 2
}
L ("telemetry up: f25, sink {0}" -f $sink)
L ""
L "---- checks ----"

# ---- 1 + 2: firmware identity and pitch/roll, both from one `g` -------------------------------
# `pitch0.1deg=` is printed ONLY by builds from 2026-07-31 on, so it doubles as a version fingerprint
# that needs no reboot. That matters: the repo's hard rule is "prove the chip runs the NEW image before
# believing any measurement", and the boot banner is only visible at power-on.
$imuR = ReadPitch 3.0
if (-not $imuR) {
    Verdict "1 firmware identity" "FAIL" "no 'pitch0.1deg=' in the g reply -> chip is running an OLDER build; reflash"
    Verdict "2 pitch/roll print"  "FAIL" "(same cause)"
} else {
    Verdict "1 firmware identity" "PASS" "g prints pitch0.1deg= (only this build does)"
    if ([Math]::Abs($imuR.pitch) -le 45.0 -and [Math]::Abs($imuR.roll) -le 45.0) {
        Verdict "2 pitch/roll print" "PASS" ("pitch {0:N2} deg, roll {1:N2} deg (plausible)" -f $imuR.pitch, $imuR.roll)
    } else {
        Verdict "2 pitch/roll print" "FAIL" ("pitch {0:N2} / roll {1:N2} - beyond +-45 deg; IMU axis or calibration wrong" -f $imuR.pitch, $imuR.roll)
    }
}

# ---- 3: inject a $V frame, expect it back in BALL: -------------------------------------------
# Sentinel value chosen so it cannot be confused with a real reading: 7777 cx = 77.77 mm, and no real
# ball sits there by accident during a bench test.
function InjectAndRead([string]$frame, [int]$tries) {
    for ($k = 0; $k -lt $tries; $k++) {
        # Drop the driver backlog before injecting - it still describes the PREVIOUS frame, because the
        # firmware holds g_uf.last until a new frame parses and re-reports it every 25 ms.
        # FlushRx is the prescribed step (璺ㄩ鍧戝簱 L173); keeping lastStamp on top of that means a repeat
        # of the old frame is also recognised as a duplicate even if one line slips through.
        FlushRx
        Wait 0.3
        FlushRx
        $n0 = $script:samples.Count
        $ns0 = $script:notSeen
        for ($r = 0; $r -lt 4; $r++) { SendFrame $frame; Start-Sleep -Milliseconds 60 }
        Wait 0.5
        if ($script:samples.Count -gt $n0) { return $script:samples[$script:samples.Count-1] }
        if ($script:notSeen -gt $ns0) { return [pscustomobject]@{ cx = $null; us = 0; age = 0 } }  # id=-1 seen
    }
    return $null
}

# Is a real camera already streaming? If so the injection checks cannot be trusted unless it is paused,
# because the next real frame lands 40 ms after the injected one. Probe before injecting: 1.2 s of quiet
# telemetry means nothing is competing.
FlushRx
$probe0 = $script:samples.Count + $script:notSeen
Wait 1.2
$cameraLive = (($script:samples.Count + $script:notSeen) -gt $probe0)
$script:injectIsolated = $true
if ($cameraLive) {
    L "  camera is streaming - pausing it so the injection checks are not racing real frames"
    if (K230Pause) {
        Wait 0.8
        FlushRx
        $p1 = $script:samples.Count + $script:notSeen
        Wait 1.0
        if (($script:samples.Count + $script:notSeen) -gt $p1) {
            L "  frames still arriving after Ctrl-C - the pause did not take"
            $script:injectIsolated = $false
        } else { L "  camera paused" }
    } else {
        $script:injectIsolated = $false
        L "  no -K230 <port> given, so the camera cannot be paused. Checks 3/4/4b will be reported as"
        L "  INCONCLUSIVE rather than FAIL: a mismatch here would be this race, not a parser fault."
    }
}
# When the camera could not be paused, a mismatch is not evidence of a parser fault - so say so instead
# of printing FAIL. A wrong FAIL here is expensive: it points at parse_fixed3, which is correct.
$injState = if ($script:injectIsolated) { "FAIL" } else { "INCONCLUSIVE" }
$raceNote = if ($script:injectIsolated) { "" } else {
    " -- but a live camera was overwriting the injected frame, so this is very likely that race, not the parser. Rerun with -K230 <console port> to isolate." }

$fV = FrameV 1 7777
$gotV = InjectAndRead $fV 3
if ($null -eq $gotV) {
    Verdict "3 `$V injection" "FAIL" ("sent $fV but no BALL: field appeared -> '`$' routing or BALL: telemetry missing")
} elseif ($null -eq $gotV.cx) {
    Verdict "3 `$V injection" "FAIL" "frame parsed as id=-1 - unexpected for a valid `$V frame"
} elseif ($gotV.cx -eq 7777) {
    Verdict "3 `$V injection" "PASS" "cx round-tripped as 7777 (routing + parser + telemetry all good)"
} else {
    Verdict "3 `$V injection" $injState ("cx came back {0}, expected 7777{1}" -f $gotV.cx, $raceNote)
}

# ---- 4: inject a $BP frame, expect the centimetre->0.01mm conversion ---------------------------
# THE check that matters most for the camera, because it is the one thing a unit test cannot fully
# settle: the same source compiled for ARM. +5.23 cm must arrive as cx = 5230. A wrong scale here
# (523 or 52300) compiles, runs, and silently puts every gain out by 10x or 100x.
$fBP = FrameBP 1 5.23
$gotBP = InjectAndRead $fBP 3
if ($null -eq $gotBP) {
    Verdict "4 `$BP injection" "FAIL" ("sent $fBP but nothing came back -> the BP branch is not in this build")
} elseif ($null -eq $gotBP.cx) {
    Verdict "4 `$BP injection" "FAIL" "parsed as id=-1 - valid=1 was sent, so the valid field is being misread"
} elseif ($gotBP.cx -eq 5230) {
    Verdict "4 `$BP injection" "PASS" "+5.23 cm -> cx 5230 (x1000 conversion correct on target)"
} else {
    $ratio = if ($gotBP.cx -ne 0) { 5230.0 / $gotBP.cx } else { 0 }
    Verdict "4 `$BP injection" $injState ("cx {0}, expected 5230 (off by {1:N0}x) - check parse_fixed3 scaling{2}" -f $gotBP.cx, $ratio, $raceNote)
}
# and the "nothing in view" frame must NOT look like a ball at the centre
$fBP0 = FrameBP 0 0.0
$got0 = InjectAndRead $fBP0 2
if ($null -eq $got0) {
    Verdict "4b `$BP valid=0" "FAIL" "no BALL: field at all for the valid=0 frame"
} elseif ($null -eq $got0.cx) {
    Verdict "4b `$BP valid=0" "PASS" "valid=0 -> id=-1, correctly excluded (not read as 'ball at 0 mm')"
} else {
    Verdict "4b `$BP valid=0" $injState ("valid=0 produced a usable sample cx={0} - the id mapping is wrong, and every{1}" -f $got0.cx, $raceNote)
    L "         fit would silently ingest fake zeros whenever the camera loses the ball"
}

# Injection group is over - put the camera back before anything tries to measure it (check 7).
K230Resume

# ---- 5: l0 must silence this port, l3 must bring it back --------------------------------------
# 鈿?The backlog must be drained BEFORE the measuring window opens, or this check lies.
# Telemetry runs at 40 Hz, so any pause after sending l0 leaves ~16 already-emitted lines sitting in the
# Windows serial driver buffer. Those were sent before the mute took effect, but a naive read pulls them
# in and the check concludes "still talking". Wait (which drains) first, THEN zero the counter, THEN
# measure a clean window. Found on the real board: this reported a false FAIL on a working l0.
Send "l0"
Wait 0.9                       # drains and discards the pre-mute backlog
$script:lines = 0
[void](Collect 1200 0 0)
$silenced = ($script:lines -eq 0)
Send "l3"
Wait 0.6
$script:lines = 0
[void](Collect 1200 0 0)
$restored = ($script:lines -gt 0)
if ($silenced -and $restored) {
    Verdict "5 l0 / l3 sink switch" "PASS" "l0 silenced the port, l3 restored it (Q62 compliance available)"
} elseif (-not $silenced) {
    Verdict "5 l0 / l3 sink switch" "FAIL" "l0 did not silence the port -> the v>0 guard is still in this build"
} else {
    Verdict "5 l0 / l3 sink switch" "FAIL" "l0 worked but l3 did not restore - the board is now MUTE; power-cycle it"
}
Send "f25"; Start-Sleep -Milliseconds 200   # SetupTelemetry's single-sink lock was undone by l3
Send "l1"; Start-Sleep -Milliseconds 200
$script:lines = 0
[void](Collect 700 0 0)
if ($script:lines -eq 0) { Send "l2"; Start-Sleep -Milliseconds 200 }

# ---- 6: RUN page ball line - needs eyes -------------------------------------------------------
if ($SkipEyes) {
    Verdict "6 RUN page ball line" "SKIP" "-SkipEyes given"
} else {
    Send "u2"; Start-Sleep -Milliseconds 500
    $fEye = FrameBP 1 -8.88
    for ($r = 0; $r -lt 25; $r++) { SendFrame $fEye; Start-Sleep -Milliseconds 80 }
    Write-Host ""
    Write-Host "  LOOK AT THE LCD (RUN page). The ball line should read about -88.8 mm." -ForegroundColor Cyan
    Write-Host "  This is the ONLY channel you get during a scored run (Q62 allows video only), so if it" -ForegroundColor Cyan
    Write-Host "  says 'ball --' the run is blind. Type Y if you see the number, anything else if not:" -ForegroundColor Cyan
    $ans = Read-Host
    if ($ans -match '^[Yy]') {
        Verdict "6 RUN page ball line" "PASS" "human confirmed ~-88.8 mm on the LCD"
    } else {
        Verdict "6 RUN page ball line" "FAIL" "LCD did not show the injected ball position (g_ball_mm not reaching the page)"
    }
}

# ---- 7: the real camera - the only thing injection cannot prove -------------------------------
if ($SkipCamera) {
    Verdict "7 real camera on PB16" "SKIP" "-SkipCamera given (checks 3/4 already proved everything above the UART)"
} else {
    # Stop injecting and see whether NEW frames keep arriving. A genuinely new frame stamp with nothing
    # being injected can only come from the camera - that is what separates "wiring works" from
    # "protocol works". Counting must be done on DELTAS with lastStamp preserved: the firmware keeps
    # re-reporting the last frame it parsed every 25 ms, so an absolute count would tally the leftover
    # injected frame from check 4b and call it camera traffic. That exact false PASS happened once.
    Wait 1.5                       # let the last injected frame age out of the pipeline
    FlushRx
    $n0 = $script:samples.Count
    $ns0 = $script:notSeen
    Wait 2.5                       # Wait (not Collect) so lastStamp survives and repeats stay filtered
    $s = @($script:samples.ToArray())
    if ($s.Count -gt $n0) { $s = $s[$n0..($s.Count-1)] } else { $s = @() }
    $fresh = $s.Count + ($script:notSeen - $ns0)
    if ($fresh -eq 0) {
        # "No frames" is three different faults with three unrelated fixes, so do NOT stop at that.
        # The firmware counts raw bytes and UART error flags on PB16 itself, which separates them:
        # no bytes and no errors = no edges at all on the pin; errors but no bytes = a signal whose
        # framing does not decode; bytes but no frames = physical layer fine, protocol layer wrong.
        $vs = AskLines "V" '^\[vs\]' 2.0
        # The firmware watches two candidate pins (PB16 per syscfg, PB5 per the carrier board doc). Take
        # the busiest one: whichever the wire is actually on is the one that matters here.
        $vb = -1; $ve = -1
        foreach ($ln in $vs) {
            if ($ln -match 'pb(16|5)\s*\([^)]*\):\s*bytes=(\d+)\s+err=(\d+)') {
                $b = [int]$Matches[2]; $e = [int]$Matches[3]
                if ($b -gt $vb) { $vb = $b }
                if ($e -gt $ve) { $ve = $e }
            }
        }
        if ($vb -ge 0) {
            if ($vb -eq 0 -and $ve -eq 0) {
                Verdict "7 real camera on PB16" "FAIL" "zero bytes AND zero UART errors on both watched pins"
                L "         This is a WIRING/POWER fault, not firmware - checks 3/4 already proved the parser."
                L "         Most common cause by far: the K230 script is not running. It draws nothing on its"
                L "         own screen (PREVIEW_ENABLE=False), so idle and running look identical. Settle it"
                L "         with:  k230_link_test.ps1 -K230 <k230 port> -Mcu $Port"
                L "         That holds both ends open at once and says which side is at fault."
            } elseif ($vb -eq 0) {
                Verdict "7 real camera on PB16" "FAIL" ("{0} UART errors but 0 bytes -> a signal is present that does not decode" -f $ve)
                L "         The wire IS connected (edges are arriving) but framing fails: baud mismatch,"
                L "         wrong signal tapped, or bad level. K230 must be 115200 8N1 on IO11."
            } else {
                Verdict "7 real camera on PB16" "FAIL" ("{0} bytes arrived (err={1}) but 0 frames parsed -> PROTOCOL layer" -f $vb, $ve)
                L "         Physical layer is fine. Read the tail= / ascii= dump below: readable '`$BP,...' means"
                L "         look at bad_csum/bad_form/overflow; garbage means the baud rate is wrong."
            }
            foreach ($l in $vs) { L ("         | " + $l) }
        } else {
            Verdict "7 real camera on PB16" "INCONCLUSIVE" "no frames, and the V command did not report a pb16: line"
            L "         That line only exists in builds from 2026-07-31 on - the chip may be running an older"
            L "         image, in which case this check cannot tell wiring from protocol. Reflash and rerun."
            foreach ($l in $vs) { L ("         | " + $l) }
        }
    } else {
        $ages = @($s | ForEach-Object { $_.age })
        $fps  = $fresh / 2.5
        $det  = if ($fresh -gt 0) { 100.0 * $s.Count / $fresh } else { 0 }
        if ($s.Count -eq 0) {
            Verdict "7 real camera on PB16" "PASS" ("link alive at {0:N1} fps but id=-1 always - wiring good, DETECTION not finding the ball" -f $fps)
            L "         Fix on the camera side (ROI / fixed exposure / background frame), not on the MCU."
        } else {
            Verdict "7 real camera on PB16" "PASS" ("{0:N1} fps, detection {1:N0}%, age median {2} ms" -f $fps, $det, (Median $ages))
            if ($fps -lt 15) { L ("         NOTE {0:N1} fps is below the ~24 fps the delivery measured - check the K230 is not throttling." -f $fps) }
        }
    }
}

L ""
L ("---- {0} pass, {1} fail, {2} inconclusive, {3} skipped ----" -f $script:nPass, $script:nFail, $script:nIncon, $script:nSkip)
L ""
L "What this script does NOT prove:"
L "  * the servo moves the beam, or the 5 V rail holds up  -> ball_signs / a multimeter"
L "  * any calibrated value                                -> ball_ident.ps1 -Step Sweep"
L "  * that the camera's px->mm calibration is right        -> ball_ident.ps1 -Step Scale"
L ""
L "Next, in this order: ball_ident -Step Scale  ->  ball_ident -Step Sweep  ->  ball_signs"
if ($script:nFail -gt 0) { Finish ("FAIL - {0} check(s) failed; fix those before calibrating (each names its own cause above)." -f $script:nFail) 1 }
if ($script:nIncon -gt 0) { Finish ("INCONCLUSIVE - {0} check(s) could not decide; treat them as unproven, not as passing." -f $script:nIncon) 2 }
Finish ("PASS - {0} check(s) good, {1} skipped." -f $script:nPass, $script:nSkip) 0
