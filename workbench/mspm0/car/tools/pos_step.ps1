# pos_step.ps1 - position-loop step capture for the car (MSPM0 COM30)
# Sets the tuned speed INNER loop, enters position mode, steps the target (counts,
# relative to entry since m3 zeroes target), captures raw telemetry (incl C=count).
# Runs standalone in background (avoids SerialPort.Close deadlock blocking the agent).
#
# Params:
#   -Target  position step in counts (m3 zeroes at entry; ~899 counts = 1 rev)
#   -Kp/-Ki/-Kd  POSITION outer-loop gains (milli, i.e. p<Kp> => Kp/1000)
#   -SpdKp/-SpdKi inner SPEED-loop gains to assert first (default tuned 30/20 = 0.03/0.02)
#   -Sec     capture seconds
param([int]$Target = 900, [int]$Kp = 50, [int]$Ki = 0, [int]$Kd = 0,
      [int]$SpdKp = 30, [int]$SpdKi = 20, [int]$W = -1, [int]$E = -1,
      [int]$Sec = 5, [string]$Port = "COM30")

$out = "pos_out.txt"
Set-Content -Path $out -Value "== pos step Target=$Target Kp=$Kp Ki=$Ki Kd=$Kd (milli) spd=$SpdKp/$SpdKi =="

function Send-Cmd($p, [string]$cmd) {
    foreach ($ch in $cmd.ToCharArray()) { $p.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    $p.Write("`n"); Start-Sleep -Milliseconds 120
}

try {
    $p = New-Object System.IO.Ports.SerialPort $Port, 115200, ([System.IO.Ports.Parity]::None), 8, ([System.IO.Ports.StopBits]::One)
    $p.ReadTimeout = 300; $p.WriteTimeout = 300; $p.Open()
} catch { Add-Content -Path $out -Value ("OPEN_FAIL: " + $_.Exception.Message); return }

Start-Sleep -Milliseconds 200
try { $p.DiscardInBuffer() } catch {}

# 1) assert tuned speed inner loop (chip may have reset to flashed default 0.10)
Send-Cmd $p "m2"
Send-Cmd $p ("p" + $SpdKp)
Send-Cmd $p ("i" + $SpdKi)
# 2) fast telemetry for transient capture
Send-Cmd $p "f20"
# 3) enter position mode (zeroes target -> drives to entry point), set outer gains
Send-Cmd $p "m3"
Send-Cmd $p ("p" + $Kp)
Send-Cmd $p ("i" + $Ki)
Send-Cmd $p ("d" + $Kd)
if ($W -ge 0) { Send-Cmd $p ("w" + $W) }   # deadzone feedforward % (-1 = leave firmware default)
if ($E -ge 0) { Send-Cmd $p ("e" + $E) }   # arrival tolerance counts (-1 = leave default)
Start-Sleep -Milliseconds 1500          # let it settle at the entry zero
try { $p.DiscardInBuffer() } catch {}

# 4) step the position target, capture
Send-Cmd $p ("t" + $Target)
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$sb = New-Object System.Text.StringBuilder
while ($sw.ElapsedMilliseconds -lt ($Sec * 1000)) {
    try { [void]$sb.Append($p.ReadExisting()) } catch {}
    Start-Sleep -Milliseconds 40
}
# 5) stop + restore
Send-Cmd $p "z"
Send-Cmd $p "f100"

Add-Content -Path $out -Value "---- RAW (watch C:c1,c2 -> Target) ----"
Add-Content -Path $out -Value $sb.ToString()
Add-Content -Path $out -Value "== pos end =="
try { $p.Close() } catch {}
