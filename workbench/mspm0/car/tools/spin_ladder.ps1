# spin_ladder.ps1 - is the mid-run MCU reset driven by MOTOR CURRENT, or by driving vibration?
#
# WHY THIS TEST: measured so far - 132 s with the car standing still gave ZERO resets (90 s idle
# watch + a 42 s run where the K press was swallowed by the power-on mute window so the car never
# moved), while all 4 runs where the car actually drove reset mid-way, every one of them at
# PWM ~40 % (cap is 60 %) with speed tracking its target, and every one while turning. So the
# trigger needs the motors running - but "motors running" bundles two different causes:
#   current  (battery sag / wiring drop / regulator dropout)  vs  vibration (intermittent contact)
# Spinning in place separates them: it is the HIGHEST current case (both motors opposing) and it is
# also the exact condition the failures happened in, yet the car stays on one spot - no track, no
# space, no repositioning. Sweep the rate low->high:
#   no reset at low rate but reset at high rate  => current-driven, and we learn the safe ceiling
#   resets at every rate incl. the lowest        => not plain current; look at vibration/contact
#   no reset at any rate                         => needs real driving (bumps), i.e. contact again
#
# NOTE ON TIMING: m7 has a 15 s hard cap that cannot be bypassed (CFG_RUN_MS_HARDCAP), so each
# stage runs 12 s and re-enters m7 rather than trying to hold one long run.
param(
    [string]$Port = "COM7",
    [int]$Baud = 115200,
    [string]$Rates = "30,60,90",   # [string] on purpose: across -File a "30,60,90" arrives as ONE string
    [double]$StageS = 12.0,
    [int]$Fms = 100
)
$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { Write-Output ("OPEN_FAIL: " + $_.Exception.Message); exit 3 }
function Slow([string]$s) { foreach ($c in ($s + "`n").ToCharArray()) { $script:sp.Write([string]$c); Start-Sleep -Milliseconds 25 } }
function Drain([int]$ms) { $o = ''; $t = Get-Date; while (((Get-Date) - $t).TotalMilliseconds -lt $ms) { try { $o += $script:sp.ReadExisting() } catch {}; Start-Sleep -Milliseconds 30 }; return $o }

Slow "z"; [void](Drain 500)
Slow ("f" + $Fms); [void](Drain 400)
Write-Output ("==== spin_ladder $Port  " + (Get-Date -Format "HH:mm:ss") + " ====")
Write-Output ("rate  samples  peak|rpm|  peak|PWM|  peak|I|mA   resets   verdict")

$summary = New-Object System.Collections.ArrayList
foreach ($rTxt in ($Rates -split ',')) {
    $r = [int]$rTxt.Trim(); if ($r -le 0) { continue }
    Slow "z";  [void](Drain 400)
    Slow "m7"; [void](Drain 300)
    Slow "v0"; [void](Drain 250)
    Slow ("r" + $r)
    $buf = ''; $t0 = Get-Date
    $n = 0; $rst = 0; $pRpm = 0; $pPwm = 0; $pI = 0
    $pSq = -1; $pA = 0; $pB = 0
    while (((Get-Date) - $t0).TotalSeconds -lt $StageS) {
        try { $buf += $sp.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 40
        while ($buf -match "`n") {
            $i = $buf.IndexOf("`n"); $ln = $buf.Substring(0, $i).Trim(); $buf = $buf.Substring($i + 1)
            if ($ln -notmatch '\| C:(-?\d+),(-?\d+)') { continue }
            $c1 = [int]$Matches[1]; $c2 = [int]$Matches[2]
            if ($ln -notmatch '#(\d+) t\d+$') { continue }
            $sq = [int]$Matches[1]
            $n++
            if ($ln -match '\| V:(-?\d+),(-?\d+)') { $v = [math]::Max([math]::Abs([int]$Matches[1]), [math]::Abs([int]$Matches[2])); if ($v -gt $pRpm) { $pRpm = $v } }
            if ($ln -match '\| PWM:(-?\d+),(-?\d+)') { $p = [math]::Max([math]::Abs([int]$Matches[1]), [math]::Abs([int]$Matches[2])); if ($p -gt $pPwm) { $pPwm = $p } }
            if ($ln -match '\| I:(-?\d+),(-?\d+)') { $q = [math]::Max([math]::Abs([int]$Matches[1]), [math]::Abs([int]$Matches[2])); if ($q -gt $pI) { $pI = $q } }
            # reset = frame sequence restarts AND encoder counts collapse (truncated telemetry lines
            # can fake an uptime drop on its own - measured in lap_013749.csv row 119)
            if ($pSq -gt 0 -and $sq -lt $pSq -and ([math]::Abs($c1) + [math]::Abs($c2)) -lt ([math]::Abs($pA) + [math]::Abs($pB)) / 2) { $rst++ }
            $pSq = $sq; $pA = $c1; $pB = $c2
        }
    }
    Slow "z"; [void](Drain 500)
    $v = if ($rst -gt 0) { "RESET" } elseif ($n -lt 20) { "NO DATA" } else { "ok" }
    Write-Output ("r{0,-4} {1,7}  {2,8}  {3,9}%  {4,9}  {5,7}   {6}" -f $r, $n, $pRpm, $pPwm, $pI, $rst, $v)
    [void]$summary.Add([pscustomobject]@{ r = $r; rst = $rst; pwm = $pPwm; rpm = $pRpm; n = $n })
}
$sp.Close()

Write-Output ""
$bad = @($summary | Where-Object { $_.rst -gt 0 })
$good = @($summary | Where-Object { $_.rst -eq 0 -and $_.n -ge 20 })
if ($bad.Count -eq 0) {
    Write-Output "RESULT: no reset at ANY spin rate."
    Write-Output "  => plain motor current is NOT the trigger. The failures needed the car to be"
    Write-Output "     actually driving over the floor, which adds bumps and chassis flex on top of"
    Write-Output "     current => back to an intermittent contact. Next: wiggle each power path"
    Write-Output "     (tools/reset_watch.ps1) WHILE spinning, or meter the battery during a real run."
}
elseif ($good.Count -gt 0) {
    Write-Output ("RESULT: current-driven. Highest clean rate r{0} (PWM {1}%), first failing r{2} (PWM {3}%)." -f `
        ($good[-1].r), ($good[-1].pwm), ($bad[0].r), ($bad[0].pwm))
    Write-Output "  => it is an electrical limit, not the line follower. Fix the supply (battery with"
    Write-Output "     lower internal resistance / thicker leads / bulk cap at the regulator) or keep"
    Write-Output "     the drive below that PWM."
}
else {
    Write-Output "RESULT: reset even at the LOWEST rate tested."
    Write-Output "  => the threshold is below this - retry with a lower rate, or the supply is failing"
    Write-Output "     the moment the motors draw anything at all."
}
