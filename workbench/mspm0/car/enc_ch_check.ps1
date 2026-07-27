# enc_ch_check.ps1 - is one channel of a quadrature encoder pair dead?
#
# THE QUESTION IT ANSWERS
#   A wheel reads ~0 rpm while its motor draws a normal no-load current, i.e. the current says
#   "spinning" and the encoder says "stopped". Those cannot both be true.
#
# THE SIGNATURE (this is the whole point)
#   encoder.c decodes 4x quadrature from sampled A/B levels. Drive the motor in ONE direction:
#     * both channels healthy -> the count marches MONOTONICALLY in one direction
#     * one channel stuck (open wire / no pull-up / dead hall) -> the decoder only ever sees the
#       other channel toggle, so the state machine walks forward then back: 00 -> 10 -> 00 ...
#       The ups and downs CANCEL, net delta ~0, and the reported rpm is a few counts of jitter.
#   So: comparing "sum of upward steps" against "sum of downward steps" separates
#   "genuinely barely turning" (all steps one way) from "one encoder channel is dead"
#   (roughly equal both ways) - without opening anything or needing a scope.
#
# The other motor is driven too, as a control: a healthy channel must come out monotonic, which
# proves the analysis itself is sound on this hardware rather than just producing a pretty number.
#
# SAFETY: open loop m5 at a modest duty, one motor at a time, <=Sec each, always ends with `z`.
# Wheels should be free (this is a bench test).
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#
# Usage: powershell -File enc_ch_check.ps1 -Port COM4 [-Duty 30] [-Sec 2.5]

param(
    [string]$Port = 'COM4',
    [int]$Baud    = 115200,
    [int]$Duty    = 30,
    [double]$Sec  = 2.5
)

$ErrorActionPreference = 'Continue'
$script:rxbuf = ''

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200

function Snd([string]$c) {
    foreach ($ch in ($c + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
}

# Complete lines only - a chunk from ReadExisting almost always ends mid-line, and judging a
# partial line would invent both missing samples and fake counts.
function Grab([System.Collections.ArrayList]$sink, [double]$sec) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalSeconds -lt $sec) {
        $s = ''
        try { $s = $sp.ReadExisting() } catch { }
        if ($s) { $script:rxbuf += $s }
        while ($script:rxbuf.Contains("`n")) {
            $i = $script:rxbuf.IndexOf("`n")
            $ln = $script:rxbuf.Substring(0, $i)
            $script:rxbuf = $script:rxbuf.Substring($i + 1)
            if ($ln -match 'C:(-?\d+),(-?\d+)') {
                [void]$sink.Add([pscustomobject]@{ c1 = [int]$Matches[1]; c2 = [int]$Matches[2] })
            }
        }
        Start-Sleep -Milliseconds 5
    }
}

function Analyse([System.Collections.ArrayList]$rows, [string]$which, [string]$label) {
    $n = $rows.Count
    if ($n -lt 10) { Write-Host ("  {0}: only {1} samples - unusable" -f $label, $n); return $null }
    $up = 0; $dn = 0; $nUp = 0; $nDn = 0
    for ($i = 1; $i -lt $n; $i++) {
        $d = if ($which -eq 'c1') { $rows[$i].c1 - $rows[$i - 1].c1 } else { $rows[$i].c2 - $rows[$i - 1].c2 }
        if ($d -gt 0) { $up += $d; $nUp++ } elseif ($d -lt 0) { $dn += -$d; $nDn++ }
    }
    $net = if ($which -eq 'c1') { $rows[$n - 1].c1 - $rows[0].c1 } else { $rows[$n - 1].c2 - $rows[0].c2 }
    $tot = $up + $dn
    $bal = if ($tot -gt 0) { 100.0 * [Math]::Min($up, $dn) / $tot } else { 0.0 }
    Write-Host ("  {0}: samples {1,3}  net {2,7}  up {3,7} ({4} steps)  down {5,7} ({6} steps)  cancelling {7,5:N1} %" -f `
        $label, $n, $net, $up, $nUp, $dn, $nDn, $bal)
    return [pscustomobject]@{ label = $label; n = $n; net = $net; up = $up; dn = $dn; total = $tot; balance = $bal }
}

Write-Host ("================ enc_ch_check  " + (Get-Date -Format 'HH:mm:ss') + " ================")
Write-Host ("port $Port   open loop m5 @ ${Duty}%   ${Sec}s per motor   WHEELS FREE")
Write-Host ""

$r1 = New-Object System.Collections.ArrayList
$r2 = New-Object System.Collections.ArrayList
$res = @()
try {
    $sp.Open(); Start-Sleep -Milliseconds 400
    Snd 'z'; Start-Sleep -Milliseconds 300
    Snd 'm5'; Start-Sleep -Milliseconds 200
    Snd 'x0'; Snd 'y0'; Start-Sleep -Milliseconds 200
    $sp.DiscardInBuffer(); $script:rxbuf = ''

    Write-Host ("-- M1 (left) at +${Duty}% --")
    Snd "x$Duty"
    Grab $r1 $Sec
    Snd 'x0'; Start-Sleep -Milliseconds 400
    $res += (Analyse $r1 'c1' 'enc1 (left,  PA7/PB19)')
    $ctl1 = Analyse $r1 'c2' 'enc2 while idle       '

    $sp.DiscardInBuffer(); $script:rxbuf = ''
    Write-Host ("-- M2 (right) at +${Duty}%  [control: a healthy channel must be monotonic] --")
    Snd "y$Duty"
    Grab $r2 $Sec
    Snd 'y0'; Start-Sleep -Milliseconds 400
    $res += (Analyse $r2 'c2' 'enc2 (right, PB20/PB21)')
}
catch { Write-Host ("EXCEPTION: " + $_.Exception.Message) }
finally {
    if ($sp.IsOpen) { try { Snd 'z' } catch { }; Start-Sleep -Milliseconds 250; try { $sp.Close() } catch { } }
    $sp.Dispose()
}

Write-Host ""
Write-Host "---- verdict ----"
$e1 = $res | Where-Object { $_ -and $_.label -like 'enc1*' }
$e2 = $res | Where-Object { $_ -and $_.label -like 'enc2 (right*' }
if (-not $e1 -or -not $e2) {
    Write-Host "RESULT: INCONCLUSIVE - did not get both channels"
    exit 2
}
Write-Host ("enc2 (control) : net {0} from {1} counts of movement, {2:N1} % cancelling" -f $e2.net, $e2.total, $e2.balance)
Write-Host ("enc1 (suspect) : net {0} from {1} counts of movement, {2:N1} % cancelling" -f $e1.net, $e1.total, $e1.balance)
if ($e2.balance -gt 15.0) {
    Write-Host "** the CONTROL channel is also cancelling, so this test cannot discriminate here"
    Write-Host "RESULT: INCONCLUSIVE"
    exit 2
}
if ($e1.total -lt 20) {
    Write-Host "** enc1 saw essentially NO transitions at all (not even cancelling ones)"
    Write-Host "   => that is a fully dead channel pair: no signal reaching PA7 AND PB19"
    Write-Host "      (connector off / encoder VCC missing / both wires on the wrong pins)"
    Write-Host "RESULT: FAIL - enc1 dead"
    exit 1
}
if ($e1.balance -gt 30.0) {
    Write-Host "** enc1 moves back and forth in near-equal amounts while driven in ONE direction"
    Write-Host "   => ONE of its two channels is stuck: the decoder walks forward and back and the"
    Write-Host "      counts cancel, so a spinning wheel reports ~0 rpm."
    Write-Host "   Check PA7 (A) and PB19 (B) individually: which one never changes level?"
    Write-Host "   Likely causes: that wire off / wrong pin after the motor swap / missing pull-up"
    Write-Host "      (these are open-collector outputs - they need a pull-up to read high)."
    Write-Host "RESULT: FAIL - one enc1 channel stuck"
    exit 1
}
Write-Host "enc1 counts march one way like the control does => the encoder is reporting honestly,"
Write-Host "  so the wheel really is barely turning and the problem is in the motor/drive path."
Write-Host "RESULT: PASS - encoder behaviour is consistent; look at the motor side"
exit 0
