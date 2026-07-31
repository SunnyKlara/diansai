# k230_link_test.ps1 - hold BOTH ends open at once and answer "does the camera reach the MCU".
#
# WHY THIS EXISTS
#   Testing the two sides in separate runs gave a wrong answer on 2026-07-31. k230_repl -Start launched
#   the vision script and exited; closing the host port took the script down with it (DTR drops on
#   close), so the MCU-side watch that followed measured a link whose transmitter was already dead and
#   reported bytes=0. Two sequential measurements cannot see that, because neither one is wrong on its
#   own - only their overlap in time was missing.
#
#   So this process keeps COM33 (K230 console) open for the whole test, which keeps the script alive,
#   and polls the MCU's PB16 byte counter on COM30 in the same loop. Both numbers come from the same
#   seconds, which is the only way the comparison means anything.
#
# WHAT IT REPORTS
#   K230 side : tx / uart_hz / fps out of the script's own BALL_UART_V4 status lines
#   MCU side  : raw bytes and UART error counts on PB16, from the V command
#   The pair localises the fault with no guessing:
#     K230 tx rising + MCU bytes rising  -> link is up
#     K230 tx rising + MCU bytes 0       -> the WIRE (IO11 -> PB16) or the ground. Nothing else is left.
#     K230 tx rising + MCU err rising    -> wire is fine, framing is not (baud / level)
#     K230 tx stuck at 0                 -> the camera side never writes; do not touch the wiring
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File k230_link_test.ps1 -K230 COM33 -Mcu COM30 -Sec 20
#
# EXIT CODES: 0 = link up, 1 = link down (message names which side), 2 = could not set up the test
# ASCII only in the code.
param(
    [string]$K230   = "COM33",
    [string]$Mcu    = "COM30",
    [int]$Sec       = 20,
    [switch]$NoStart,          # the script is already running - just observe, do not Ctrl-C/restart it
    [switch]$NoSoftReset,      # skip the Ctrl-D before launching (only if you know nothing stale is held)
    [string]$Script = "/sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_position_uart_stable_v4.py",
    [string]$Out    = "_logs\k230_link_test_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$k = $null; $m = $null
function Finish([string]$verdict, [int]$code) {
    L ""; L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        Write-Host "(log -> $Out)"
    } catch {}
    foreach ($p in @($k, $m)) { if ($p -and $p.IsOpen) { try { $p.Close(); $p.Dispose() } catch {} } }
    exit $code
}

L ("================ k230_link_test  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
L ("K230 console={0}   MCU debug={1}   window={2}s" -f $K230, $Mcu, $Sec)

$k = New-Object System.IO.Ports.SerialPort $K230, 115200, None, 8, one
$k.ReadTimeout = 200; $k.DtrEnable = $true; $k.RtsEnable = $true
$k.Encoding = [System.Text.Encoding]::UTF8
try { $k.Open() } catch { Write-Host "OPEN_FAIL ($K230): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

$m = New-Object System.IO.Ports.SerialPort $Mcu, 115200, None, 8, one
$m.ReadTimeout = 200; $m.DtrEnable = $false; $m.RtsEnable = $false
$m.Encoding = [System.Text.Encoding]::UTF8
try { $m.Open() } catch { Write-Host "OPEN_FAIL ($Mcu): $($_.Exception.Message)" -ForegroundColor Red; Finish "INCONCLUSIVE - MCU port busy" 2 }

# ---- MCU side: ask V, parse the pb16 counters --------------------------------------------------
function McuPb16() {
    try { $m.DiscardInBuffer() } catch {}          # read the reply, never the backlog (跨题坑库 L173)
    foreach ($ch in "V`n".ToCharArray()) { $m.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    $buf = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalMilliseconds -lt 600) {
        try { $buf += $m.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 20
    }
    # The firmware watches BOTH candidate pins (PB16 = the firmware's design pin, PB5 = the pin the
    # carrier board doc assigns to the camera) and prints one line each. Parse both and report which one
    # is actually carrying traffic - that is the whole point of listening on two pins.
    $r = [pscustomobject]@{ b16 = -1; e16 = -1; b5 = -1; e5 = -1; bytes = 0; err = 0; tail = ""; pin = "" }
    $got = $false
    foreach ($ln in ($buf -split "`n")) {
        if ($ln -match 'pb(?<p>16|5)\s*\([^)]*\):\s*bytes=(?<b>\d+)\s+err=(?<e>\d+)') {
            $got = $true
            $b = [int]$Matches['b']; $e = [int]$Matches['e']; $p = $Matches['p']
            $tail = ""
            if ($ln -match "ascii='(?<a>[^']*)'") { $tail = $Matches['a'] }
            if ($p -eq '16') { $r.b16 = $b; $r.e16 = $e } else { $r.b5 = $b; $r.e5 = $e }
            if ($b -gt $r.bytes) { $r.bytes = $b; $r.tail = $tail; $r.pin = "PB$p" }
            if ($e -gt $r.err) { $r.err = $e }
        }
    }
    if (-not $got) { return $null }
    return $r
}

$base = McuPb16
if (-not $base) {
    L "the MCU did not answer V with a 'pb16:' line - that counter only exists in builds from 2026-07-31."
    L "Reflash (check 'wrote N bytes' == build.ps1 text+data) or send l3 to unmute the wired sink."
    Finish "INCONCLUSIVE - no pb16 counter on the MCU" 2
}
L ("MCU baseline: PB16 bytes={0} err={1} | PB5 bytes={2} err={3}" -f $base.b16, $base.e16, $base.b5, $base.e5)
if ($base.b5 -lt 0) {
    L "  NOTE: the firmware did not report a pb5 line - this build only watches PB16. If the camera wire"
    L "  is on PB5 (which is what the carrier board doc assigns to it) this test cannot see it."
}

# ---- K230 side: start the script unless told not to -------------------------------------------
if (-not $NoStart) {
    L ""
    L "starting the vision script on the K230 (this port stays open for the whole test, which is what"
    L "keeps it alive - that is the bug this tool exists to avoid)"
    try { $k.DiscardInBuffer() } catch {}
    $k.Write([char]0x03); Start-Sleep -Milliseconds 250
    $k.Write([char]0x03); Start-Sleep -Milliseconds 400
    # Soft reboot (Ctrl-D) before every launch, not just after a failure.
    #
    # A killed run does NOT release the camera sensor and the NPU: the script's cleanup lives in a
    # finally: pipeline.destroy(), and when the interpreter is torn down mid-run that never executes.
    # The next launch then dies inside camera init, and its own destroy() blows up first with
    # "'NoneType' object has no attribute 'stop'" - which HIDES the real error behind a misleading one.
    # Observed exactly like that on 2026-07-31. Ctrl-D drops the whole interpreter state, so the media
    # subsystem is released and the next start is clean. Two seconds, every time, no judgement needed.
    if (-not $NoSoftReset) {
        L "  soft reboot (Ctrl-D) first: a previously killed run leaves the camera claimed"
        # Wait for the post-reboot BANNER, not for a '>>>'.
        # Waiting on '>>>' is wrong and cost one full run: the pre-reboot prompt is still sitting in the
        # buffer, so the match fires instantly, the exec line goes out while the board is still coming
        # up, and it is simply swallowed. The board then idles at a fresh prompt looking exactly like
        # "the script refuses to start". 'Type "help()"' is only ever printed by a completed boot.
        try { $k.DiscardInBuffer() } catch {}
        $k.Write([char]0x04)
        $sb = ""
        $booted = $false
        $swr = [System.Diagnostics.Stopwatch]::StartNew()
        while ($swr.Elapsed.TotalSeconds -lt 20) {
            try { $sb += $k.ReadExisting() } catch {}
            if ($sb -match 'Type "help\(\)"' -or $sb -match 'CanMV v') { $booted = $true; break }
            Start-Sleep -Milliseconds 50
        }
        if ($booted) {
            L ("  soft reboot done ({0:N1}s)" -f $swr.Elapsed.TotalSeconds)
            # The banner precedes the prompt; give the prompt time to land before typing at it.
            $swp = [System.Diagnostics.Stopwatch]::StartNew()
            while ($swp.Elapsed.TotalSeconds -lt 5) {
                try { $sb += $k.ReadExisting() } catch {}
                if ($sb -match '>>>\s*$') { break }
                Start-Sleep -Milliseconds 50
            }
        } else {
            L "  WARNING: no boot banner within 20 s after Ctrl-D. Power-cycle the K230 and retry."
        }
        Start-Sleep -Milliseconds 500
    }
    try { $k.DiscardInBuffer() } catch {}
    $k.Write("exec(open('" + $Script + "').read())`r`n")
    $boot = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 30) {
        try { $boot += $k.ReadExisting() } catch {}
        if ($boot -match '_READY conf=') { break }
        if ($boot -match 'Traceback') { break }
        Start-Sleep -Milliseconds 100
    }
    foreach ($l in ($boot -split "`n")) { if ($l.Trim()) { L ("  K230| " + $l.TrimEnd("`r")) } }
    if ($boot -match 'Traceback') { Finish "FAIL - the vision script raised an exception (see traceback above)" 1 }
    if ($boot -notmatch '_READY conf=') {
        L "  (no READY banner within 30 s - continuing anyway, the loop below will show whether it runs)"
    }
} else {
    L ""
    L "-NoStart: observing only, the script is assumed to be running already."
}

# ---- the overlapped loop -----------------------------------------------------------------------
L ""
L "---- both ends, same seconds ----"
$kbuf = ""
$prev = $base
$firstTx = -1; $lastTx = -1; $lastHz = -1.0; $lastFps = -1.0
$sawBytes = $false; $sawErr = $false
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalSeconds -lt $Sec) {
    # Drain the K230 console continuously. Leaving it unread risks the board blocking on print() once
    # the CDC buffer fills, which would look exactly like "inference stopped".
    $t0 = [System.Diagnostics.Stopwatch]::StartNew()
    while ($t0.Elapsed.TotalMilliseconds -lt 1000) {
        try { $kbuf += $k.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 25
    }
    foreach ($ln in ($kbuf -split "`n")) {
        if ($ln -match 'tx=(?<t>\d+)') {
            $lastTx = [int]$Matches['t']
            if ($firstTx -lt 0) { $firstTx = $lastTx }
        }
        if ($ln -match 'uart_hz=(?<h>[\d.]+)')  { $lastHz  = [double]$Matches['h'] }
        if ($ln -match 'fps=(?<f>[\d.]+)')      { $lastFps = [double]$Matches['f'] }
    }
    $kbuf = ""

    $cur = McuPb16
    if (-not $cur) { L ("{0,5:N1}s  MCU no reply" -f $sw.Elapsed.TotalSeconds); continue }
    $db = $cur.bytes - $prev.bytes
    $de = $cur.err - $prev.err
    if ($db -gt 0) { $sawBytes = $true }
    if ($de -gt 0) { $sawErr = $true }
    $tl = if ($cur.pin) { ("  on " + $cur.pin + " ascii='" + $cur.tail + "'") } else { "" }
    L ("{0,5:N1}s  K230 tx={1,-7} hz={2,-6} fps={3,-6} | MCU PB16={4,-7} PB5={5,-7} (+{6}) err16={7} err5={8}{9}" -f `
        $sw.Elapsed.TotalSeconds, $lastTx, $lastHz, $lastFps, $cur.b16, $cur.b5, $db, $cur.e16, $cur.e5, $tl)
    $prev = $cur
}

L ""
$txGrew = ($firstTx -ge 0 -and $lastTx -gt $firstTx)
L ("K230: tx {0} -> {1}   uart_hz={2}   fps={3}" -f $firstTx, $lastTx, $lastHz, $lastFps)
L ("MCU : PB16 {0} -> {1} (err {2})   PB5 {3} -> {4} (err {5})" -f `
    $base.b16, $prev.b16, $prev.e16, $base.b5, $prev.b5, $prev.e5)
L ""

if ($sawBytes) {
    L ("bytes are arriving on {0} while the camera transmits => THE LINK IS UP." -f $prev.pin)
    if ($prev.tail) { L ("  last 8 raw bytes: '" + $prev.tail + "'") }
    if ($prev.pin -eq 'PB5') {
        L "  The wire is on PB5, the pin the carrier board doc assigns to the camera - not PB16, which is"
        L "  what config/syscfg was written around. It works because the firmware now listens on both."
        L "  Record PB5 as the real pin in the SSOT so the next session does not 'fix' it back."
    }
    L "  Next: ball_bringup.ps1 -Port COM30 -SkipEyes  (check 7 should now pass)"
    Finish ("PASS - end-to-end camera link up on {0}" -f $prev.pin) 0
}
if ($txGrew -or ($lastHz -gt 0)) {
    if ($sawErr) {
        L "the K230 is transmitting and PB16 sees edges, but they do not decode into bytes."
        L "  => framing, not connectivity: baud mismatch or a bad level. K230 is 115200 8N1 on IO11."
        Finish "FAIL - edges arrive but do not decode" 1
    }
    L "the K230 is definitely transmitting (its own tx counter is rising) and NEITHER candidate pin saw"
    L "anything - not one byte, not one framing error, on PB16 or PB5."
    L "  => By elimination the fault is the connection itself. Both plausible destination pins are ruled"
    L "     out, so what is left is:"
    L "     (a) the wire is on some third pin (PB15 is PB16's neighbour and the classic miss),"
    L "     (b) no shared ground between the K230 and the control board,"
    L "     (c) a broken/unseated jumper, or the K230 end is not really IO11."
    L "  The MCU end is already proven: injection tests pass, and the counters react to edges."
    L "  Header positions: PB16 = J2-7R, PB5 = J2-5L, PB15 = J2-6R (see 载板接线设计 10.1)."
    Finish "FAIL - camera transmits, nothing reaches PB16 or PB5: wiring" 1
}
L "the K230's own tx counter never moved - the camera side is not writing to the UART at all."
L "  Do NOT touch the wiring yet. Check the console lines above: if there are no BALL_UART_V4 lines the"
L "  script is not looping; if uart_hz=0 it loops but never writes."
Finish "FAIL - camera side is not transmitting" 1
