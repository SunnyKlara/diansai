# disturb_test.ps1 - serial capture for disturbance-rejection test.
#   Sends setup commands, then streams telemetry to a file for N seconds.
#   Writes each read to disk immediately -> data survives even if Close() deadlocks.
#   Sends chars one-by-one with 25ms gap to avoid MCU RX FIFO overflow on long cmds.
#   ASCII comments only (PowerShell 5.1 mis-decodes UTF-8 .ps1 -> keep comments ASCII).
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File disturb_test.ps1 -Setup "f50;m2;t400" -Seconds 30 -Out disturb_out.txt
param(
    [string]$Port      = "COM30",
    [int]$Baud         = 115200,
    [string]$Setup     = "",
    [int]$Seconds      = 20,
    [string]$Out       = "disturb_out.txt",
    [string]$StopAfter = "z",
    [string]$StepCmd   = "",
    [int]$StepAtMs     = 300
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 300
try { $sp.Open() } catch { "OPEN_FAIL: $($_.Exception.Message)" | Out-File $Out -Encoding ascii; exit 1 }

Start-Sleep -Milliseconds 300
try { $sp.DiscardInBuffer() } catch {}

# send setup commands (semicolon separated), char-by-char with gap
if ($Setup) {
    foreach ($cmd in $Setup.Split(';')) {
        if ($cmd.Trim() -eq "") { continue }
        foreach ($ch in ($cmd.Trim() + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
        Start-Sleep -Milliseconds 200
    }
}
Start-Sleep -Milliseconds 150
try { $sp.DiscardInBuffer() } catch {}

# stream to file, flushing each read to disk
"==== capture start $(Get-Date -Format HH:mm:ss) setup=[$Setup] dur=${Seconds}s ====" | Out-File $Out -Encoding ascii
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$stepSent = $false
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    # fire a step command mid-capture (e.g. t150) to catch the transient with pre-step baseline
    if ($StepCmd -and (-not $stepSent) -and ($sw.Elapsed.TotalMilliseconds -ge $StepAtMs)) {
        foreach ($ch in ($StepCmd.Trim() + "`n").ToCharArray()) { try { $sp.Write([string]$ch) } catch {}; Start-Sleep -Milliseconds 20 }
        $stepSent = $true
        [System.IO.File]::AppendAllText((Resolve-Path $Out), "---- STEP sent: $StepCmd @ $([int]$sw.Elapsed.TotalMilliseconds)ms ----`n")
    }
    try { $d = $sp.ReadExisting() } catch { $d = "" }
    if ($d) { [System.IO.File]::AppendAllText((Resolve-Path $Out), $d) }
    Start-Sleep -Milliseconds 40
}

# send stop command before closing (so motors stop even if Close deadlocks)
if ($StopAfter) {
    foreach ($ch in ($StopAfter.Trim() + "`n").ToCharArray()) { try { $sp.Write([string]$ch) } catch {}; Start-Sleep -Milliseconds 25 }
    Start-Sleep -Milliseconds 200
}
try { $sp.Close() } catch {}
[System.IO.File]::AppendAllText((Resolve-Path $Out), "`n==== capture end (stopped='$StopAfter') ====`n")
