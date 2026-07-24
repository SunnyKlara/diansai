# watch_serial.ps1 - live serial monitor for COM30.
#   Streams every line as it arrives, parses I1/I2 raw values, and color-codes
#   STOP rows by the "current must be ~0 when motor stopped" rule:
#     GREEN = connected (STOP raw < threshold)   RED = floating (STOP raw still large)
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File watch_serial.ps1 [-Port COM30] [-Baud 115200] [-StopThresh 30]
# Quit: Ctrl+C
# NOTE: a COM port can be opened by only ONE program at a time. Close other serial
#       monitors (and the AI-side capture) before running this.
param(
    [string]$Port = "COM30",
    [int]$Baud = 115200,
    [int]$StopThresh = 30
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200
try {
    $sp.Open()
} catch {
    Write-Host "[X] open $Port failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "    A COM port is exclusive - close other serial tools/scripts first." -ForegroundColor DarkYellow
    exit 1
}
Start-Sleep -Milliseconds 200
try { $sp.DiscardInBuffer() } catch {}

Write-Host "===== live current monitor $Port @ $Baud  (Ctrl+C to quit) =====" -ForegroundColor Cyan
Write-Host "rule: on STOP rows, raw < $StopThresh = connected (green); still large = floating (red)." -ForegroundColor DarkGray
Write-Host "      motion rows (FORWARD/TURN/REVERSE) are not judged - just watch the value move." -ForegroundColor DarkGray
Write-Host ""

$acc = ""
try {
    while ($true) {
        try { $acc += $sp.ReadExisting() } catch {}
        while ($acc.Contains("`n")) {
            $idx  = $acc.IndexOf("`n")
            $line = $acc.Substring(0, $idx).Trim()
            $acc  = $acc.Substring($idx + 1)
            if ($line.Length -eq 0) { continue }

            if ($line -match '\[car\]') {
                $name = "?"
                if ($line -match '#\d+\s+([A-Z]+)') { $name = $Matches[1] }
                $r1 = -1; $r2 = -1
                if ($line -match 'I1=.*?\(r(\d+)\)') { $r1 = [int]$Matches[1] }
                if ($line -match 'I2=.*?\(r(\d+)\)') { $r2 = [int]$Matches[1] }

                Write-Host ("{0,-8}" -f $name) -NoNewline -ForegroundColor White

                # I1 (PA27)
                $c1 = "Gray"; $t1 = ""
                if ($name -eq "STOP") {
                    if ($r1 -ge 0 -and $r1 -lt $StopThresh) { $c1 = "Green"; $t1 = " OK" }
                    else { $c1 = "Red"; $t1 = " FLOAT?" }
                }
                Write-Host ("  I1(PA27)=r{0,-5}{1}" -f $r1, $t1) -NoNewline -ForegroundColor $c1

                # I2 (PA26)
                $c2 = "Gray"; $t2 = ""
                if ($name -eq "STOP") {
                    if ($r2 -ge 0 -and $r2 -lt $StopThresh) { $c2 = "Green"; $t2 = " OK" }
                    else { $c2 = "Red"; $t2 = " FLOAT?" }
                }
                Write-Host ("   I2(PA26)=r{0,-5}{1}" -f $r2, $t2) -ForegroundColor $c2
            }
            else {
                Write-Host $line -ForegroundColor DarkGray
            }
        }
        Start-Sleep -Milliseconds 50
    }
} finally {
    $sp.Close()
    Write-Host "`nserial closed." -ForegroundColor Cyan
}
