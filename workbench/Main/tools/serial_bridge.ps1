# serial_bridge.ps1 - background serial RX/TX bridge (ASCII only for PS5.1 safety)
# RX: print board output (AI reads via get_process_output) + append to serial_log.txt
# TX: watch serial_cmd.txt, send new appended lines to the board (cmd file append-only)
# Usage: powershell -ExecutionPolicy Bypass -File serial_bridge.ps1 -Port COM8 -Baud 115200
# NOTE: COM port is exclusive. Close any other serial tool first.
param([string]$Port = "COM8", [int]$Baud = 115200)

$dir     = Split-Path -Parent $MyInvocation.MyCommand.Path
$cmdFile = Join-Path $dir "serial_cmd.txt"
$logFile = Join-Path $dir "serial_log.txt"
if (-not (Test-Path $cmdFile)) { New-Item -ItemType File -Path $cmdFile | Out-Null }
"--- bridge start ---" | Out-File -FilePath $logFile -Encoding ascii

$sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, 'None', 8, 'One')
$sp.ReadTimeout  = 100
$sp.WriteTimeout = 500
try { $sp.Open() }
catch { Write-Host ("OPEN FAIL: " + $_.Exception.Message + " -- close other serial tool using " + $Port); exit 1 }
Write-Host ("BRIDGE OPEN " + $Port + " @" + $Baud)

$cmdSent = @(Get-Content $cmdFile -ErrorAction SilentlyContinue).Count
$buf = ""
$nl  = [char]10
$cr  = [char]13

while ($true) {
    try {
        if ($sp.BytesToRead -gt 0) {
            $buf += $sp.ReadExisting()
            while ($buf.Contains($nl)) {
                $idx  = $buf.IndexOf($nl)
                $line = $buf.Substring(0, $idx).TrimEnd($cr)
                $buf  = $buf.Substring($idx + 1)
                $out  = (Get-Date -Format 'HH:mm:ss.fff') + "  " + $line
                Write-Host $out
                Add-Content -Path $logFile -Value $out
            }
        }
    } catch {}

    try {
        $lines = @(Get-Content $cmdFile -ErrorAction SilentlyContinue)
        if ($lines.Count -gt $cmdSent) {
            for ($i = $cmdSent; $i -lt $lines.Count; $i++) {
                $c = $lines[$i].Trim()
                if ($c -ne "") { $sp.Write($c + $nl); Write-Host ("SENT> " + $c) }
            }
            $cmdSent = $lines.Count
        }
    } catch {}

    Start-Sleep -Milliseconds 50
}
