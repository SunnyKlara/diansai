# serial_collect.ps1 - resilient BACKGROUND serial logger.
# Continuously reads the heartbeat and appends timestamped lines to a log file,
# auto-reconnecting if the port drops (board reset during flash) or the .NET
# SerialPort goes zombie (the ~1min stall noted in the debug journal).
# Meant to run in the background while the user flashes/resets; the agent tails
# the log file to analyze.
# Usage:
#   powershell -ExecutionPolicy Bypass -File serial_collect.ps1 -Port COM10
param(
  [string]$Port = "COM10",
  [int]$Baud = 115200,
  [string]$Log = ""
)
if ($Log -eq "") { $Log = Join-Path $PSScriptRoot "serial_log.txt" }
# fresh start
"# serial_collect start $(Get-Date -Format o) port=$Port" | Set-Content -Path $Log -Encoding UTF8
$sw = [System.Diagnostics.Stopwatch]::StartNew()

while ($true) {
  $sp = $null
  try {
    $sp = New-Object System.IO.Ports.SerialPort($Port, $Baud, 'None', 8, 'One')
    $sp.ReadTimeout = 500; $sp.WriteTimeout = 500
    $sp.Open()
    ("# OPEN ok t={0}ms" -f [int]$sw.Elapsed.TotalMilliseconds) | Add-Content -Path $Log -Encoding UTF8
    $sp.ReadExisting() | Out-Null
    $buf = ""
    $lastData = [System.Diagnostics.Stopwatch]::StartNew()
    while ($true) {
      $chunk = ""
      try { $chunk = $sp.ReadExisting() } catch {}
      if ($chunk -ne "") {
        $buf += $chunk; $lastData.Restart()
        while ($buf.Contains("`n")) {
          $i = $buf.IndexOf("`n"); $ln = $buf.Substring(0,$i).TrimEnd("`r"); $buf = $buf.Substring($i+1)
          if ($ln.Trim() -ne "") {
            ("{0}`t{1}" -f [int]$sw.Elapsed.TotalMilliseconds, $ln) | Add-Content -Path $Log -Encoding UTF8
          }
        }
      } else {
        Start-Sleep -Milliseconds 20
      }
      # zombie/stall watchdog: no bytes for 3s -> force reconnect
      if ($lastData.Elapsed.TotalSeconds -gt 3.0) {
        ("# STALL {0}ms -> reconnect" -f [int]$sw.Elapsed.TotalMilliseconds) | Add-Content -Path $Log -Encoding UTF8
        break
      }
    }
  } catch {
    ("# ERR {0}ms {1}" -f [int]$sw.Elapsed.TotalMilliseconds, $_.Exception.Message) | Add-Content -Path $Log -Encoding UTF8
  } finally {
    if ($sp -ne $null) { try { $sp.Close() } catch {} ; try { $sp.Dispose() } catch {} }
  }
  Start-Sleep -Milliseconds 400   # let port re-enumerate after reset
}
