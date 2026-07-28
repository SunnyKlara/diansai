# link_duplex_test.ps1 - is the telemetry loss caused by OUR OWN downlink traffic?
#
# THE QUESTION
#   Measured on this car (2026-07-27, wireless COM4, f20 = 50 Hz):
#     receive-only, still, 10 s        ->  0.00 % loss (497/497 by #seq)
#     run_straight (sends commands)    -> 12.96 % in its IDLE baseline, 23.66 % while driving
#   and every single one of the 38 holes was exactly ONE line long (zero bursts).
#   Isolated single-line drops plus "only lossy when we transmit" does not look like distance,
#   shadowing or motor EMI - those come as bursts. It looks like our own downlink colliding with
#   the uplink on a half-duplex-ish ESP-01S UDP passthrough link.
#   This matters before touching the transport: if the loss is self-inflicted, moving to TCP makes
#   it WORSE (every segment gets an ACK, so the packet count roughly doubles).
#
# THE DESIGN (one variable: how often the PC transmits; nothing else changes)
#   A1  receive only                      -> baseline
#   B1  + probe every -Dose1 ms           -> low dose
#   B2  + probe every -Dose2 ms           -> mid dose
#   B3  + probe every -Dose3 ms           -> high dose
#   A2  receive only again                -> proves the environment did not just drift
#   Loss is the REAL loss from the firmware #seq numbers (holes), not "did the line count match".
#
# THE PROBE: "QQ" - and why exactly that
#   car.c line 330: a line that fails the command format gate does `g_cmd_rej++; return;`
#   => completely silent, no state change, and no print_status(). The gate accepts a lone letter,
#   so "Q" would fall through to the switch and trigger print_status() (line 305) - that would add
#   UPLINK bytes and wreck the experiment. Two letters get rejected, so "QQ" is pure downlink.
#   Bonus: the rej counter (read via `?` before/after, outside the measured windows) proves the
#   probes really reached the MCU rather than being eaten by the radio.
#
# Sent one char per 25 ms, exactly like uart_send.ps1 / run_straight.ps1 do, so this reproduces
# the real scripts' downlink pattern rather than an artificial burst.
#
# SAFETY: no motion command is ever sent. The car stays in whatever mode it is in (send `z` first
# yourself if it is not idle). Telemetry rate is NOT touched either - it is measured, not set,
# so that the only difference between phases really is the downlink.
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#
# Usage: powershell -File link_duplex_test.ps1 -Port COM4 [-Sec 10]

param(
    [string]$Port  = 'COM4',
    [int]$Baud     = 115200,
    [double]$Sec   = 10.0,
    [int]$Dose1    = 1000,
    [int]$Dose2    = 400,
    [int]$Dose3    = 150,
    [string]$Out   = 'link_duplex_out.txt'
)

$ErrorActionPreference = 'Continue'
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$script:rxbuf = ''
$script:rej   = $null
$re = [regex]'#(?<seq>\d+)\s+t(?<t>\d+)'

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, 'None', 8, 'One'
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false

# Pull whatever has arrived, cut it into COMPLETE lines only (a chunk from ReadExisting almost
# always ends mid-line; judging a partial line would fake both seq numbers and loss).
function Drain([System.Collections.ArrayList]$sink) {
    $txt = ''
    try { $txt = $sp.ReadExisting() } catch { }
    if ($txt) { $script:rxbuf += $txt }
    while ($script:rxbuf.Contains("`n")) {
        $i   = $script:rxbuf.IndexOf("`n")
        $ln  = $script:rxbuf.Substring(0, $i).TrimEnd("`r")
        $script:rxbuf = $script:rxbuf.Substring($i + 1)
        if ($ln -match 'rej=(\d+)') { $script:rej = [int]$Matches[1] }
        if ($null -eq $sink) { continue }
        if ($ln -notmatch '^\[ctl\]') { continue }
        $m = $re.Match($ln)
        if ($m.Success) { [void]$sink.Add([pscustomobject]@{ seq = [int]$m.Groups['seq'].Value; t = [int]$m.Groups['t'].Value }) }
    }
}

# NOTE the sink is passed in on purpose. Sending takes 3 x 25 ms and telemetry keeps arriving the
# whole time; draining into $null here would throw those lines away and manufacture loss in
# exactly the phases under test - i.e. the measurement would "prove" the hypothesis by itself.
function Probe([System.Collections.ArrayList]$sink) {
    foreach ($ch in "QQ`n".ToCharArray()) {
        $sp.Write([string]$ch)
        $end = (Get-Date).AddMilliseconds(25)
        while ((Get-Date) -lt $end) { Drain $sink; Start-Sleep -Milliseconds 5 }
    }
}

# One phase. $everyMs = 0 means receive-only.
function Phase([string]$name, [int]$everyMs) {
    $rows = New-Object System.Collections.ArrayList
    $script:rxbuf = ''
    try { [void]$sp.ReadExisting() } catch { }
    $sent = 0
    $sw   = [System.Diagnostics.Stopwatch]::StartNew()
    $next = if ($everyMs -gt 0) { 0.0 } else { [double]::MaxValue }
    while ($sw.Elapsed.TotalSeconds -lt $Sec) {
        if ($sw.Elapsed.TotalMilliseconds -ge $next) {
            # count the probe first: if the link eats it we still want it in the denominator
            $sent++
            $next = $sw.Elapsed.TotalMilliseconds + $everyMs
            Probe $rows
        }
        Drain $rows
        Start-Sleep -Milliseconds 5
    }
    Drain $rows

    $n = $rows.Count
    if ($n -lt 5) {
        L ("  {0,-22} got only {1} lines - phase unusable" -f $name, $n)
        return $null
    }
    $exp   = $rows[$n - 1].seq - $rows[0].seq + 1
    $loss  = if ($exp -gt 0) { 100.0 * ($exp - $n) / $exp } else { 0.0 }
    $gaps  = 0; $maxGap = 0; $dts = New-Object System.Collections.ArrayList
    for ($i = 1; $i -lt $n; $i++) {
        $d = $rows[$i].seq - $rows[$i - 1].seq
        if ($d -gt 1) { $gaps++; if (($d - 1) -gt $maxGap) { $maxGap = $d - 1 } }
        elseif ($d -eq 1) { [void]$dts.Add($rows[$i].t - $rows[$i - 1].t) }
    }
    $dtAvg = if ($dts.Count -gt 0) { ($dts | Measure-Object -Average).Average } else { 0 }
    $o = [pscustomobject]@{
        phase = $name; every = $everyMs; sent = $sent; got = $n; exp = $exp
        loss = $loss; gaps = $gaps; maxGap = $maxGap; dt = $dtAvg
    }
    L ("  {0,-22} probes {1,4}   lines {2,4}/{3,4}   LOSS {4,6:N2} %   holes {5,3} (max {6} line)   fw dt {7,5:N1} ms" -f `
        $name, $sent, $n, $exp, $loss, $gaps, $maxGap, $dtAvg)
    return $o
}

L ("================ link_duplex_test  " + (Get-Date -Format 'HH:mm:ss') + " ================")
L ("port $Port @ $Baud   ${Sec}s per phase   probe = 'QQ' (silently rejected by the format gate)")
L "no motion command is sent; telemetry rate is measured, not changed"
L ""

try { $sp.Open() } catch { L "OPEN_FAIL ($Port): $($_.Exception.Message)"; exit 1 }
$results = New-Object System.Collections.ArrayList
try {
    Start-Sleep -Milliseconds 400

    # rej before, outside any measured window
    foreach ($ch in "?`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    Start-Sleep -Milliseconds 800
    Drain $null
    $rej0 = $script:rej
    L ("rej before : {0}" -f $(if ($null -ne $rej0) { $rej0 } else { 'not seen' }))
    L ""

    foreach ($p in @(
        @{ n = 'A1 receive-only';        e = 0 },
        @{ n = "B1 probe/${Dose1}ms";    e = $Dose1 },
        @{ n = "B2 probe/${Dose2}ms";    e = $Dose2 },
        @{ n = "B3 probe/${Dose3}ms";    e = $Dose3 },
        @{ n = 'A2 receive-only';        e = 0 }
    )) {
        $r = Phase $p.n $p.e
        if ($null -ne $r) { [void]$results.Add($r) }
    }

    L ""
    foreach ($ch in "?`n".ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
    Start-Sleep -Milliseconds 800
    Drain $null
    $rej1 = $script:rej
    $totalProbes = ($results | Where-Object { $_.every -gt 0 } | Measure-Object sent -Sum).Sum
    L ("rej after  : {0}   (delta {1} vs {2} probes sent)" -f `
        $(if ($null -ne $rej1) { $rej1 } else { 'not seen' }), `
        $(if (($null -ne $rej0) -and ($null -ne $rej1)) { $rej1 - $rej0 } else { 'n/a' }), $totalProbes)
    if (($null -ne $rej0) -and ($null -ne $rej1)) {
        $dr = $rej1 - $rej0
        if ($dr -ge 1) {
            L "  => the probes DID reach the MCU, so the downlink really was carrying traffic"
            if ($totalProbes -gt 0 -and $dr -lt $totalProbes) {
                L ("  => but only {0} of {1} landed: the DOWNLINK itself is lossy too ({2:N1} % of probes lost)" -f `
                    $dr, $totalProbes, (100.0 * ($totalProbes - $dr) / $totalProbes))
            }
        } else {
            L "  => rej did not move: the probes never reached the MCU. Treat the phases as inconclusive."
        }
    }
}
catch { L ("EXCEPTION: " + $_.Exception.Message) }
finally {
    if ($sp.IsOpen) { try { $sp.Close() } catch { } }
    $sp.Dispose()
}

L ""
L "---- verdict ----"
$a = @($results | Where-Object { $_.every -eq 0 })
$b = @($results | Where-Object { $_.every -gt 0 })
if ($a.Count -lt 2 -or $b.Count -lt 2) {
    L "RESULT: INCONCLUSIVE - not enough usable phases"
    Set-Content $Out $log.ToString() -Encoding ASCII
    exit 2
}
$aAvg = ($a | Measure-Object loss -Average).Average
$aSpread = ($a | Measure-Object loss -Maximum).Maximum - ($a | Measure-Object loss -Minimum).Minimum
$bMax = ($b | Measure-Object loss -Maximum).Maximum
$bHi  = ($b | Sort-Object every | Select-Object -First 1).loss     # smallest interval = highest dose
$bLo  = ($b | Sort-Object every -Descending | Select-Object -First 1).loss
L ("receive-only loss : {0:N2} % avg  (spread between A1 and A2: {1:N2} pt)" -f $aAvg, $aSpread)
L ("with downlink     : {0:N2} % at the lowest dose -> {1:N2} % at the highest" -f $bLo, $bHi)

if ($aSpread -gt 5.0) {
    L "RESULT: INCONCLUSIVE - the two receive-only phases disagree, so the environment drifted"
    L "  during the test. Re-run; if it keeps happening the link is unstable on its own and the"
    L "  downlink question cannot be answered this way."
    Set-Content $Out $log.ToString() -Encoding ASCII
    exit 2
}
if ($bMax -gt ($aAvg + 3.0)) {
    L "RESULT: downlink IS the dominant loss source (self-inflicted)."
    if ($bHi -gt $bLo + 3.0) { L "  Dose effect confirmed: more probes per second -> more loss." }
    else { L "  No clean dose effect: transmitting at all hurts, the rate matters less." }
    L "  => Fix is architectural, not radio: do not chat while the run is in progress (send the"
    L "     setup commands before it and read the result after), and drop to one sink (l1/l2) at"
    L "     f20 or faster. Do NOT switch to TCP for this - its ACKs add downlink packets."
    Set-Content $Out $log.ToString() -Encoding ASCII
    exit 0
}
L "RESULT: downlink is NOT the cause - loss is about the same whether we transmit or not."
L "  => Now it is worth looking at the radio layer: channel census (AT+CWLAP, count APs on"
L "     1/6/11) and AT+RFPOWER, via the wired AT bridge (`b<sec>`, needs the DAP plugged in)."
Set-Content $Out $log.ToString() -Encoding ASCII
exit 0
