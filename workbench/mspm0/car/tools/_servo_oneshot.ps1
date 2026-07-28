# One-shot servo moves with long dwells, so a human can watch each single jump clearly.
# Each U<us> is ONE command (one line, one \n) -> the firmware receives exactly one target and
# the servo makes one full-speed move to it. No intermediate values, no ramp.
# Purpose: decide whether a SINGLE commanded jump is crisp (one move) or crawls/chatters.
# ASCII-only by repo rule.
param([string]$Port = 'COM30', [int]$Baud = 115200)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 300
$sp.Open()
Start-Sleep -Milliseconds 250
try { $sp.DiscardInBuffer() } catch { }

function Go([int]$us, [int]$dwellMs, [string]$label) {
    foreach ($ch in ("U$us" + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    Start-Sleep -Milliseconds 400
    $echo = $sp.ReadExisting()
    $line = @($echo -split "`n" | Where-Object { $_ -match '\[srv\] us=(\d+)' })
    $shown = if ($line.Count) { ($line[-1] -replace '\| center.*','').Trim() } else { '(no echo)' }
    Write-Output ("{0,-14} U{1}  ->  {2}" -f $label, $us, $shown)
    Start-Sleep -Milliseconds $dwellMs
}

Write-Output 'watch the servo horn; each line below is ONE single move (no ramp, no repeats):'
Go 1500 3000 'CENTER'
Go 1900 4000 'RIGHT? (1900)'
Go 1500 4000 'CENTER'
Go 1100 4000 'LEFT?  (1100)'
Go 1500 1500 'CENTER'
$sp.Close()
Write-Output 'done. report per move: crisp single move / crawl / chatter; and which physical side 1900 went.'
