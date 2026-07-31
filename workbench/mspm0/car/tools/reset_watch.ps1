# reset_watch.ps1 - continuously watch for MCU resets and report the instant one happens.
#
# PURPOSE: find an INTERMITTENT power connection. Three consecutive line-follow runs rebooted at
# PWM ~40% (nowhere near the 60% cap) with speed tracking normally, all while turning - i.e. under
# ordinary load, not a stall and not a battery unable to drive the motors. The run right before the
# car was picked up and turned around had no reboot at all. That points at a connection disturbed
# by handling, and vibration/chassis twist during turns is when it lets go.
#
# HOW TO USE (this is a two-person measurement: you wiggle, this watches):
#   1. car powered, standing still, wheels on the ground, no motion command
#   2. run this
#   3. flex ONE power path at a time, a few seconds each, and say which one you are on:
#        battery leads -> power switch -> buck/regulator input -> board 3V3/5V header
#        -> motor connectors -> encoder connectors
#   4. the instant the MCU resets this prints RESET DETECTED with the elapsed time, so the wire
#      being touched at that moment is the culprit
#
# A reset is detected by the trailing t<ms> uptime in the telemetry going BACKWARDS. That is the
# only way it can decrease, and it is also the only reliable reset signal available: polling `?`
# cannot see a reset because every counter simply restarts and the car looks quietly idle.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [double]$Seconds = 90,
    [int]$Fms = 100
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Output ("OPEN_FAIL: " + $_.Exception.Message); exit 3 }
function Slow([string]$s) { foreach ($c in ($s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }

Slow "z"
Start-Sleep -Milliseconds 400
Slow ("f" + $Fms)
Start-Sleep -Milliseconds 400
$null = $sp.ReadExisting()

Write-Output ("watching $Port for MCU resets, {0:N0}s - wiggle one power path at a time" -f $Seconds)
$t0 = Get-Date
$buf = ''
$prev = -1
$prevSq = -1
$prevC1 = 0
$prevC2 = 0
$n = 0
$hits = New-Object System.Collections.ArrayList
$lastBeat = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    try { $buf += $sp.ReadExisting() } catch {}
    Start-Sleep -Milliseconds 40
    while ($buf -match "`n") {
        $i = $buf.IndexOf("`n")
        $ln = $buf.Substring(0, $i).Trim()
        $buf = $buf.Substring($i + 1)
        if ($ln -notmatch '\| C:(-?\d+),(-?\d+)') { continue }
        $c1 = [int]$Matches[1]; $c2 = [int]$Matches[2]
        if ($ln -notmatch '#(\d+) t(\d+)$') { continue }
        $sq = [int]$Matches[1]; $up = [int]$Matches[2]
        $n++
        # Confirm with the frame SEQUENCE number, not uptime alone: a truncated telemetry line can
        # make 't110975' parse as 't1' and fake a collapse (measured in lap_013749.csv row 119,
        # where the encoder counts carried on smoothly right through the "reset"). #N sits before
        # the uptime field, and a real boot also clears the encoder counts - truncation does neither.
        if ($prevSq -gt 0 -and $sq -lt $prevSq -and
            ([math]::Abs($c1) + [math]::Abs($c2)) -lt ([math]::Abs($prevC1) + [math]::Abs($prevC2)) / 2) {
            $el = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
            [void]$hits.Add($el)
            Write-Output ("  *** RESET DETECTED at t+{0}s   seq {1}->{2}  uptime {3}ms->{4}ms   <== the wire you are touching NOW is the one" -f $el, $prevSq, $sq, $prev, $up)
        }
        $prev = $up; $prevSq = $sq; $prevC1 = $c1; $prevC2 = $c2
    }
    if (((Get-Date) - $lastBeat).TotalSeconds -ge 10) {
        $lastBeat = Get-Date
        Write-Output ("  t+{0,4:N0}s  uptime {1:N1}s  samples {2}  resets {3}" -f ((Get-Date) - $t0).TotalSeconds, ($prev / 1000.0), $n, $hits.Count)
    }
}
$sp.Close()
Write-Output ""
if ($hits.Count -eq 0) {
    Write-Output ("RESULT: NO RESET in {0:N0}s while standing still." -f $Seconds)
    Write-Output "  => the connection does not fail from wiggling alone. Next: repeat this while the"
    Write-Output "     car actually drives (that adds motor current + real vibration), or put a meter"
    Write-Output "     on the battery terminals and on the board 3V3 during a run."
}
else {
    Write-Output ("RESULT: {0} RESET(S) at t+{1}s" -f $hits.Count, ($hits -join ', '))
    Write-Output "  => intermittent connection confirmed. Re-seat/re-solder the path you were on."
}
