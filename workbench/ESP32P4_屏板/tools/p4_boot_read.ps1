# ASCII only. Reset ESP32-P4 via RTS on USB-Serial-JTAG CDC port, then capture boot log.
# Usage: powershell -File p4_boot_read.ps1 [-Port COM7] [-Seconds 8]
param(
    [string]$Port = "COM7",
    [int]$Seconds = 8,
    [string]$Out = "d:\diansai\.tmp_pdf\esp32p4\boot_log.txt",
    [switch]$NoReset   # passive listen only: do NOT touch RTS, keep the running app alive
)

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$sp.ReadTimeout = 200
try {
    $sp.Open()
} catch {
    Write-Output "OPEN_FAIL $($_.Exception.Message)"
    exit 1
}

# DTR drives boot strapping on USB-Serial-JTAG: keep it de-asserted so chip runs app, not download mode.
$sp.DtrEnable = $false
Start-Sleep -Milliseconds 50
if (-not $NoReset) {
    # RTS pulse = hard reset (same line esptool reports as "Hard resetting via RTS pin")
    $sp.RtsEnable = $true
    Start-Sleep -Milliseconds 150
    $sp.RtsEnable = $false
}

$sb = New-Object System.Text.StringBuilder
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    try {
        $chunk = $sp.ReadExisting()
        if ($chunk) { [void]$sb.Append($chunk) }
    } catch {}
    Start-Sleep -Milliseconds 40
}

[System.IO.File]::WriteAllText($Out, $sb.ToString(), (New-Object System.Text.UTF8Encoding($false)))
# Close last: SerialPort.Close() can deadlock while data still streaming (see repo pitfall log)
try { $sp.DiscardInBuffer() } catch {}
try { $sp.Close() } catch {}
Write-Output "DONE bytes=$($sb.Length) file=$Out"
