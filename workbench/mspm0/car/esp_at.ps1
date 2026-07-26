# esp_at.ps1 - one-shot AT transaction console for ESP-01S (ESP8266 AT firmware).
#   Opens port -> optional flush -> sends each command -> collects reply -> writes RESULT FILE -> closes.
#   Result is written to file BEFORE Close(), because .NET SerialPort.Close() can deadlock while
#   data keeps streaming (see .kiro/steering/knowledge/跨题坑库.md). Read the file if stdout is lost.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_at.ps1 -Port COM5 -Baud 115200 -Cmds "AT","AT+GMR"
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_at.ps1 -Port COM5 -Cmds "AT" -WaitMs 1500 -Out at.txt
#   -Raw          : send payload WITHOUT CRLF (for transparent-mode data tests)
#   -NoNewline    : alias of -Raw
#   -ListenMs N   : after all commands, keep listening N ms and append whatever arrives
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM5",
    [int]$Baud       = 115200,
    # semicolon-separated command list, e.g. -Cmds "AT;AT+GMR;AT+CWMODE?"
    # (a real string[] param gets collapsed into one element when called via `powershell -File`)
    # QUOTES: write AT string args with SINGLE quotes - the script converts ' -> " before sending.
    #   Nested double quotes get eaten by the shell layers; single quotes survive intact.
    #   e.g.  -Cmds "AT+CWJAP_DEF='DIANSAI_CAR','car2026wifi'"
    [string]$Cmds    = "AT",
    # one command per line in a file (comments with # allowed); wins over -Cmds when given
    [string]$CmdFile = "",
    [int]$WaitMs     = 900,
    [int]$ListenMs   = 0,
    [switch]$Raw,
    [switch]$Flush,
    # Send the '+++' passthrough-escape dance BEFORE the commands.
    # Needed whenever a previous run left the module in transparent mode: in that state the
    # module treats everything you send as DATA (transmits it over the air) and answers nothing
    # on the UART, so it looks dead. Harmless if the module is already in AT mode.
    [switch]$Escape,
    [string]$Out     = "esp_at_out.txt"
)

$lines = New-Object System.Text.StringBuilder
function Log([string]$s) { [void]$lines.AppendLine($s) }

Log "==== esp_at $Port @ $Baud  ($(Get-Date -Format 'HH:mm:ss')) ===="

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout  = 300
$sp.WriteTimeout = 1000
$sp.NewLine      = "`r`n"
# ESP-01S has no DTR/RTS wired on the 4-wire hookup; keep them low so nothing gets reset.
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false

try {
    $sp.Open()
} catch {
    Log "OPEN_FAIL: $($_.Exception.Message)"
    Set-Content -Path $Out -Value $lines.ToString() -Encoding ASCII
    Write-Output $lines.ToString()
    exit 1
}

Start-Sleep -Milliseconds 200

if ($Escape) {
    # spec: >=1s of UART silence, then exactly "+++" with NO CRLF, then >=1s of silence
    Log "[escape] sending +++ to leave passthrough (1s silence each side)"
    Start-Sleep -Milliseconds 1100
    try { $sp.Write("+++") } catch { Log "escape write failed: $($_.Exception.Message)" }
    Start-Sleep -Milliseconds 1100
    # If the module was ALREADY in AT mode, those three '+' chars are now sitting in its line
    # buffer and would corrupt the next command ("+++AT"). Terminate the line and throw the
    # resulting ERROR away, so the first real command starts from a clean buffer either way.
    try { $sp.Write("`r`n") } catch {}
    Start-Sleep -Milliseconds 300
    try { $sp.DiscardInBuffer() } catch {}
}

if ($Flush) { try { $sp.DiscardInBuffer() } catch {} ; Start-Sleep -Milliseconds 150 }

# whatever was already sitting in the buffer (boot log / leftovers) - useful, not noise
$pre = ""
try { $pre = $sp.ReadExisting() } catch {}
if ($pre.Length -gt 0) { Log "[PRE-BUFFER] >>>"; Log $pre; Log "<<<" }

$rawList = @()
if ($CmdFile.Length -gt 0) {
    foreach ($ln in (Get-Content -Path $CmdFile)) {
        $s = $ln.Trim()
        if ($s.Length -eq 0) { continue }
        if ($s.StartsWith('#')) { continue }
        $rawList += $s
    }
} else {
    $rawList = $Cmds -split ';'
}
$cmdList = @()
foreach ($piece in $rawList) {
    if ($piece.Length -gt 0) { $cmdList += ($piece -replace "'", '"') }
}

foreach ($c in $cmdList) {
    if ($Raw) {
        Log "--> (raw) $c"
        try { $sp.Write($c) } catch { Log "WRITE_FAIL: $($_.Exception.Message)" }
    } else {
        Log "--> $c"
        try { $sp.Write($c + "`r`n") } catch { Log "WRITE_FAIL: $($_.Exception.Message)" }
    }
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $WaitMs) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 40
    }
    $t = $rx.ToString()
    if ($t.Length -eq 0) { Log "<-- (nothing)" } else { Log "<-- $t" }
}

if ($ListenMs -gt 0) {
    Log "--- listening $ListenMs ms ---"
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $ListenMs) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 50
    }
    Log $rx.ToString()
}

Log "==== END ===="
# file first, close after (Close may hang)
Set-Content -Path $Out -Value $lines.ToString() -Encoding ASCII
Write-Output $lines.ToString()
try { $sp.Close() } catch {}
