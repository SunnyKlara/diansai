# probe_serial.ps1 - non-destructive liveness probe for COM30 car firmware
# Captures RAW bytes (ReadExisting, no newline dependency) for N sec, reports byte count + text.
# Runs standalone in background to avoid SerialPort.Close deadlock blocking the agent.
param([string]$Port = "COM30", [int]$Sec = 3)

$out = "probe_out.txt"
Set-Content -Path $out -Value "== probe $Port start $(Get-Date -Format o) =="

try {
    $p = New-Object System.IO.Ports.SerialPort $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $p.ReadTimeout = 300
    $p.WriteTimeout = 300
    $p.Open()
} catch {
    Add-Content -Path $out -Value ("OPEN_FAIL: " + $_.Exception.Message)
    return
}

Start-Sleep -Milliseconds 200
try { $p.DiscardInBuffer() } catch {}
# nudge the firmware: '?' prints status, 'f200' enables telemetry (harmless in IDLE)
try { $p.Write("?`n") } catch {}
Start-Sleep -Milliseconds 300
try { $p.Write("f200`n") } catch {}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sb = New-Object System.Text.StringBuilder
while ($sw.ElapsedMilliseconds -lt ($Sec * 1000)) {
    try { [void]$sb.Append($p.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 60
}
$txt = $sb.ToString()
Add-Content -Path $out -Value ("BYTES_RECEIVED: " + $txt.Length)
Add-Content -Path $out -Value "---- RAW ----"
Add-Content -Path $out -Value $txt
Add-Content -Path $out -Value "== probe end =="
try { $p.Close() } catch {}
