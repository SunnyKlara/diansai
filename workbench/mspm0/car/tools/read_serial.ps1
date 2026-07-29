# read_serial.ps1 - 一次性事务式串口采集 (打开->采N秒->关口)
# 用法: powershell -ExecutionPolicy Bypass -File read_serial.ps1 [-Port COM30] [-Baud 115200] [-Seconds 12]
param(
    [string]$Port = "COM30",
    [int]$Baud = 115200,
    [int]$Seconds = 12
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 500
try {
    $sp.Open()
} catch {
    Write-Output "OPEN_FAIL: $($_.Exception.Message)"
    exit 1
}

# 冲掉 VCOM 里的陈旧缓冲(避免读到上一轮的半截数据),再稳定 300ms
Start-Sleep -Milliseconds 300
try { $sp.DiscardInBuffer() } catch {}
Start-Sleep -Milliseconds 200
try { $sp.DiscardInBuffer() } catch {}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$buf = New-Object System.Text.StringBuilder
while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    try { [void]$buf.Append($sp.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 80
}
$sp.Close()

Write-Output "==== RAW CAPTURE ($Seconds s @ $Port $Baud) ===="
Write-Output $buf.ToString()
Write-Output "==== END ===="
