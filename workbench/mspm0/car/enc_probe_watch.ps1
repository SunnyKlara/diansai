# enc_probe_watch.ps1 - 编码器探针实时监看 (配合 car.c ENC_PROBE=1 固件)
# 用法:
#   .\enc_probe_watch.ps1              # 一直看, Ctrl+C 停
#   .\enc_probe_watch.ps1 -Seconds 120 # 看 120s 后自动停(后台采集用)
#   .\enc_probe_watch.ps1 -Log cap.txt # 同时把每行存到 cap.txt
# 读的是 [probe] 行: L(原始电平) E(软件轮询上升沿,不经中断) I(中断ISR计数)
param([int]$Seconds = 0, [string]$Log = "")

[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$port = New-Object System.IO.Ports.SerialPort('COM30', 115200, 'None', 8, 'One')
$port.ReadTimeout = 500
$port.Open()
Write-Host "== 监看 COM30 (编码器探针). 慢转轮子, 看 L 电平跳/E/I 计数涨 ==" -ForegroundColor Cyan
$end = if ($Seconds -gt 0) { (Get-Date).AddSeconds($Seconds) } else { [DateTime]::MaxValue }
try {
    while ((Get-Date) -lt $end) {
        try { $line = $port.ReadLine() } catch { continue }
        if ($line -notmatch '\[probe\]') { continue }
        $stamp = (Get-Date).ToString('HH:mm:ss.fff')
        $out = "$stamp  $line"
        # 电平非全 0 或计数非 0 时高亮, 方便一眼看出"动了"
        if ($line -match 'A1=1|B1=1|A2=1|B2=1' -or $line -match 'E [1-9]|I [1-9]' -or $line -match 'E \d+,[1-9]|I \d+,[1-9]') {
            Write-Host $out -ForegroundColor Green
        } else {
            Write-Host $out -ForegroundColor DarkGray
        }
        if ($Log) { Add-Content -Path $Log -Value $out -Encoding UTF8 }
    }
} finally { $port.Close(); Write-Host "== 停止监看 ==" -ForegroundColor Cyan }
