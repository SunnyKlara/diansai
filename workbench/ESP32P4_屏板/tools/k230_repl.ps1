# ASCII only (PowerShell 5.1 encoding pitfall).
#
# Talk to the K230 board (LuShanPai / CanMV) over its USB CDC serial port and
# capture a transcript. Used to establish HARD FACTS about the K230 half of the
# video-link plan before writing any code:
#   - which system answers on the port (MicroPython REPL? RT-Thread msh? Linux sh?)
#   - is there a camera sensor attached at all
#   - does the board have Wi-Fi (the K230 SoC has none -- it has to come from a
#     module on the board), and if so, can it act as STA or AP
#
# Usage:
#   .\k230_repl.ps1                                   # just wake it up and grab the prompt
#   .\k230_repl.ps1 -Cmds "import sys","sys.platform"  # send lines, capture replies
#   .\k230_repl.ps1 -File probe.txt                    # one command per line (# = comment)
#   .\k230_repl.ps1 -Interrupt:$false                  # do NOT send Ctrl-C first
#
# Notes learnt the hard way on this machine:
#   - write the transcript to a FILE and read it back; piping serial output straight
#     to the console gets swallowed here (see the repo pitfall log)
#   - send line by line with a gap: a burst can overrun the target's RX
#   - Close() last, after DiscardInBuffer(), or it can deadlock while data streams

param(
    [string]$Port      = "COM3",
    [int]$Baud         = 115200,
    [string[]]$Cmds    = @(),
    [string]$File      = "",
    [int]$GapMs        = 400,      # wait after each line before reading
    [int]$SettleSec    = 2,        # initial read window
    [switch]$Interrupt = $true,    # send Ctrl-C to break into a REPL
    [switch]$Paste,                # wrap everything in Ctrl-E .. Ctrl-D (MicroPython paste mode)
    [switch]$SoftReset,            # Ctrl-D at the bare prompt = soft reboot, frees the heap first
    [switch]$HardReset,            # machine.reset(): the ONLY way to clear the network stack
    [int]$PasteWaitMs  = 4000,     # how long to read after Ctrl-D
    [string]$Out       = "d:\diansai\.tmp_pdf\esp32p4\k230_repl.txt"
)

# WHY -Paste exists (cost one wasted probe run): fed line by line, the plain REPL
# treats `try:` / `for:` / `if:` as the start of a continuation block, and every
# following line nests deeper ("... " prompts stack up) instead of executing.
# Any multi-line construct MUST go through paste mode. Single-line statements
# (imports, prints, assignments) are fine without it.

$ErrorActionPreference = "Continue"

if ($File -and (Test-Path $File)) {
    $Cmds = Get-Content $File -Encoding UTF8 |
            Where-Object { $_.Trim() -ne "" -and -not $_.Trim().StartsWith("#") }
}

$ports = [System.IO.Ports.SerialPort]::GetPortNames()
if ($ports -notcontains $Port) {
    "RESULT: INCONCLUSIVE`nREASON: $Port not present (seen: $($ports -join ','))" |
        Set-Content $Out -Encoding UTF8
    Write-Output "RESULT: INCONCLUSIVE -- $Port not present (seen: $($ports -join ','))"
    exit 3
}

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout  = 200
$sp.WriteTimeout = 1000
$sp.NewLine      = "`r`n"
try { $sp.Open() } catch {
    "RESULT: FAIL`nREASON: open $Port -> $($_.Exception.Message)" | Set-Content $Out -Encoding UTF8
    Write-Output "RESULT: FAIL -- cannot open $Port : $($_.Exception.Message)"
    exit 1
}
# Some CDC targets only send once DTR is asserted; keep RTS low so we do not reset anything.
$sp.DtrEnable = $true
$sp.RtsEnable = $false

$sb = New-Object System.Text.StringBuilder
function Drain([int]$ms) {
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalMilliseconds -lt $ms) {
        try { $c = $sp.ReadExisting(); if ($c) { [void]$sb.Append($c) } } catch {}
        Start-Sleep -Milliseconds 30
    }
}

[void]$sb.AppendLine("### port=$Port baud=$Baud  $(Get-Date -Format 'HH:mm:ss')")
Drain 300
if ($Interrupt) {
    [void]$sb.AppendLine("### >>> <Ctrl-C>")
    $sp.Write([char]3 + "")
    Start-Sleep -Milliseconds 300
    $sp.Write([char]3 + "")
    Drain 500
}
if ($HardReset) {
    # A MicroPython soft reboot does NOT reset the RT-Smart network stack, so a
    # stale association / DHCP lease / ARP entry survives it. machine.reset() is
    # the only way to get a genuinely clean network state between experiments.
    #
    # MEASURED PITFALL: machine.reset() makes the USB CDC port disappear and
    # re-enumerate, which kills the already-open handle -- everything sent after
    # that silently goes nowhere (first attempt lost a whole run this way).
    # So: send it, CLOSE, wait for the port to come back, REOPEN.
    [void]$sb.AppendLine("### >>> machine.reset() (port will re-enumerate)")
    $sp.Write("`r`n")
    Start-Sleep -Milliseconds 200
    $sp.Write("import machine`r`n")
    Start-Sleep -Milliseconds 300
    try { $sp.Write("machine.reset()`r`n") } catch {}
    Start-Sleep -Milliseconds 500
    try { $sp.Close() } catch {}

    $gone = $false
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 40) {
        $now = [System.IO.Ports.SerialPort]::GetPortNames()
        if (-not $gone) {
            if ($now -notcontains $Port) { $gone = $true }
        } elseif ($now -contains $Port) {
            Start-Sleep -Milliseconds 1500        # let the CDC settle before opening
            break
        }
        Start-Sleep -Milliseconds 250
    }
    [void]$sb.AppendLine("### >>> port re-enumerated (disappeared=$gone), reopening")
    $sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
    $sp.ReadTimeout  = 200
    $sp.WriteTimeout = 1000
    $sp.NewLine      = "`r`n"
    $opened = $false
    for ($i = 0; $i -lt 20; $i++) {
        try { $sp.Open(); $opened = $true; break } catch { Start-Sleep -Milliseconds 750 }
    }
    if (-not $opened) {
        [System.IO.File]::WriteAllText($Out, $sb.ToString() + "`nRESULT: FAIL reopen $Port after reset`n", (New-Object System.Text.UTF8Encoding($false)))
        Write-Output "RESULT: FAIL -- could not reopen $Port after machine.reset()"
        exit 1
    }
    $sp.DtrEnable = $true
    $sp.RtsEnable = $false
    Drain 3000
    $sp.Write([char]3 + "")     # Ctrl-C: make sure we are at a bare prompt
    Drain 500
}
if ($SoftReset) {
    # MEASURED PITFALL: pasting script after script into the REPL leaks the heap.
    # It surfaces as OSError(12)/ENOMEM from something completely unrelated -- a
    # socket.connect() failed here, and the tell was that even open().read() on a
    # small file failed the same way. A soft reboot (Ctrl-D at the bare prompt,
    # NOT in paste mode where Ctrl-D means "run") gives a clean heap.
    [void]$sb.AppendLine("### >>> <Ctrl-D soft reset>")
    $sp.Write([char]4 + "")
    Drain 3000
}
[void]$sb.AppendLine("### >>> <CR>")
$sp.Write("`r`n")
Drain ($SettleSec * 1000)

if ($Paste) {
    [void]$sb.AppendLine("### >>> <Ctrl-E paste mode>")
    $sp.Write([char]5 + "")          # Ctrl-E: enter paste mode
    Drain 300
    foreach ($c in $Cmds) {
        $sp.Write($c + "`r")         # paste mode wants bare CR, no echo processing
        Start-Sleep -Milliseconds 60
    }
    [void]$sb.AppendLine("### >>> <Ctrl-D run>")
    $sp.Write([char]4 + "")          # Ctrl-D: execute the pasted block
    Drain $PasteWaitMs
} else {
    foreach ($c in $Cmds) {
        [void]$sb.AppendLine("### >>> $c")
        $sp.Write($c + "`r`n")
        Drain $GapMs
    }
}

Drain 500
[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
try { $sp.DiscardInBuffer() } catch {}
try { $sp.Close() } catch {}
Write-Output "DONE bytes=$($sb.Length) file=$Out"
