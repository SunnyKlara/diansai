# iloop.ps1 - current-loop (m1 MODE_CURRENT) step capture. Drives BOTH motors to a
# current target (mA); on a free load the motor accelerates (torque control) so watch
# the RISE of I:i1,i2 toward Target during accel. Tune gkp[0]/gki[0] (p/i in m1).
param([int]$Target = 150, [int]$Kp = 200, [int]$Ki = 20, [int]$Kd = 0, [int]$Sec = 3, [string]$Port = "COM30")

$out = "iloop_out.txt"
Set-Content -Path $out -Value "== iloop m1 Target=${Target}mA Kp=$Kp Ki=$Ki Kd=$Kd (milli) =="
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
Send-Cmd $p "m1"                      # CURRENT mode
Send-Cmd $p "f20"                     # fast telemetry
Send-Cmd $p ("p" + $Kp); Send-Cmd $p ("i" + $Ki); Send-Cmd $p ("d" + $Kd)
try { $p.DiscardInBuffer() } catch {}
Send-Cmd $p ("t" + $Target)           # step current target (mA)

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sb = New-Object System.Text.StringBuilder
while ($sw.ElapsedMilliseconds -lt ($Sec * 1000)) {
    try { [void]$sb.Append($p.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 40
}
Send-Cmd $p "z"                       # stop
Add-Content -Path $out -Value $sb.ToString()
Add-Content -Path $out -Value "== iloop end =="
try { $p.Close() } catch {}
