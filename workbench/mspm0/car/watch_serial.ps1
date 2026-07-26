# watch_serial.ps1 - live colour-coded telemetry monitor for the car firmware.
#
# 2026-07-27 REWRITTEN. The old version parsed "[car] ... I1=..(rNNN)", a format car.c stopped
# emitting long ago -> every telemetry line fell into the else-branch and printed dark grey, and
# the STOP colour rule NEVER fired. The tool looked alive while being completely blind. That is
# the "tool/firmware format drift = silent failure" pitfall in .kiro/steering/knowledge/跨题坑库.md.
# To make that class of failure impossible to miss again, this version:
#   * parses the CURRENT car.c line   [ctl] <MODE> tgt=<t> | I:a,b | V:a,b | PWM:a,b | C:a,b
#                                     [ | D:dv,dw ] [ | Y:<yaw*10> W:<w*100> ]
#   * counts lines it could NOT parse and prints a LOUD warning when they dominate, instead of
#     quietly greying them out. If the firmware format changes again you will see it in seconds.
#
# Works over the ESP-01S wireless bridge too - just point -Port at the PC-side module (e.g. COM6,
# after esp_pc_up.ps1). Over a lossy link some lines arrive truncated: those are counted as
# "garbled" separately from "unknown format", so link quality and format drift stay separable.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File watch_serial.ps1 -Port COM30
#   powershell -NoProfile -ExecutionPolicy Bypass -File watch_serial.ps1 -Port COM6 -IdleThresh 30
# Quit: Ctrl+C
# NOTE: a COM port can be opened by only ONE program at a time - close other serial tools first.
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port       = "COM30",
    [int]$Baud          = 115200,
    [int]$IdleThresh    = 60,     # mA: in IDLE the motors are off, so |I| above this is suspicious
    [int]$PwmCap        = 60,     # % : config.h PWM_CAP - hitting it means the loop is saturated
    [int]$StatsEverySec = 10      # how often to print the parse-health summary
)

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200
$sp.DtrEnable   = $false
$sp.RtsEnable   = $false
try {
    $sp.Open()
} catch {
    Write-Host "[X] open $Port failed: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "    A COM port is exclusive - close other serial tools/scripts first." -ForegroundColor DarkYellow
    exit 1
}
Start-Sleep -Milliseconds 200
try { $sp.DiscardInBuffer() } catch {}

Write-Host "===== live telemetry monitor  $Port @ $Baud   (Ctrl+C to quit) =====" -ForegroundColor Cyan
Write-Host "expects: [ctl] <MODE> tgt=<t> | I:a,b | V:a,b | PWM:a,b | C:a,b [| D:..] [| Y:.. W:..]" -ForegroundColor DarkGray
Write-Host "colours: IDLE row with |I| >= $IdleThresh mA = RED (motor should be off)" -ForegroundColor DarkGray
Write-Host "         |PWM| >= $PwmCap % = YELLOW (saturated - loop cannot push harder)" -ForegroundColor DarkGray
Write-Host "         parse-health summary every $StatsEverySec s - if 'telemetry' stays 0 the format drifted" -ForegroundColor DarkGray
Write-Host ""

# [ctl] SPEED tgt=500 | I:12,15 | V:498,496 | PWM:35,36 | C:1234,1230 | D:0,0 | Y:123 W:45
$reTelem = '^\[ctl\]\s+(\S+)\s+tgt=(-?\d+)\s*\|\s*I:(-?\d+),(-?\d+)\s*\|\s*V:(-?\d+),(-?\d+)\s*\|\s*PWM:(-?\d+),(-?\d+)\s*\|\s*C:(-?\d+),(-?\d+)'

$nTelem = 0; $nStatus = 0; $nGarbled = 0; $nUnknown = 0
$lastStats = [System.Diagnostics.Stopwatch]::StartNew()

$acc = ""
try {
    while ($true) {
        try { $acc += $sp.ReadExisting() } catch {}
        while ($acc.Contains("`n")) {
            $idx  = $acc.IndexOf("`n")
            $line = $acc.Substring(0, $idx).Trim()
            $acc  = $acc.Substring($idx + 1)
            if ($line.Length -eq 0) { continue }

            $m = [regex]::Match($line, $reTelem)
            if ($m.Success) {
                $nTelem++
                $mode = $m.Groups[1].Value
                $tgt  = [int]$m.Groups[2].Value
                $i1   = [int]$m.Groups[3].Value; $i2 = [int]$m.Groups[4].Value
                $v1   = [int]$m.Groups[5].Value; $v2 = [int]$m.Groups[6].Value
                $p1   = [int]$m.Groups[7].Value; $p2 = [int]$m.Groups[8].Value
                $c1   = [int]$m.Groups[9].Value; $c2 = [int]$m.Groups[10].Value

                Write-Host ("{0,-6} tgt{1,6}" -f $mode, $tgt) -NoNewline -ForegroundColor White

                # current: only judged in IDLE, where the motors are supposed to be off
                $ci = "Gray"; $ti = ""
                if ($mode -eq "IDLE") {
                    if ([Math]::Max([Math]::Abs($i1), [Math]::Abs($i2)) -ge $IdleThresh) { $ci = "Red"; $ti = " <-IDLE but current!" }
                    else { $ci = "Green" }
                }
                Write-Host ("  I{0,6},{1,6}{2}" -f $i1, $i2, $ti) -NoNewline -ForegroundColor $ci

                Write-Host ("  V{0,6},{1,6}" -f $v1, $v2) -NoNewline -ForegroundColor Cyan

                # PWM: saturation is the single most useful thing to see live
                $cp = "Gray"; $tp = ""
                if ([Math]::Max([Math]::Abs($p1), [Math]::Abs($p2)) -ge $PwmCap) { $cp = "Yellow"; $tp = " SAT" }
                Write-Host ("  PWM{0,5},{1,5}{2}" -f $p1, $p2, $tp) -NoNewline -ForegroundColor $cp

                Write-Host ("  C{0,9},{1,9}" -f $c1, $c2) -NoNewline -ForegroundColor DarkGray

                # optional tail fields (added later than the core line - absent on older firmware)
                $tail = ""
                $my = [regex]::Match($line, 'Y:(-?\d+)\s+W:(-?\d+)')
                if ($my.Success) { $tail += ("  yaw{0,8:N1}deg w{1,7:N2}dps" -f ([double]$my.Groups[1].Value / 10.0), ([double]$my.Groups[2].Value / 100.0)) }
                $md = [regex]::Match($line, 'D:(-?\d+),(-?\d+)')
                if ($md.Success) { $tail += ("  D{0},{1}" -f $md.Groups[1].Value, $md.Groups[2].Value) }
                if ($tail.Length -gt 0) { Write-Host $tail -ForegroundColor Magenta } else { Write-Host "" }
            }
            elseif ($line -match '^\[ctl\]\s+mode=') {
                # the print_status() reply to a command: shows gains and the rej= gate counter
                $nStatus++
                $col = "DarkCyan"
                $mr = [regex]::Match($line, 'rej=(\d+)')
                if ($mr.Success -and [int]$mr.Groups[1].Value -gt 0) { $col = "DarkYellow" }   # gate rejected something
                Write-Host $line -ForegroundColor $col
            }
            elseif ($line -match '^\[imu\]|^\[probe\]|^boot|^build') {
                Write-Host $line -ForegroundColor Green
            }
            elseif ($line -match '\[ctl\]|I:|V:|PWM:') {
                # recognisable fragment but did not match in full => almost certainly a truncated
                # line (lossy wireless link), not a format change. Kept separate on purpose.
                $nGarbled++
                Write-Host ("~ " + $line) -ForegroundColor DarkYellow
            }
            else {
                $nUnknown++
                Write-Host $line -ForegroundColor DarkGray
            }
        }

        if ($lastStats.Elapsed.TotalSeconds -ge $StatsEverySec) {
            $lastStats.Restart()
            $col = "DarkGray"
            $note = ""
            if ($nTelem -eq 0) {
                $col = "Red"
                $note = "  <-- ZERO telemetry parsed! firmware format drifted, or wrong port/baud. FIX THE REGEX."
            } elseif ($nGarbled -gt ($nTelem * 0.1)) {
                $col = "DarkYellow"
                $note = "  <-- >10% truncated lines: link quality (check RSSI/distance), not format"
            }
            Write-Host ("---- parsed: telemetry=$nTelem status=$nStatus truncated=$nGarbled other=$nUnknown ----" + $note) -ForegroundColor $col
        }

        Start-Sleep -Milliseconds 50
    }
} finally {
    try { $sp.Close() } catch {}
    Write-Host ""
    Write-Host "serial closed.  totals: telemetry=$nTelem status=$nStatus truncated=$nGarbled other=$nUnknown" -ForegroundColor Cyan
}
