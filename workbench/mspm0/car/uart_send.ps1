# uart_send.ps1 - send one command line to COM30 and read the reply.
#   For live tuning of the motor-control firmware (modes / target / gains).
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File uart_send.ps1 -Cmd "m1" [-Port COM30] [-Baud 115200] [-ReadMs 800]
# Commands the firmware accepts: m<0-4> mode | t<v> target | p/i/d<x1000> gains | z stop | ? status
# NOTE: exclusive port - close watch_serial / other serial tools first.
param(
    [string]$Cmd  = "?",
    [string]$Port = "COM30",
    [int]$Baud    = 115200,
    [int]$ReadMs  = 800
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 300
try {
    $sp.Open()
} catch {
    Write-Output "OPEN_FAIL: $($_.Exception.Message)"
    exit 1
}
Start-Sleep -Milliseconds 250
try { $sp.DiscardInBuffer() } catch {}
# 逐字符发送 + 小间隔: 防止 MCU RX FIFO(小) 在一次突发里溢出丢字节(短命令没事,长命令如 t100 会丢)
foreach ($ch in ($Cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
Start-Sleep -Milliseconds $ReadMs
Write-Output $sp.ReadExisting()
$sp.Close()
