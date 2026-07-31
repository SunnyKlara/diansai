# vis_watch.ps1 - live watch of the VIS_UART (PB16) physical layer while you move wires.
#
# WHY THIS EXISTS
#   ball_bringup check 7 answered "are bytes arriving on PB16" with a hard no (bytes=0 err=0). Fixing
#   that is a physical loop: move a wire, look, move another. Re-running the whole bring-up for each
#   attempt costs ~40 s and buries the one number that matters. This polls the firmware's byte/error
#   counters once a second and prints a timestamped delta, so the moment a wire lands right it shows up
#   immediately - and the timestamp tells you WHICH action did it.
#
# THE ONE TEST THAT SPLITS THE BLAME  (-GndTouch)
#   PB16 is a plain UART input. Briefly touching it to GND and releasing generates real edges, and a
#   framing error is the expected result because a long low is not a valid character. So:
#       err goes up when you touch GND  -> the pin, the PINCM mux, the UART config and this firmware are
#                                          all PROVEN GOOD. Whatever is wrong is on the K230 side or in
#                                          the wire between them. Stop suspecting the MCU.
#       nothing moves at all            -> suspect our side: wrong pin on the header, PB16 not actually
#                                          routed out, or the build is not the one running.
#   It costs one jumper and five seconds, and it stops the classic waste where both sides are blamed in
#   turn for an hour. Touching an input to GND cannot damage anything.
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File vis_watch.ps1 -Port COM30 -Sec 120
#   powershell -NoProfile -ExecutionPolicy Bypass -File vis_watch.ps1 -Port COM30 -GndTouch
#
# EXIT CODES: 0 = bytes arrived (physical layer OK), 1 = still nothing, 2 = could not talk to the board
# ASCII only in the code; the board's own hints may be Chinese.
param(
    [string]$Port  = "COM30",
    [int]$Baud     = 115200,
    [int]$Sec      = 120,
    [switch]$GndTouch,               # print the blame-splitting procedure and watch err instead of bytes
    [string]$Out   = "_logs\vis_watch_out.txt"
)

$ErrorActionPreference = "Continue"
$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

function Finish([string]$verdict, [int]$code) {
    L ""; L "RESULT: $verdict"
    try {
        $d = Split-Path $Out -Parent
        if ($d -and -not (Test-Path $d)) { New-Item -ItemType Directory -Path $d -Force | Out-Null }
        Set-Content $Out $log.ToString() -Encoding UTF8
        Write-Host "(log -> $Out)"
    } catch {}
    if ($sp -and $sp.IsOpen) { try { $sp.Close(); $sp.Dispose() } catch {} }
    exit $code
}

L ("================ vis_watch  {0} ================" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"))
L "watching VIS_UART(PB16) raw byte + UART error counters via the V command"

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
$sp.Encoding = [System.Text.Encoding]::UTF8      # the board's hints are UTF-8 Chinese
try { $sp.Open() } catch { Write-Host "OPEN_FAIL ($Port): $($_.Exception.Message)" -ForegroundColor Red; exit 2 }

function Send([string]$cmd) {
    # 25 ms per character: the MSPM0 RX FIFO is 4 bytes deep and a burst write silently loses the tail
    # (跨题坑库 L116). Commands here are 1-2 characters, so the cost is nil.
    foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
}

# Ask V once, return @{ bytes; err; tail; raw }, or $null if the board did not answer with a pb16: line.
function AskPb16() {
    try { $sp.DiscardInBuffer() } catch {}     # read the REPLY, never the backlog (跨题坑库 L173)
    Send "V"
    $buf = ""
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    while ($sw.Elapsed.TotalMilliseconds -lt 700) {
        try { $buf += $sp.ReadExisting() } catch {}
        Start-Sleep -Milliseconds 20
    }
    # Two candidate pins are watched by the firmware: PB16 (what syscfg was built around) and PB5 (what
    # the carrier board doc assigns to the camera). Take whichever is carrying traffic.
    $r = $null
    foreach ($ln in ($buf -split "`n")) {
        if ($ln -match 'pb(?<p>16|5)\s*\([^)]*\):\s*bytes=(?<b>\d+)\s+err=(?<e>\d+)') {
            $b = [int]$Matches['b']; $e = [int]$Matches['e']; $p = "PB" + $Matches['p']
            $tail = ""
            if ($ln -match "ascii='(?<a>[^']*)'") { $tail = $Matches['a'] }
            if ($null -eq $r -or $b -gt $r.bytes -or $e -gt $r.err) {
                $r = [pscustomobject]@{ bytes = $b; err = $e; tail = $tail; pin = $p; raw = $ln.Trim() }
            }
        }
    }
    return $r
}

$first = AskPb16
if (-not $first) {
    L "the board did not print a 'pb16:' line in reply to V."
    L "That line only exists in builds from 2026-07-31 on. Either the chip runs an older image (reflash"
    L "and check 'wrote N bytes' == build.ps1's text+data), or telemetry is muted to the wireless sink"
    L "only (send l3 or l1 first). Without it this script cannot tell wiring from protocol."
    Finish "INCONCLUSIVE - no pb16: counter in the firmware reply" 2
}

L ("baseline: bytes={0} err={1}" -f $first.bytes, $first.err)
if ($GndTouch) {
    L ""
    L "---- blame-splitting procedure (do this now) ----"
    L "  1. Find PB16 on the header (the camera RX pin - the same one the K230 IO11 should reach)."
    L "  2. With a jumper, touch it to GND for about a second, then release. Repeat a few times."
    L "  3. Watch the err column below."
    L ""
    L "  err RISES     -> our side is PROVEN GOOD (pin, mux, UART, firmware). The fault is the K230 or"
    L "                   the wire. Next: confirm the K230 prints BALL_UART_V4 lines on its own console,"
    L "                   then confirm its IO11 really lands on PB16 and that GND is shared."
    L "  err UNCHANGED -> suspect OUR side: wrong physical pin, PB16 not brought out on this header, or"
    L "                   the running image is not this build."
    L ""
} else {
    L "move/plug the camera wire now; each line below is one poll. Ctrl-C is safe to use."
    L ""
}

$sw = [System.Diagnostics.Stopwatch]::StartNew()
$prev = $first
$peakBytes = $first.bytes
$peakErr = $first.err
$sawBytes = $false
$sawErr = $false

while ($sw.Elapsed.TotalSeconds -lt $Sec) {
    Start-Sleep -Milliseconds 900
    $cur = AskPb16
    if (-not $cur) { L ("{0,5:N1}s  (no reply)" -f $sw.Elapsed.TotalSeconds); continue }
    $db = $cur.bytes - $prev.bytes
    $de = $cur.err   - $prev.err
    if ($cur.bytes -gt $peakBytes) { $peakBytes = $cur.bytes }
    if ($cur.err   -gt $peakErr)   { $peakErr   = $cur.err }
    if ($db -gt 0) { $sawBytes = $true }
    if ($de -gt 0) { $sawErr = $true }
    $mark = if ($db -gt 0) { "  <== BYTES ARRIVING" } elseif ($de -gt 0) { "  <== edges, but they do not decode" } else { "" }
    $tl = if ($cur.tail) { "  ascii='" + $cur.tail + "'" } else { "" }
    L ("{0,5:N1}s  bytes={1,-8} (+{2,-5}) err={3,-6} (+{4,-4}){5}{6}" -f `
        $sw.Elapsed.TotalSeconds, $cur.bytes, $db, $cur.err, $de, $tl, $mark)
    $prev = $cur
    # Once real bytes flow there is nothing left to watch - stop early rather than burn the full window.
    if ($sawBytes -and $db -gt 0 -and $cur.bytes -gt 40) { L ""; L "enough bytes to judge; stopping early."; break }
}

L ""
L ("summary: bytes {0} -> {1}   err {2} -> {3}" -f $first.bytes, $prev.bytes, $first.err, $prev.err)

if ($sawBytes) {
    L "bytes are arriving on PB16 => the physical layer is GOOD."
    if ($prev.tail) { L ("last 8 raw bytes as ascii: '" + $prev.tail + "'") }
    L "If ascii looks like '`$BP,1,+...' the protocol is right too - rerun ball_bringup to confirm check 7."
    L "If it is garbage, the baud rate does not match (K230 must be 115200 8N1)."
    Finish "PASS - bytes reached PB16" 0
}
if ($GndTouch) {
    if ($sawErr) {
        L "err rose while you touched GND, but no real bytes came in."
        L "=> OUR SIDE IS PROVEN GOOD. The pin, the PINCM mux, the UART peripheral and this firmware all"
        L "   react to edges on PB16. The remaining fault is on the K230 or in the wire between them."
        Finish "FAIL(camera side) - PB16 works, nothing is transmitting into it" 1
    }
    L "neither bytes nor errors moved during the GND touch."
    L "=> Suspect OUR side first: is that really PB16 on the header? is it brought out at all? is the"
    L "   running image this build (V printed a pb16: line, so it is - so look at the pin)."
    Finish "FAIL(our side) - PB16 shows no reaction even to a GND touch" 1
}
L "no bytes, no errors: PB16 saw nothing at all for the whole window."
L "Run again with -GndTouch to settle whether the fault is on our side or the camera's - that test takes"
L "five seconds and removes half the search space."
Finish "FAIL - nothing arrived on PB16" 1
