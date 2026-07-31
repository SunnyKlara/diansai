# duty_soak.ps1 - hold BOTH motors at a fixed OPEN-LOOP duty for as long as you want, and record
#                  real current + speed + any MCU reset.
#
# WHY THIS EXISTS (three separate reasons, all learned the hard way tonight)
#
# 1) THE `I:` FIELD IS ONLY LIVE IN m5/m4.  car.c refreshes the telemetry current field only in
#    m5 (DUAL direct drive) and m4 (CURRENT loop); in m7/m11/IDLE it holds a stale value, normally
#    0,0.  Every "peak |I| = 0 mA" seen during line-following runs was therefore MEANINGLESS - we
#    had no current data at all while trying to decide whether current spikes were browning out the
#    MCU.  This script forces m5, so the numbers are real.
#
# 2) CLOSED LOOP HIDES THE QUESTION.  In m7/m11 the speed loop decides the duty, so "duty" is an
#    output, not an input - you cannot ask "can the supply hold 45 %?" because the loop keeps moving
#    it.  Open loop makes duty the independent variable, which is what a supply test needs.
#
# 3) THE RUN GATES STOP YOU AT 15 s.  CFG_RUN_MS_HARDCAP is 15 s and cannot be bypassed (only m11
#    gets the longer CFG_RUN_MS_HARDCAP_LINE).  A naive "send x45 y45 and wait" therefore dies at
#    15 s and looks like a fault.  This script re-arms the command every RearmS seconds so the soak
#    really continues, and says so in the output.
#
# CLAMPING: x/y are clamped to +/-PWM_CAP in firmware (config.h). PWM_CAP is currently 45, so
# asking for 50 silently gives you 45.  The script reads back the achieved PWM and reports both,
# so a clamp can never be mistaken for a supply problem.
#
# RESET DETECTION: frame sequence #N restarting AND encoder counts collapsing - uptime alone gives
# false positives when a telemetry line gets truncated (measured: lap_013749.csv row 119).
#
# SAFETY: WHEELS SHOULD BE OFF THE GROUND unless you intend the car to drive away. Sends `z` at the
# end. `z` does not clear the task state machine, only motion.
#
# Usage:
#   powershell -File tools\duty_soak.ps1 -Port COM7 -Duty 50 -Seconds 60
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [int]$Duty = 45,
    [double]$Seconds = 60,
    # MUST be shorter than the SILENCE timeout of the mode, not just the hard cap.
    # Measured the hard way: CFG_RUN_MS_DUAL is 4000 ms, so an 8 s re-arm let m5 time out and fall
    # back to IDLE - after which x/y only write the variables and drive nothing, giving a perfectly
    # quiet "PWM 0,0 / rpm 0,0" that looks exactly like a dead motor. Any command refreshes the
    # gate, so 2.5 s keeps it alive with margin.
    [double]$RearmS = 2.5,
    [int]$Fms = 100,
    [string]$OutDir = "_logs\track"
)
$root = Split-Path -Parent $PSScriptRoot
$dOut = if ([IO.Path]::IsPathRooted($OutDir)) { $OutDir } else { Join-Path $root $OutDir }
if (-not (Test-Path $dOut)) { New-Item -ItemType Directory -Path $dOut -Force | Out-Null }
$stamp = Get-Date -Format "HHmmss"
$csv = Join-Path $dOut ("soak_$stamp.csv")
$raw = Join-Path $dOut ("soak_${stamp}_raw.txt")

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Output ("OPEN_FAIL: " + $_.Exception.Message); exit 3 }
function Slow([string]$s) { foreach ($c in ($s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Drain([int]$ms) { $o = ''; $t = Get-Date; while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $o += $script:sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 30 }; return $o }

Write-Output ("==== duty_soak $Port  duty=$Duty  {0:N0}s  " -f $Seconds + (Get-Date -Format "HH:mm:ss") + " ====")
Write-Output "  WHEELS OFF THE GROUND unless you want the car to drive away."
Slow "z"; [void](Drain 400)
Slow ("f" + $Fms); [void](Drain 400)
Slow "m5"; [void](Drain 400)     # DUAL direct drive - the only mode where I: is live
Slow ("x" + $Duty)
Slow ("y" + $Duty)

$t0 = Get-Date
$nextArm = (Get-Date).AddSeconds($RearmS)
$buf = ''
$rows = New-Object System.Collections.ArrayList
$rawLines = New-Object System.Collections.ArrayList
$pSq = -1; $pA = 0; $pB = 0
$resets = New-Object System.Collections.ArrayList
$rearms = 0
$remodes = 0
$modeNow = 'DUAL'
$lastBeat = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    try { $buf += $sp.ReadExisting() } catch {}
    Start-Sleep -Milliseconds 40
    if ((Get-Date) -ge $nextArm) {
        $nextArm = (Get-Date).AddSeconds($RearmS)
        # if the gate already fired (or an MCU reset dropped us to IDLE) re-enter the mode first,
        # otherwise x/y silently write variables that drive nothing
        if ($modeNow -ne 'DUAL') { Slow "m5"; $remodes++ }
        Slow ("x" + $Duty); Slow ("y" + $Duty)
        $rearms++
    }
    while ($buf -match "`n") {
        $i = $buf.IndexOf("`n"); $ln = $buf.Substring(0, $i).Trim(); $buf = $buf.Substring($i + 1)
        $el = [math]::Round(((Get-Date) - $t0).TotalSeconds, 2)
        if ($ln) { [void]$rawLines.Add(("{0,7} {1}" -f $el, $ln)) }
        if ($ln -match '^\[ctl\] ([A-Z]+) tgt=') { $modeNow = $Matches[1] }
        if ($ln -notmatch '\| C:(-?\d+),(-?\d+)') { continue }
        $c1 = [int]$Matches[1]; $c2 = [int]$Matches[2]
        if ($ln -notmatch '#(\d+) t(\d+)$') { continue }
        $sq = [int]$Matches[1]; $up = [int]$Matches[2]
        $v1 = 0; $v2 = 0; $p1 = 0; $p2 = 0; $i1 = 0; $i2 = 0
        if ($ln -match '\| V:(-?\d+),(-?\d+)') { $v1 = [int]$Matches[1]; $v2 = [int]$Matches[2] }
        if ($ln -match '\| PWM:(-?\d+),(-?\d+)') { $p1 = [int]$Matches[1]; $p2 = [int]$Matches[2] }
        if ($ln -match '\| I:(-?\d+),(-?\d+)') { $i1 = [int]$Matches[1]; $i2 = [int]$Matches[2] }
        [void]$rows.Add([pscustomobject]@{ el = $el; up = $up; sq = $sq; c1 = $c1; c2 = $c2; v1 = $v1; v2 = $v2; p1 = $p1; p2 = $p2; i1 = $i1; i2 = $i2 })
        if ($pSq -gt 0 -and $sq -lt $pSq -and ([math]::Abs($c1) + [math]::Abs($c2)) -lt ([math]::Abs($pA) + [math]::Abs($pB)) / 2) {
            [void]$resets.Add($el)
            Write-Output ("  *** MCU RESET at t+{0}s   seq {1}->{2} ***" -f $el, $pSq, $sq)
        }
        $pSq = $sq; $pA = $c1; $pB = $c2
    }
    if (((Get-Date) - $lastBeat).TotalSeconds -ge 10 -and $rows.Count) {
        $lastBeat = Get-Date
        $r = $rows[$rows.Count - 1]
        Write-Output ("  t+{0,4:N0}s  rpm {1,4},{2,-4} PWM {3,3},{4,-3} I {5,5},{6,-5} mA  up {7:N0}s  resets {8}" -f `
            ((Get-Date) - $t0).TotalSeconds, $r.v1, $r.v2, $r.p1, $r.p2, $r.i1, $r.i2, ($r.up / 1000.0), $resets.Count)
    }
}
Slow "z"; [void](Drain 400)
$sp.Close()
$rows | Export-Csv -NoTypeInformation -Encoding UTF8 -Path $csv
[IO.File]::WriteAllLines($raw, $rawLines)

Write-Output ""
Write-Output "---- result ----"
if ($rows.Count -lt 5) { Write-Output "  TOO FEW SAMPLES - link down?"; exit 3 }
$pwmMax = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.p1), [math]::Abs($_.p2)) } | Measure-Object -Maximum).Maximum
$iMax = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.i1), [math]::Abs($_.i2)) } | Measure-Object -Maximum).Maximum
$iAvg = ($rows | ForEach-Object { ([math]::Abs($_.i1) + [math]::Abs($_.i2)) / 2 } | Measure-Object -Average).Average
$vMax = ($rows | ForEach-Object { [math]::Max([math]::Abs($_.v1), [math]::Abs($_.v2)) } | Measure-Object -Maximum).Maximum
Write-Output ("  asked duty {0}%   achieved PWM {1}%{2}" -f $Duty, $pwmMax, $(if ($pwmMax -lt $Duty) { "   <== CLAMPED by PWM_CAP in config.h, not a supply problem" } else { "" }))
Write-Output ("  peak |rpm| {0}   current avg {1:N0} mA  peak {2} mA" -f $vMax, $iAvg, $iMax)
$nDrive = @($rows | Where-Object { [math]::Abs($_.p1) -gt 0 -or [math]::Abs($_.p2) -gt 0 }).Count
Write-Output ("  samples {0} ({1} of them actually driving)   re-arms {2}   mode re-entries {3}   duration {4:N1}s" -f `
    $rows.Count, $nDrive, $rearms, $remodes, $rows[$rows.Count - 1].el)
if ($nDrive -lt ($rows.Count / 4)) {
    Write-Output "  ⚠ most samples had PWM 0 - the mode kept timing out. Lower -RearmS below the"
    Write-Output "    mode's CFG_RUN_MS_* silence timeout (m5 DUAL = 4000 ms)."
}
$rstLines = @($rawLines | Where-Object { $_ -match '\[rst\]' })
if ($rstLines.Count) { Write-Output "  firmware reset verdict:"; foreach ($r in $rstLines) { Write-Output ("    " + $r) } }
Write-Output ""
if ($resets.Count -eq 0) {
    Write-Output ("RESULT: PASS - held {0}% for {1:N0}s with NO reset." -f $pwmMax, $Seconds)
    Write-Output "  => open-loop duty alone does not brown out the MCU at this level. If driving on"
    Write-Output "     the ground still resets, the extra factor is load current (wheels loaded) or"
    Write-Output "     vibration, not duty as such. Repeat on the ground at the same duty to split them."
}
else {
    Write-Output ("RESULT: RESET x{0} at t+{1}s while holding {2}%." -f $resets.Count, ($resets -join ', '), $pwmMax)
    Write-Output "  => reproduced WITHOUT any control loop and with the wheels unloaded, so it is not"
    Write-Output "     the line follower and not mechanical drag. Read the [rst] verdict above; if it"
    Write-Output "     says BOR_SUPPLY the supply cannot hold this duty - fix the supply."
}
Write-Output ("csv: " + $csv)
Write-Output ("raw: " + $raw)
