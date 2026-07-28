# motor_probe.ps1 - per-motor "does it actually push?" check: current vs speed at the same duty.
#
# WHY: in closed loop (m7) all you see is "PWM saturated at the cap while rpm stays 0".
# That cannot tell apart:
#   (1) electrically dead  - loose connector / open winding / bad driver channel
#   (2) mechanically stalled - wheel jammed, or load far above what the duty can drive
# One measurement splits them, because current only flows in case (2):
#   I ~ 0   and rpm ~ 0  -> ELECTRICALLY DEAD (no current at all, so no torque was ever produced)
#   I large and rpm ~ 0  -> MECHANICALLY STALLED (current went in, shaft would not turn)
#   I small and rpm high -> FREE SPINNING, no load (wheel off the ground or slipping)
#
# IMPORTANT: the I: field in the telemetry is refreshed ONLY in m5 (DUAL) and m4 (CURRENT)
# modes - see car.c. In m7/IDLE the I: values you see are stale (usually 0,0), so never draw
# conclusions from them. That is why this script forces m5 + x/y direct drive.
#
# SAFETY: the wheels DO spin. Put the car on a stand or on clear floor.
# Each step is <=1.2 s at <=30% duty. m5 silence timeout is CFG_RUN_MS_DUAL=4000 ms and every
# step sends a command (which refreshes it); CFG_RUN_MS_HARDCAP=15000 ms is the backstop.
# The port is always closed with a 'z' (stop) even if the script throws.
#
# Keep this file ASCII-only: Windows PowerShell 5.1 reads .ps1 as ANSI, so non-ASCII comments
# in a no-BOM file get mangled into syntax errors.
#
# Usage: powershell -File motor_probe.ps1 -Port COM4 [-Duty1 15] [-Duty2 30]

param(
    [string]$Port    = 'COM4',
    [int]$Baud       = 115200,
    [int]$Duty1      = 15,     # just above the ~10% measured deadzone
    [int]$Duty2      = 30,
    [double]$StepSec = 1.2
)

$ErrorActionPreference = 'Continue'

function New-Step([string[]]$cmds, [string]$tag, [double]$sec) {
    [pscustomobject]@{ cmds = $cmds; tag = $tag; sec = $sec }
}

$steps = @(
    (New-Step @('z', 'm5', 'x0', 'y0') 'm5 idle ' 0.8),
    (New-Step @("x$Duty1")             "M1 @$Duty1%  " $StepSec),
    (New-Step @("x$Duty2")             "M1 @$Duty2%  " $StepSec),
    (New-Step @('x0', "y$Duty1")       "M2 @$Duty1%  " $StepSec),
    (New-Step @("y$Duty2")             "M2 @$Duty2%  " $StepSec),
    (New-Step @('x0', 'y0')            'both 0  ' 0.6)
)

Write-Host ("================ motor_probe  " + (Get-Date -Format 'HH:mm:ss') + " ================")
Write-Host ("port $Port @ $Baud   duties ${Duty1}% / ${Duty2}%   step ${StepSec}s")
Write-Host "WHEELS MUST BE FREE TO SPIN (stand, or clear floor)."
Write-Host ""

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200
$results = New-Object System.Collections.ArrayList

try {
    $sp.Open()
    Start-Sleep -Milliseconds 400
    $sp.DiscardInBuffer()

    foreach ($st in $steps) {
        foreach ($c in $st.cmds) {
            foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
        }
        $sp.DiscardInBuffer()

        $buf = ''
        $acc = New-Object System.Collections.ArrayList
        $sw  = [System.Diagnostics.Stopwatch]::StartNew()
        while ($sw.Elapsed.TotalSeconds -lt $st.sec) {
            $s = ''
            try { $s = $sp.ReadExisting() } catch { }
            if ($s) { $buf += $s }
            while ($buf.Contains("`n")) {
                $i   = $buf.IndexOf("`n")
                $ln  = $buf.Substring(0, $i)
                $buf = $buf.Substring($i + 1)
                if ($ln -match 'I:(-?\d+),(-?\d+).*V:(-?\d+),(-?\d+).*PWM:(-?\d+),(-?\d+)') {
                    [void]$acc.Add([pscustomobject]@{
                        i1 = [int]$Matches[1]; i2 = [int]$Matches[2]
                        v1 = [int]$Matches[3]; v2 = [int]$Matches[4]
                        p1 = [int]$Matches[5]; p2 = [int]$Matches[6]
                    })
                }
            }
            Start-Sleep -Milliseconds 10
        }

        if ($acc.Count -eq 0) {
            Write-Host ("  {0} n=  0   <- no parsable telemetry in this step (link loss?)" -f $st.tag)
            continue
        }
        $last = $acc[$acc.Count - 1]
        $o = [pscustomobject]@{
            step  = $st.tag
            n     = $acc.Count
            pwm   = ("{0},{1}" -f $last.p1, $last.p2)
            i1avg = [int](($acc | Measure-Object i1 -Average).Average)
            i1max = ($acc | Measure-Object i1 -Maximum).Maximum
            i2avg = [int](($acc | Measure-Object i2 -Average).Average)
            i2max = ($acc | Measure-Object i2 -Maximum).Maximum
            v1max = ($acc | ForEach-Object { [Math]::Abs($_.v1) } | Measure-Object -Maximum).Maximum
            v2max = ($acc | ForEach-Object { [Math]::Abs($_.v2) } | Measure-Object -Maximum).Maximum
        }
        [void]$results.Add($o)
        Write-Host ("  {0} n={1,3}  PWM {2,-7}  I1 {3,5}/{4,5} mA  I2 {5,5}/{6,5} mA  |V1| {7,4} rpm  |V2| {8,4} rpm" -f `
            $o.step, $o.n, $o.pwm, $o.i1avg, $o.i1max, $o.i2avg, $o.i2max, $o.v1max, $o.v2max)
    }
}
catch {
    Write-Host ("EXCEPTION: " + $_.Exception.Message)
}
finally {
    if ($sp.IsOpen) {
        foreach ($ch in "z`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
        Start-Sleep -Milliseconds 250
        $sp.Close()
    }
    $sp.Dispose()
}

Write-Host ""
Write-Host "---- verdict ----"
$m1 = @($results | Where-Object { $_.step -like 'M1*' })
$m2 = @($results | Where-Object { $_.step -like 'M2*' })
if ($m1.Count -eq 0 -or $m2.Count -eq 0) {
    Write-Host "RESULT: INCONCLUSIVE - did not capture both sides (too much link loss, or commands never landed)"
    exit 2
}

# Judge on the AVERAGE current, never the max, and compare it against the OTHER channel's
# no-load average instead of a fixed threshold.
# WHY (this verdict was wrong four runs in a row on 2026-07-27): the current channels carry
# 40..250 mA noise spikes even at 0% duty, so `I max` says "hundreds of mA" for a motor that is
# barely drawing anything - which made the script shout "MECHANICALLY STALLED" at a left motor
# pulling 97 mA average at 60% duty while the healthy right motor pulled 72 mA spinning freely.
# A real stall draws MUCH more than no-load (locked rotor is several times the free-run current);
# a channel that draws the SAME as no-load and still will not turn is not stalled at all - the
# torque never reached the shaft (intermittent joint, open winding segment, dead brush).
$i1 = ($m1 | Measure-Object i1avg -Maximum).Maximum
$p1 = ($m1 | Measure-Object i1max -Maximum).Maximum
$v1 = ($m1 | Measure-Object v1max -Maximum).Maximum
$i2 = ($m2 | Measure-Object i2avg -Maximum).Maximum
$p2 = ($m2 | Measure-Object i2max -Maximum).Maximum
$v2 = ($m2 | Measure-Object v2max -Maximum).Maximum

# no-load reference = the average current of whichever channel actually spun
$ref = 0
if ($v1 -ge 20 -and $v2 -ge 20) { $ref = [Math]::Min($i1, $i2) }
elseif ($v1 -ge 20) { $ref = $i1 }
elseif ($v2 -ge 20) { $ref = $i2 }

function Say([string]$who, [int]$iavg, [int]$imax, [int]$v, [int]$reference) {
    if ($v -ge 20) {
        $t = "spins ($v rpm peak) -> this channel drives fine"
    } elseif ($iavg -le 15) {
        $t = "** no spin AND no current at all -> OPEN CIRCUIT: connector off / broken lead / dead driver channel"
    } elseif ($reference -gt 0 -and $iavg -lt 3 * $reference) {
        $t = "** no spin, yet current is only ${iavg}mA = same order as the healthy side's no-load ${reference}mA"
        $t += " -> NOT a stall. Torque is not reaching the shaft: intermittent joint / bad motor / open winding."
        $t += " Localise with the exchange test (swap the two motor plugs at the driver, keep encoders put)."
    } elseif ($reference -gt 0) {
        $t = "** no spin and current is ${iavg}mA, far above the ${reference}mA no-load -> MECHANICALLY STALLED: turn the wheel by hand"
    } else {
        $t = "** no spin, current ${iavg}mA, and NO healthy channel to compare against -> INCONCLUSIVE"
    }
    Write-Host ("  {0}: I avg {1,5} mA (max {2,5}, noisy)   |V| max {3,4} rpm   -> {4}" -f $who, $iavg, $imax, $v, $t)
}
Say 'M1 (left) ' $i1 $p1 $v1 $ref
Say 'M2 (right)' $i2 $p2 $v2 $ref

if ($v1 -ge 20 -and $v2 -ge 20) {
    $lo = [Math]::Min($v1, $v2); $hi = [Math]::Max($v1, $v2)
    if ($hi -gt 3 * $lo) {
        Write-Host ("RESULT: FAIL - both turn but speeds differ {0:N1}x at the same duty ({1} vs {2} rpm) - one side is weak" -f ($hi / [double]$lo), $v1, $v2)
        exit 1
    }
    Write-Host "RESULT: PASS - both channels turn"
    exit 0
}
Write-Host "RESULT: FAIL - one channel does not push; follow the -> above"
exit 1
