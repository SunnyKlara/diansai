# esp_link_test.ps1 - one-shot end-to-end test of the ESP-01S <-> ESP-01S UDP serial bridge.
#   Opens BOTH modules in a SINGLE process (no port contention / no cross-process timing games),
#   sets up a UDP link, then measures the real byte path in both directions.
#
#   Module A = car side  = SoftAP  192.168.4.1   (default COM5)
#   Module B = PC side   = STA     192.168.4.2   (default COM6)
#
# Phases (each one adds exactly ONE variable, so a failure is attributable):
#   P1  UDP link up (AT+CIPSTART on both, AT+CIPSTATUS readback)
#   P2  non-transparent send A->B and B->A  (AT+CIPSEND=<n>, receiver shows +IPD)
#   P3  transparent mode both ends (AT+CIPMODE=1 + AT+CIPSEND), raw bytes both directions
#   P4  quantitative: N lines A->B, count complete lines + one-way latency (avg/min/max)
#   P5  escape check: '+++' returns to AT command mode (this is the way OUT of passthrough)
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_link_test.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_link_test.ps1 -Lines 100 -Phase 4
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$PortA  = "COM5",
    [string]$PortB  = "COM6",
    [int]$Baud      = 115200,
    [string]$IpA    = "192.168.4.1",
    [string]$IpB    = "192.168.4.2",
    [int]$UdpPort   = 3333,
    [int]$Lines     = 60,        # P4: how many telemetry-like lines to push
    [int]$LineGapMs = 100,       # P4: gap between lines (100ms = 10Hz, matches PRINT_MS)
    [int]$Phase     = 5,         # run phases 1..$Phase
    [switch]$Sweep,              # after P4, sweep the line rate to find where loss starts
    [int]$SoakSec   = 0,         # after P4, stream indexed lines for N seconds (stability / dropout hunt)
    [int]$SoakGapMs = 100,       # soak line rate (100ms = 10Hz = the firmware default PRINT_MS)
    [string]$Out    = "esp_link_test_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

function NewPort([string]$p, [int]$b) {
    $sp = New-Object System.IO.Ports.SerialPort $p, $b, None, 8, one
    $sp.ReadTimeout  = 200
    $sp.WriteTimeout = 1000
    $sp.DtrEnable    = $false
    $sp.RtsEnable    = $false
    return $sp
}

# send an AT line and collect the reply for $ms
function AT($sp, [string]$cmd, [int]$ms) {
    $sp.Write($cmd + "`r`n")
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 30
    }
    return $rx.ToString()
}

# send raw payload (no CRLF appended)
function RAW($sp, [string]$payload) { $sp.Write($payload) }

function Drain($sp, [int]$ms) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $rx = New-Object System.Text.StringBuilder
    while ($sw.Elapsed.TotalMilliseconds -lt $ms) {
        try { [void]$rx.Append($sp.ReadExisting()) } catch {}
        Start-Sleep -Milliseconds 20
    }
    return $rx.ToString()
}

function Clean([string]$s) { return ($s -replace "`r`n", " | ").Trim() }

L "================ ESP-01S UDP bridge test  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ================"
L "A(car/SoftAP) = $PortA @ $IpA    B(PC/STA) = $PortB @ $IpB    UDP port $UdpPort    baud $Baud"

$A = NewPort $PortA $Baud
$B = NewPort $PortB $Baud
try { $A.Open() } catch { L "OPEN_FAIL A ($PortA): $($_.Exception.Message)"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
try { $B.Open() } catch { L "OPEN_FAIL B ($PortB): $($_.Exception.Message)"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
Start-Sleep -Milliseconds 250
[void](Drain $A 200); [void](Drain $B 200)

# Always start from a known state: a previous run (or an aborted one) may have left a module
# in passthrough, where it treats AT commands as DATA and answers nothing -> looks dead.
# '+++' escape is harmless if already in AT mode (the trailing CRLF flushes the stray '+++').
L ""
L "---- P-1  escape any leftover passthrough on both ends ----"
Start-Sleep -Milliseconds 1100
try { $A.Write("+++") } catch {}
try { $B.Write("+++") } catch {}
Start-Sleep -Milliseconds 1100
try { $A.Write("`r`n") } catch {}
try { $B.Write("`r`n") } catch {}
Start-Sleep -Milliseconds 300
[void](Drain $A 300); [void](Drain $B 300)
L "done"

$fail = 0

# ---------------------------------------------------------------- P0: sanity
L ""
L "---- P0  both modules answer AT ----"
$ra = AT $A "AT" 600
$rb = AT $B "AT" 600
L "A: $(Clean $ra)"
L "B: $(Clean $rb)"
if ($ra -notmatch "OK") { L "  !! A not answering"; $fail++ }
if ($rb -notmatch "OK") { L "  !! B not answering"; $fail++ }

# ---------------------------------------------------------------- P1: UDP link
if ($Phase -ge 1 -and $fail -eq 0) {
    L ""
    L "---- P1  bring up UDP link (CIPMUX=0, CIPMODE=0, CIPSTART UDP) ----"
    # make sure we start from a known state
    [void](AT $A "AT+CIPCLOSE" 500)
    [void](AT $B "AT+CIPCLOSE" 500)
    [void](AT $A "AT+CIPMODE=0" 400)
    [void](AT $B "AT+CIPMODE=0" 400)
    [void](AT $A "AT+CIPMUX=0" 400)
    [void](AT $B "AT+CIPMUX=0" 400)
    $ra = AT $A "AT+CIPSTART=`"UDP`",`"$IpB`",$UdpPort,$UdpPort,0" 1500
    $rb = AT $B "AT+CIPSTART=`"UDP`",`"$IpA`",$UdpPort,$UdpPort,0" 1500
    L "A CIPSTART: $(Clean $ra)"
    L "B CIPSTART: $(Clean $rb)"
    $sa = AT $A "AT+CIPSTATUS" 700
    $sb = AT $B "AT+CIPSTATUS" 700
    L "A CIPSTATUS: $(Clean $sa)"
    L "B CIPSTATUS: $(Clean $sb)"
    if (($ra -notmatch "OK|ALREADY CONNECTED") ) { L "  !! A CIPSTART failed"; $fail++ }
    if (($rb -notmatch "OK|ALREADY CONNECTED") ) { L "  !! B CIPSTART failed"; $fail++ }
}

# ---------------------------------------------------------------- P2: non-transparent
if ($Phase -ge 2 -and $fail -eq 0) {
    L ""
    L "---- P2  non-transparent send (proves the UDP data path itself) ----"
    # A -> B, with up to 3 attempts. Attempt 1 can legitimately be lost while the SoftAP
    # ARP-resolves the station's MAC (lightweight IP stacks drop the datagram that triggered
    # the ARP request). We log the SENDER's own reply too, so "did not send" is separable
    # from "did not arrive".
    $a2b = $false
    for ($try = 1; $try -le 3; $try++) {
        [void](Drain $B 200)
        $msg = "A2B_HELLO$try"
        $r = AT $A "AT+CIPSEND=$($msg.Length)" 700
        RAW $A $msg
        Start-Sleep -Milliseconds 400
        $sndr = Drain $A 500          # expect "SEND OK" (or "SEND FAIL" / "busy")
        $got  = Drain $B 700
        L "A->B try ${try}: prompt=$(Clean $r)  senderSaid=$(Clean $sndr)  Bgot=$(Clean $got)"
        if ($got -match "A2B_HELLO") { $a2b = $true; L "  OK  A -> B works (attempt $try)"; break }
    }
    if (-not $a2b) { L "  !! A -> B FAILED after 3 attempts"; $fail++ }

    [void](Drain $A 200)
    $msg = "B2A_HELLO"
    $r = AT $B "AT+CIPSEND=$($msg.Length)" 700
    RAW $B $msg
    Start-Sleep -Milliseconds 700
    $got = Drain $A 600
    L "B send prompt: $(Clean $r)"
    L "A received   : $(Clean $got)"
    if ($got -match "B2A_HELLO") { L "  OK  B -> A works" } else { L "  !! B -> A FAILED"; $fail++ }
}

# ---------------------------------------------------------------- P3: transparent
$transparent = $false
if ($Phase -ge 3 -and $fail -eq 0) {
    L ""
    L "---- P3  transparent mode both ends (CIPMODE=1 + CIPSEND) ----"
    $r1 = AT $A "AT+CIPMODE=1" 500
    $r2 = AT $B "AT+CIPMODE=1" 500
    L "A CIPMODE=1: $(Clean $r1)"
    L "B CIPMODE=1: $(Clean $r2)"
    $r3 = AT $A "AT+CIPSEND" 800
    $r4 = AT $B "AT+CIPSEND" 800
    L "A CIPSEND  : $(Clean $r3)"
    L "B CIPSEND  : $(Clean $r4)"
    if (($r3 -match ">") -and ($r4 -match ">")) {
        $transparent = $true
        L "  both ends in passthrough"
    } else {
        L "  !! could not enter passthrough on both ends"; $fail++
    }

    if ($transparent) {
        [void](Drain $B 300)
        RAW $A "RAW_A2B_LINE`n"
        Start-Sleep -Milliseconds 600
        $got = Drain $B 500
        L "B got (raw A->B): $(Clean $got)"
        if ($got -match "RAW_A2B_LINE") { L "  OK  transparent A -> B" } else { L "  !! transparent A -> B FAILED"; $fail++ }

        [void](Drain $A 300)
        RAW $B "RAW_B2A_LINE`n"
        Start-Sleep -Milliseconds 600
        $got = Drain $A 500
        L "A got (raw B->A): $(Clean $got)"
        if ($got -match "RAW_B2A_LINE") { L "  OK  transparent B -> A" } else { L "  !! transparent B -> A FAILED"; $fail++ }
    }
}

# ---------------------------------------------------------------- P4: quantitative
if ($Phase -ge 4 -and $transparent) {
    L ""
    L "---- P4  telemetry stress: $Lines lines, gap ${LineGapMs}ms (A -> B) ----"
    [void](Drain $B 300)
    $lat = @()
    $rxAll = New-Object System.Text.StringBuilder
    for ($i = 1; $i -le $Lines; $i++) {
        # shaped like the real firmware telemetry line, ~60 chars, self-contained
        $payload = ("[ctl] SPEED tgt:500 | I:{0,4} | V:{1,5} | PWM:{2,4} | C:{3,7} #{4}" -f 123, 498, 350, 1234567, $i)
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        RAW $A ($payload + "`n")
        # wait for anything to show up at B (one-way latency incl. both USB-serial hops)
        # TIGHT SPIN, no Start-Sleep: on Windows the timer granularity is ~15.6ms, so a
        # `Start-Sleep -Milliseconds 3` inside the latency window would add ~15ms of pure
        # measurement error and make the link look far slower than it is.
        $seen = $false
        while ($sw.Elapsed.TotalMilliseconds -lt 400) {
            $chunk = ""
            try { $chunk = $B.ReadExisting() } catch {}
            if ($chunk.Length -gt 0) {
                [void]$rxAll.Append($chunk)
                if (-not $seen) { $lat += $sw.Elapsed.TotalMilliseconds; $seen = $true }
            }
            if ($seen -and $sw.Elapsed.TotalMilliseconds -gt 40) { break }
        }
        $remain = $LineGapMs - $sw.Elapsed.TotalMilliseconds
        if ($remain -gt 0) { Start-Sleep -Milliseconds $remain }
    }
    [void]$rxAll.Append((Drain $B 800))
    $text = $rxAll.ToString()

    $okLines = 0
    $seenIdx = @{}
    foreach ($ln in ($text -split "`n")) {
        if ($ln -match '^\[ctl\] SPEED tgt:500 \| I:\s*123 \| V:\s*498 \| PWM:\s*350 \| C:1234567 #(\d+)\s*$') {
            $okLines++
            $seenIdx[[int]$matches[1]] = $true
        }
    }
    $lost = $Lines - $seenIdx.Count
    $lossPct = [math]::Round(100.0 * $lost / $Lines, 2)
    L "bytes received      : $($text.Length)"
    L "complete good lines : $okLines / $Lines"
    L "unique indices seen : $($seenIdx.Count) / $Lines   -> LOSS $lossPct %"
    if ($lat.Count -gt 0) {
        $avg = [math]::Round(($lat | Measure-Object -Average).Average, 1)
        $mn  = [math]::Round(($lat | Measure-Object -Minimum).Minimum, 1)
        $mx  = [math]::Round(($lat | Measure-Object -Maximum).Maximum, 1)
        $sd  = 0.0
        if ($lat.Count -gt 1) {
            $m = ($lat | Measure-Object -Average).Average
            $acc = 0.0
            foreach ($v in $lat) { $acc += ($v - $m) * ($v - $m) }
            $sd = [math]::Round([math]::Sqrt($acc / ($lat.Count - 1)), 1)
        }
        L "one-way latency ms  : avg $avg  std $sd  min $mn  max $mx   (n=$($lat.Count), incl. 2x USB-serial)"
    }
    $missing = @()
    for ($i = 1; $i -le $Lines; $i++) { if (-not $seenIdx.ContainsKey($i)) { $missing += $i } }
    if ($missing.Count -gt 0) { L "missing indices     : $($missing -join ',')" }
}

# ------------------------------------------------- P4b: rate sweep (find the safe rate)
if ($Sweep -and $transparent) {
    L ""
    L "---- P4b  rate sweep A -> B (pure blast, no per-line latency wait) ----"
    L "  gap_ms   rate_Hz   lines   arrived   LOSS%    bytes"
    foreach ($gap in @(100, 50, 20, 10, 5, 2)) {
        [void](Drain $B 400)
        $n = 60
        $rate = 0
        if ($gap -gt 0) { $rate = [math]::Round(1000.0 / $gap, 1) }
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $rx = New-Object System.Text.StringBuilder
        for ($i = 1; $i -le $n; $i++) {
            $payload = ("[ctl] SPEED tgt:500 | I:{0,4} | V:{1,5} | PWM:{2,4} | C:{3,7} #{4}" -f 123, 498, 350, 1234567, $i)
            RAW $A ($payload + "`n")
            # drain as we go so the receive buffer never becomes the bottleneck
            try { [void]$rx.Append($B.ReadExisting()) } catch {}
            $target = $i * $gap
            while ($sw.Elapsed.TotalMilliseconds -lt $target) {
                try { [void]$rx.Append($B.ReadExisting()) } catch {}
            }
        }
        $elapsed = $sw.Elapsed.TotalMilliseconds
        [void]$rx.Append((Drain $B 1200))
        $text = $rx.ToString()
        $seenIdx = @{}
        foreach ($ln in ($text -split "`n")) {
            if ($ln -match '^\[ctl\] SPEED tgt:500 \| I:\s*123 \| V:\s*498 \| PWM:\s*350 \| C:1234567 #(\d+)\s*$') {
                $seenIdx[[int]$matches[1]] = $true
            }
        }
        $lossPct = [math]::Round(100.0 * ($n - $seenIdx.Count) / $n, 1)
        $actualHz = [math]::Round(1000.0 * $n / $elapsed, 1)
        L ("  {0,6}   {1,7}   {2,5}   {3,7}   {4,5}    {5,6}   (actual {6} Hz)" -f $gap, $rate, $n, $seenIdx.Count, $lossPct, $text.Length, $actualHz)
    }
}

# ------------------------------------------------- P4c: soak (does the link hold for minutes?)
if ($SoakSec -gt 0 -and $transparent) {
    L ""
    L "---- P4c  soak: ${SoakSec}s of indexed lines every ${SoakGapMs}ms (A -> B) ----"
    L "     hunting for what a 60-line burst cannot see: slow dropouts, AP disassociation,"
    L "     periodic beacon stalls - i.e. whether the link survives a whole competition run."
    [void](Drain $B 400)
    $rx = New-Object System.Text.StringBuilder
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    $sent = 0
    $marks = @()
    $lastReport = 0.0
    while ($sw.Elapsed.TotalSeconds -lt $SoakSec) {
        $sent++
        $payload = ("[ctl] SPEED tgt:500 | I:{0,4} | V:{1,5} | PWM:{2,4} | C:{3,7} #{4}" -f 123, 498, 350, 1234567, $sent)
        RAW $A ($payload + "`n")
        $target = $sent * $SoakGapMs
        while ($sw.Elapsed.TotalMilliseconds -lt $target) {
            try { [void]$rx.Append($B.ReadExisting()) } catch {}
        }
        # progress every 30s so a long soak is not a silent black box
        if (($sw.Elapsed.TotalSeconds - $lastReport) -ge 30) {
            $lastReport = $sw.Elapsed.TotalSeconds
            $sofar = $rx.ToString()
            $cnt = ([regex]::Matches($sofar, '#(\d+)\s*\n')).Count
            L ("     t={0,4:N0}s  sent={1,6}  arrived={2,6}" -f $sw.Elapsed.TotalSeconds, $sent, $cnt)
        }
    }
    [void]$rx.Append((Drain $B 1500))
    $text = $rx.ToString()

    $seen = @{}
    foreach ($ln in ($text -split "`n")) {
        if ($ln -match '^\[ctl\] SPEED tgt:500 \| I:\s*123 \| V:\s*498 \| PWM:\s*350 \| C:1234567 #(\d+)\s*$') {
            $seen[[int]$matches[1]] = $true
        }
    }
    # longest run of consecutive missing indices = worst dropout, in lines and in seconds
    $worst = 0; $worstAt = 0; $run = 0; $lost = 0
    for ($i = 1; $i -le $sent; $i++) {
        if ($seen.ContainsKey($i)) {
            $run = 0
        } else {
            $lost++; $run++
            if ($run -gt $worst) { $worst = $run; $worstAt = $i }
        }
    }
    $lossPct = 0.0
    if ($sent -gt 0) { $lossPct = [math]::Round(100.0 * $lost / $sent, 3) }
    L ""
    L "  duration          : $([math]::Round($sw.Elapsed.TotalSeconds,1)) s"
    L "  sent / arrived    : $sent / $($seen.Count)"
    L "  LOSS              : $lost lines = $lossPct %"
    L "  worst dropout     : $worst consecutive lines (~$([math]::Round($worst * $SoakGapMs / 1000.0, 2)) s) ending at #$worstAt"
    if ($worst -ge (2000 / $SoakGapMs)) { L "  !! a >2s hole means the link actually dropped - not just packet loss" }
}

# ---------------------------------------------------------------- P5: escape
if ($Phase -ge 5 -and $transparent) {
    L ""
    L "---- P5  '+++' escape back to AT command mode (the way OUT of passthrough) ----"
    Start-Sleep -Milliseconds 1100          # >=1s of silence before +++
    RAW $A "+++"
    Start-Sleep -Milliseconds 1100          # >=1s of silence after
    [void](Drain $A 300)
    $r = AT $A "AT" 800
    L "A after +++ , 'AT' -> $(Clean $r)"
    if ($r -match "OK") { L "  OK  A escaped passthrough (recoverable)" } else { L "  !! A did NOT escape - power cycle needed"; $fail++ }

    Start-Sleep -Milliseconds 1100
    RAW $B "+++"
    Start-Sleep -Milliseconds 1100
    [void](Drain $B 300)
    $r = AT $B "AT" 800
    L "B after +++ , 'AT' -> $(Clean $r)"
    if ($r -match "OK") { L "  OK  B escaped passthrough (recoverable)" } else { L "  !! B did NOT escape - power cycle needed"; $fail++ }

    # leave both in a clean, non-transparent state
    [void](AT $A "AT+CIPMODE=0" 400)
    [void](AT $B "AT+CIPMODE=0" 400)
    [void](AT $A "AT+CIPCLOSE" 400)
    [void](AT $B "AT+CIPCLOSE" 400)
    L "both ends reset to CIPMODE=0 / link closed"
}

L ""
if ($fail -eq 0) { L "==== RESULT: ALL CHECKS PASSED ====" } else { L "==== RESULT: $fail CHECK(S) FAILED ====" }

Set-Content -Path $Out -Value $log.ToString() -Encoding ASCII
try { $A.Close() } catch {}
try { $B.Close() } catch {}
