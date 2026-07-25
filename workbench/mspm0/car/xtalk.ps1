# xtalk.ps1 - current-channel crosstalk measurement (needs m5 DUAL firmware).
# Drives M1/M2 independently (x/y), captures telemetry I:m1,m2. Idle motor's current
# should read ~0; if it reads hundreds of mA while the OTHER motor is driven => crosstalk.
param([int]$M1 = 0, [int]$M2 = 40, [int]$Sec = 3, [string]$Port = "COM30")

$out = "xtalk_out.txt"
Set-Content -Path $out -Value "== xtalk M1duty=$M1 M2duty=$M2 (I:m1,m2 ; idle motor should be ~0) =="

function Send-Cmd($p, [string]$cmd) {
    foreach ($ch in $cmd.ToCharArray()) { $p.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    $p.Write("`n"); Start-Sleep -Milliseconds 120
}
try {
    $p = New-Object System.IO.Ports.SerialPort $Port,115200,([System.IO.Ports.Parity]::None),8,([System.IO.Ports.StopBits]::One)
    $p.ReadTimeout = 300; $p.WriteTimeout = 300; $p.Open()
} catch { Add-Content -Path $out -Value ("OPEN_FAIL: " + $_.Exception.Message); return }

Start-Sleep -Milliseconds 200
try { $p.DiscardInBuffer() } catch {}
Send-Cmd $p "m5"                      # DUAL mode
Send-Cmd $p "f50"                     # 50ms telemetry
Send-Cmd $p ("x" + $M1)               # M1 duty %
Send-Cmd $p ("y" + $M2)               # M2 duty %

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sb = New-Object System.Text.StringBuilder
while ($sw.ElapsedMilliseconds -lt ($Sec * 1000)) {
    try { [void]$sb.Append($p.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 40
}
Send-Cmd $p "z"                       # stop
Add-Content -Path $out -Value $sb.ToString()
Add-Content -Path $out -Value "== xtalk end =="
try { $p.Close() } catch {}
