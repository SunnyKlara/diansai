# serial_logger.ps1 - PERSISTENT background telemetry logger.
# Runs indefinitely (start via control_pwsh_process), streams the board's heartbeat to a
# rolling CSV with wall-clock + elapsed timestamps and EVERY field. The judge operates the
# physical keys freely; this captures the whole session so any window can be analyzed offline
# without re-running short snapshot captures.
#
# Output (fixed paths so the agent always knows where to read):
#   logs/live_session.csv   - full per-sample CSV (header + rows, auto-flushed)
#   logs/live_session.txt   - last raw lines mirror (for quick tail)
#
# It sends ONE 'w20' at start to get 50Hz telemetry (telemetry rate only, no control change).
# Stop it via control_pwsh_process 'stop'. Re-running overwrites the session files.
param(
  [string]$Port = "COM10",
  [int]$Baud = 115200
)
$ErrorActionPreference = "Continue"
$logdir = Join-Path $PSScriptRoot "logs"
if (-not (Test-Path $logdir)) { New-Item -ItemType Directory -Path $logdir | Out-Null }
$csvPath = Join-Path $logdir "live_session.csv"
$rawPath = Join-Path $logdir "live_session.txt"
$cmdPath = Join-Path $logdir "cmd.txt"   # drop commands here; logger forwards each line to the port then deletes

$sp = New-Object System.IO.Ports.SerialPort
$sp.PortName=$Port; $sp.BaudRate=$Baud
$sp.Parity=[System.IO.Ports.Parity]::None; $sp.DataBits=8; $sp.StopBits=[System.IO.Ports.StopBits]::One
$sp.ReadTimeout=500; $sp.WriteTimeout=500
$sp.DtrEnable=$true; $sp.RtsEnable=$true   # some ST-Link VCPs only stream once DTR/RTS asserted (esp. after MCU reset)
try { $sp.Open() } catch { Write-Output ("OPEN FAIL: "+$_.Exception.Message); exit 1 }
Start-Sleep -Milliseconds 150
$sp.Write("w20" + [char]10)   # 50Hz telemetry (rate only, no control change)

# canonical column order; extra/unknown keys are appended on first sight
$keys = [System.Collections.ArrayList]@('H','T','E','P','M','D','R','RAW','A','F','PRE','CAS','RS','CM','FH','TID','TIN','TST')
# open with FileShare.ReadWrite so the analyzer can read the CSV WHILE we keep logging
$cfs = New-Object System.IO.FileStream($csvPath,[System.IO.FileMode]::Create,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$cw = New-Object System.IO.StreamWriter($cfs)
$cw.AutoFlush = $true
$cw.WriteLine("wall,elapsed_ms," + ($keys -join ','))
$rfs = New-Object System.IO.FileStream($rawPath,[System.IO.FileMode]::Create,[System.IO.FileAccess]::Write,[System.IO.FileShare]::ReadWrite)
$rw = New-Object System.IO.StreamWriter($rfs)
$rw.AutoFlush = $true

$tok=[regex]'([A-Za-z]+):(0x[0-9A-Fa-f]+|-?[\d.]+)'
$buf=""
$sw=[System.Diagnostics.Stopwatch]::StartNew()
$rawCount=0
Write-Output ("LOGGER running -> $csvPath  (Ctrl-C / stop to end)")
while ($true) {
  # --- command injection: forward any queued commands from cmd.txt, then delete it ---
  if (Test-Path $cmdPath) {
    try {
      $cmds = Get-Content $cmdPath -ErrorAction Stop
      Remove-Item $cmdPath -Force -ErrorAction SilentlyContinue
      foreach($c in $cmds){ if($c.Trim() -ne ''){ $sp.Write($c.Trim()+[char]10); $rw.WriteLine((Get-Date -Format "HH:mm:ss.fff")+"  >>SENT "+$c.Trim()); Start-Sleep -Milliseconds 90 } }
    } catch {}
  }
  try { $buf += $sp.ReadExisting() } catch {}
  while ($buf.Contains("`n")) {
    $i=$buf.IndexOf("`n"); $ln=$buf.Substring(0,$i).TrimEnd("`r"); $buf=$buf.Substring($i+1)
    if ($ln.Trim() -eq "") { continue }
    # mirror raw line (keep file from growing forever: rewrite header-less rolling tail)
    $rw.WriteLine((Get-Date -Format "HH:mm:ss.fff") + "  " + $ln)
    if ($ln -notmatch 'H:' -or $ln -notmatch 'P:') { continue }
    $m=[ordered]@{}; foreach($mm in $tok.Matches($ln)){ $m[$mm.Groups[1].Value]=$mm.Groups[2].Value }
    if (-not $m.Contains('H')) { continue }
    foreach($k in @($m.Keys)){ if(-not $keys.Contains($k)){ [void]$keys.Add($k) } }  # discover extras
    $row = (Get-Date -Format "HH:mm:ss.fff") + "," + [int]$sw.Elapsed.TotalMilliseconds
    foreach($k in $keys){ $row += "," + ($(if($m.Contains($k)){$m[$k]}else{''})) }
    $cw.WriteLine($row)
  }
  Start-Sleep -Milliseconds 5
}
