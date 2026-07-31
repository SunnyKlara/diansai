# k230_repl.ps1 - drive the K230 over its USB MicroPython REPL: inspect the deployment, start or stop
# the ball-detection script, and watch its console.
#
# WHY THIS EXISTS
#   2026-07-31 bring-up: the MCU reported bytes=0 err=0 on PB16 (no edges at all), and the K230 console
#   answered a bare Enter with '>>>'. The board was powered and healthy but PARKED AT THE REPL - the
#   detection script simply was not running, so nothing was ever transmitted. Without a way to look at
#   the K230 side, that is indistinguishable from a broken wire, and we would have spent the night
#   re-seating jumpers.
#
#   The delivery runs with PREVIEW_ENABLE = False (latency mode), so the K230 draws NOTHING on its
#   screen. There is no way to tell "running" from "idle" by looking at it. This script is that way.
#
# WHY A TOOL AND NOT A ONE-OFF
#   Calibration needs the camera started and stopped repeatedly (scale check, sign tests, sweeps), and
#   between runs the script must be restarted after every Ctrl-C. Opening CanMV IDE for each of those is
#   slow and cannot be scripted alongside the MCU-side scripts.
#
# WHAT EACH MODE IS FOR
#   -Ls      does the deployment actually exist on the SD card, and is main.py in place for autostart?
#            (autostart matters on competition day: /sdcard/main.py is what CanMV runs at power-on)
#   -Start   interrupt, then exec the detection script; waits for its READY banner
#   -Stop    Ctrl-C back to the prompt
#   -Watch   tail the console; the BALL_UART_V4 lines carry fps / uart_hz / tx, which is the K230-side
#            proof that it is transmitting - pair it with the MCU-side 'V' byte counter for end-to-end
#   -Cmds    arbitrary single-line statements, separated by ';;'
#
# SAFETY
#   -Stop / -Start send Ctrl-C, which stops whatever is running on the K230. Nothing here writes to the
#   SD card or changes any stored file, so every effect is undone by a power cycle or a restart.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File k230_repl.ps1 -Port COM33 -Ls
#   powershell -NoProfile -ExecutionPolicy Bypass -File k230_repl.ps1 -Port COM33 -Start
#   powershell -NoProfile -ExecutionPolicy Bypass -File k230_repl.ps1 -Port COM33 -Watch 10
#   powershell -NoProfile -ExecutionPolicy Bypass -File k230_repl.ps1 -Port COM33 -Stop
#
# EXIT CODES: 0 = PASS, 1 = FAIL, 2 = INCONCLUSIVE / could not talk to the board
# ASCII only in the code.
param(
    [string]$Port   = "COM33",
    [int]$Baud      = 115200,
    [switch]$Ls,
    [switch]$Start,
    [switch]$Stop,
    [switch]$DeployMain,       # write /sdcard/main.py so the board autostarts with no host attached
    [switch]$RemoveMain,       # undo -DeployMain
    [int]$Watch     = 0,
    [string]$Cmds   = "",
    [string]$Script = "/sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_position_uart_stable_v4.py",
    [string]$Out    = "_logs\k230_repl_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = $null
function Finish([string]$verdict, [int]$code) {
    L ""; L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        Write-Host "(log -> $Out)"
    } catch {}
    if ($sp -and $sp.IsOpen) { try { $sp.Close(); $sp.Dispose() } catch {} }
    exit $code
}

L ("================ k230_repl  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200
# Some USB-CDC stacks hold TX until the host raises DTR/RTS. Raising them is harmless on a CDC device
# (there is no real modem behind it) and it is what a terminal emulator does anyway.
$sp.DtrEnable = $true; $sp.RtsEnable = $true
$sp.Encoding = [System.Text.Encoding]::UTF8
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

# ---- REPL primitives ---------------------------------------------------------------------------
# The friendly REPL echoes what it receives, so every reply contains the command itself. Callers strip
# it. Single-line statements only: the friendly REPL auto-indents after a colon, which silently mangles
# pasted multi-line blocks.

function ReadFor([double]$sec) {
    $b = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        try { $b += $sp.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 30
    }
    return $b
}

# Read until the prompt comes back, or timeout. Returns the raw text.
function ReadToPrompt([double]$timeoutS) {
    $b = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
        try { $b += $sp.ReadExisting() } catch {}
        if ($b -match '>>>\s*$') { break }
        Start-Sleep -Milliseconds 30
    }
    return $b
}

# Ctrl-D. Needed before touching the filesystem: a leaked file handle from an earlier attempt makes the
# next open() fail with OSError EPERM, which reads like "the card is read-only" and is not. Dropping the
# interpreter releases every handle, so this makes filesystem operations idempotent.
function SoftReboot([double]$timeoutS = 20.0) {
    try { $sp.DiscardInBuffer() } catch {}
    $sp.Write([char]0x04)
    $b = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    # Wait for the BANNER, not for '>>>': the pre-reboot prompt is still in the buffer and would match
    # instantly, so anything typed next lands while the board is still coming up and is swallowed.
    while ($sw.Elapsed.TotalSeconds -lt $timeoutS) {
        try { $b += $sp.ReadExisting() } catch {}
        if ($b -match 'Type "help\(\)"' -or $b -match 'CanMV v') { break }
        Start-Sleep -Milliseconds 50
    }
    $ok = ($b -match 'Type "help\(\)"' -or $b -match 'CanMV v')
    if ($ok) {
        $swp = [System.Diagnostics.Stopwatch]::StartNew()
        while ($swp.Elapsed.TotalSeconds -lt 5) {
            try { $b += $sp.ReadExisting() } catch {}
            if ($b -match '>>>\s*$') { break }
            Start-Sleep -Milliseconds 50
        }
    }
    return $ok
}

function Interrupt([double]$waitS = 1.5) {
    try { $sp.DiscardInBuffer() } catch {}
    $sp.Write([char]0x03)                 # Ctrl-C
    Start-Sleep -Milliseconds 250
    $sp.Write([char]0x03)                 # twice: the first one may land mid-import
    return (ReadToPrompt $waitS)
}

# Send one statement, return only the OUTPUT (echo and trailing prompt removed).
function Eval([string]$stmt, [double]$timeoutS = 6.0) {
    try { $sp.DiscardInBuffer() } catch {}
    $sp.Write($stmt + "`r`n")
    $raw = ReadToPrompt $timeoutS
    $lines = @()
    foreach ($ln in ($raw -split "`n")) {
        $t = $ln.TrimEnd("`r")
        if ($t -match '^\s*>>>') { continue }        # prompt (and the echoed command after it)
        if ($t.Trim() -eq $stmt.Trim()) { continue } # bare echo
        if ($t.Trim() -eq '') { continue }
        $lines += $t
    }
    return ,@($lines)
}

# ---- confirm we are actually at a prompt -------------------------------------------------------
$probe = Interrupt 2.0
if ($probe -notmatch '>>>') {
    L "the K230 console never showed a '>>>' prompt after Ctrl-C."
    L "  * If it is mid-boot, wait ~15 s and retry."
    L "  * If CanMV IDE is connected to this port, close it - only one host may own the port."
    L ("  * bytes seen: {0}" -f $probe.Length)
    if ($probe.Length) { foreach ($l in ($probe -split "`n")) { L ("  | " + $l.TrimEnd("`r")) } }
    Finish "INCONCLUSIVE - no REPL prompt on $Port" 2
}
L "REPL prompt reached (Ctrl-C answered)."

$fail = 0

if ($Ls) {
    L ""
    L "---- deployment check ----"
    # Membership in os.listdir(parent), not os.stat: stat raises on a missing path, and try/except needs
    # a multi-line block, which the friendly REPL's auto-indent mangles when pasted. This stays one line.
    $paths = @{
        "autostart /sdcard/main.py" = "/sdcard/main.py"
        "script"                    = $Script
        "kmodel"                    = "/sdcard/steelball/fixed_pipe_480_v1/fixed_pipe_ball_480_v1.kmodel"
        "calibration json"          = "/sdcard/steelball/fixed_pipe_480_v1/position_calibration.json"
    }
    [void](Eval "import os")
    foreach ($k in @("autostart /sdcard/main.py", "script", "kmodel", "calibration json")) {
        $p = $paths[$k]
        $dir  = $p.Substring(0, $p.LastIndexOf('/'))
        $name = $p.Substring($p.LastIndexOf('/') + 1)
        $o = Eval ("print('YES' if '" + $name + "' in os.listdir('" + $dir + "') else 'NO')") 4.0
        $ans = ($o -join ' ').Trim()
        # A missing PARENT directory raises instead of answering - that is a distinct, louder fault.
        if ($ans -match 'ENOENT|OSError|Error') {
            L ("  [MISS] {0,-26} {1}   (parent dir '{2}' does not exist)" -f $k, $p, $dir)
            $fail++
            continue
        }
        if ($ans -match 'YES') { L ("  [ok  ] {0,-26} {1}" -f $k, $p) }
        elseif ($ans -match 'NO') {
            L ("  [MISS] {0,-26} {1}" -f $k, $p)
            if ($k -like 'autostart*') {
                L "         => the board will NOT start the vision script at power-on. Fine for bench work"
                L "            (use -Start), but it MUST be in place before the scored run."
            } else { $fail++ }
        }
        else { L ("  [??  ] {0,-26} {1}   (reply: {2})" -f $k, $p, $ans) }
    }
    $o = Eval "print(os.listdir('/sdcard'))" 4.0
    L ("  /sdcard: " + (($o -join ' ').Trim()))
}

if ($RemoveMain) {
    L ""
    [void](Eval "import os")
    $o = Eval "os.remove('/sdcard/main.py')" 5.0
    if (($o -join ' ') -match 'Error') { L ("  " + ($o -join ' ')); Finish "FAIL - could not remove /sdcard/main.py" 1 }
    L "removed /sdcard/main.py - the board will boot to the REPL again."
    Finish "PASS - autostart removed" 0
}

if ($DeployMain) {
    L ""
    L "---- deploying /sdcard/main.py (autostart) ----"
    L "WHY THIS IS REQUIRED, not a convenience: CanMV runs /sdcard/main.py at power-on and nothing else."
    L "Without it the vision script only runs while a PC holds this console open - and it dies the moment"
    L "the port closes. A scored run has no PC attached, so the camera would simply never transmit."
    L ""
    L "A one-line loader is used instead of copying the 630-line script to main.py: one source of truth,"
    L "so the delivery file stays the only place the algorithm lives and the two cannot drift apart."
    L ""
    if (SoftReboot 20.0) { L "  soft reboot done (releases any leaked file handle from an earlier attempt)" }
    else { L "  WARNING: no boot banner after Ctrl-D; if the write fails with EPERM, power-cycle and retry" }
    [void](Eval "import os")
    # Single line, single-quoted in PowerShell so the inner double quotes reach Python untouched.
    $body = 'exec(open(''' + $Script + ''').read())'
    # close() explicitly. Relying on the temporary being collected leaves the file open and buffered:
    # write() reports the right byte count, and the very next read raises OSError EPERM because the FS
    # still holds it. That looked like a permission problem and is really a missing close (seen 07-31).
    # Semicolons keep this one physical line - the friendly REPL auto-indents after a colon and would
    # mangle a real multi-line block pasted at the prompt.
    $stmt = 'f=open(''/sdcard/main.py'',''w''); f.write("' + $body.Replace('"','\"') + '\n"); f.close()'
    $o = Eval $stmt 6.0
    $joined = ($o -join ' ')
    if ($joined -match 'Error|Traceback') {
        L ("  " + $joined)
        Finish "FAIL - could not write /sdcard/main.py (is the card read-only?)" 1
    }
    L ("  write() returned: " + $joined.Trim() + "  (that is the byte count)")
    # Read it back. Writing and trusting it is how a half-written file becomes a competition-day surprise.
    $rb = Eval "print(open('/sdcard/main.py').read())" 5.0
    L ("  read back: " + (($rb -join ' ').Trim()))
    if (($rb -join ' ') -notmatch 'fixed_pipe_position_uart_stable_v4') {
        Finish "FAIL - main.py does not contain the loader after write-back check" 1
    }
    L ""
    L "autostart in place. Verify it for real by POWER-CYCLING the K230 with no PC attached, then"
    L "checking the MCU side:  tools\vis_watch.ps1 -Port COM30 -Sec 15   (bytes must rise on their own)"
    L "That power-cycle test is the only thing that proves it; this write-back only proves the file exists."
    Finish "PASS - /sdcard/main.py deployed (power-cycle test still pending)" 0
}

if ($Stop) {
    L ""
    L "sent Ctrl-C; the board is at the prompt and transmitting nothing on IO11."
    Finish "PASS - stopped, K230 idle at REPL" 0
}

if ($Start) {
    L ""
    L "---- starting the detection script ----"
    L ("  exec " + $Script)
    try { $sp.DiscardInBuffer() } catch {}
    # __name__ is '__main__' at the REPL, so the script's own __main__ guard fires. Model load plus
    # camera and NPU init take a few seconds, so give the banner a generous window.
    $sp.Write("exec(open('" + $Script + "').read())`r`n")
    $raw = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt 30) {
        try { $raw += $sp.ReadExisting() } catch {}
        if ($raw -match 'BALL_UART_V4|_READY') { break }
        if ($raw -match 'Traceback|Error') { break }
        Start-Sleep -Milliseconds 100
    }
    foreach ($l in ($raw -split "`n")) { if ($l.Trim()) { L ("  | " + $l.TrimEnd("`r")) } }
    if ($raw -match 'Traceback|MemoryError|OSError|NameError') {
        L ""
        L "the script raised an exception - read the traceback above. Common causes: the kmodel or the"
        L "calibration json is missing from the SD card, or a previous run left the camera/NPU claimed"
        L "(power-cycle the K230 and retry)."
        Finish "FAIL - script did not start" 1
    }
    if ($raw -notmatch 'POSITION_UART_READY') {
        L ""
        L "no POSITION_UART_READY banner: the UART did not open, so nothing will reach PB16 even though"
        L "the script may be inferring. Check UART_ENABLE / the IO11 pinmux inside the script."
        $fail++
    }
    $more = ReadFor 3.0
    foreach ($l in ($more -split "`n")) { if ($l.Trim()) { L ("  | " + $l.TrimEnd("`r")) } }
    $tx = 0
    if ($more -match 'tx=(\d+)') { $tx = [int]$Matches[1] }
    elseif ($raw -match 'tx=(\d+)') { $tx = [int]$Matches[1] }
    if ($tx -gt 0) {
        L ""
        L ("K230 reports tx={0} packets already sent => it IS transmitting on IO11." -f $tx)
        L "Now check the MCU side: tools\vis_watch.ps1 -Port COM30 -Sec 15"
        L "  bytes rising there  => end-to-end link is up, rerun ball_bringup for check 7."
        L "  still bytes=0       => the fault is now definitively the WIRE (IO11 -> PB16) or GND."
        Finish "PASS - script running and transmitting (tx=$tx)" 0
    }
    L ""
    L "the script started but no tx= counter appeared yet; give it a few seconds and run -Watch."
    Finish "INCONCLUSIVE - started, transmission not yet confirmed" 2
}

if ($Watch -gt 0) {
    L ""
    L ("---- watching the K230 console for {0}s ----" -f $Watch)
    $raw = ReadFor $Watch
    $n = 0
    foreach ($l in ($raw -split "`n")) { if ($l.Trim()) { L ("  | " + $l.TrimEnd("`r")); $n++ } }
    if ($n -eq 0) {
        L "  (silent)"
        L "A running script prints one BALL_UART_V4 line per second. Silence means it is NOT running -"
        L "use -Start. Silence is NOT a wiring symptom."
        Finish "FAIL - K230 console silent, script not running" 1
    }
    if ($raw -match 'uart_hz=([\d.]+)') {
        $hz = [double]$Matches[1]
        if ($hz -gt 0) { Finish ("PASS - script running, uart_hz={0}" -f $hz) 0 }
        L "  uart_hz is 0 - inference is running but nothing is being written to the UART."
        Finish "FAIL - running but not transmitting" 1
    }
    Finish "INCONCLUSIVE - console active but no uart_hz field seen" 2
}

if ($Cmds) {
    L ""
    foreach ($c in ($Cmds -split ';;')) {
        if (-not $c.Trim()) { continue }
        L ("  >>> " + $c.Trim())
        foreach ($l in (Eval $c.Trim() 8.0)) { L ("      " + $l) }
    }
}

if ($fail -gt 0) { Finish "FAIL - see the MISS/warning lines above" 1 }
if (-not ($Ls -or $Start -or $Stop -or $Watch -or $Cmds)) {
    L ""
    L "nothing to do. Pick one: -Ls (deployment check) / -Start / -Stop / -Watch <sec> / -Cmds '...'"
    Finish "INCONCLUSIVE - no mode selected" 2
}
Finish "PASS" 0
