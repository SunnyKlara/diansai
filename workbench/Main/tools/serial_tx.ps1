# serial_tx.ps1 - one-shot serial transact (ASCII only for PS5.1 safety)
# Opens the port, sends command(s), then prints everything received for -Watch seconds.
# Synchronous request/response -- ideal for AI auto-tuning (send, observe, decide).
# Commands separated by '|'. Empty -Cmd = watch-only (no send).
# Usage:
#   powershell -ExecutionPolicy Bypass -File serial_tx.ps1 -Cmd "m1500" -Watch 3
#   powershell -ExecutionPolicy Bypass -File serial_tx.ps1 -Cmd "kp120|ki0|kd200|a" -Watch 5
#   powershell -ExecutionPolicy Bypass -File serial_tx.ps1 -Cmd "" -Watch 4   (watch only)
param([string]$Port = "COM9", [int]$Baud = 115200, [string]$Cmd = "", [double]$Watch = 3.0)

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, 'None', 8, 'One')
$sp.ReadTimeout = 200; $sp.WriteTimeout = 500
try { $sp.Open() } catch { Write-Host ("OPEN FAIL: " + $_.Exception.Message); exit 1 }

Start-Sleep -Milliseconds 150
$sp.ReadExisting() | Out-Null            # flush stale buffer

if ($Cmd -ne "") {
    foreach ($c in $Cmd.Split('|')) {
        $t = $c.Trim()
        if ($t -ne "") { $sp.Write($t + "`n"); Write-Host ("SENT> " + $t); Start-Sleep -Milliseconds 120 }
    }
}

$end = (Get-Date).AddSeconds($Watch)
$acc = ""
while ((Get-Date) -lt $end) {
    try { $acc += $sp.ReadExisting() } catch {}
    Start-Sleep -Milliseconds 40
}
$sp.Close()
Write-Host "--- RX ---"
$acc.Replace("`r","").TrimEnd() -split "`n" | ForEach-Object { Write-Host $_ }
