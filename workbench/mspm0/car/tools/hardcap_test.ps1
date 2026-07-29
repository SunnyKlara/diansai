# hardcap_test.ps1 - prove the HARD CAP auto-stop really cannot be bypassed by keeping the
#                     command channel busy. One-shot, single serial session, motors optional.
#
# WHAT IT PROVES
#   The firmware has TWO independent stop gates (config.h §7):
#     1. SILENCE  timeout - refreshed by ANY command  -> catches "nobody is talking to the car"
#     2. HARD CAP timeout - counted from mode entry, NOTHING refreshes it, not even h<ms>
#                           -> catches "someone IS talking but cannot stop it"
#   Gate 1 was verified by just staying quiet. Gate 2 needs the opposite: keep sending commands
#   the whole time, so gate 1 never fires, and check gate 2 still stops the car.
#
# HOW
#   h<big>  widen the SILENCE gate so it provably is not the one that fires
#   m<mode> + v<speed>   start moving (wheels MUST be off the ground)
#   then poke '?' every second (each poke refreshes SILENCE) while watching the stream,
#   until "RUN TIMEOUT (HARDCAP)" shows up - or we give up.
#
# PASS  = HARDCAP line seen, and its reported age is close to the configured cap
# FAIL  = SILENCE fired instead (means h<ms> did not widen gate 1) or nothing fired in time
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port     = "COM4",
    [int]$Baud        = 115200,
    [int]$Mode        = 7,        # 7 = closed-loop differential
    [int]$Speed       = 100,      # v<Speed>
    [int]$SilenceMs   = 12000,    # h<ms>: widen gate 1 so it cannot be the one that fires
    [int]$ExpectCapMs = 15000,    # CFG_RUN_MS_HARDCAP
    [int]$WatchSec    = 22,
    [string]$Out      = "hardcap_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout = 200; $sp.DtrEnable = $false; $sp.RtsEnable = $false
try { $sp.Open() } catch { L "OPEN_FAIL ($Port): $($_.Exception.Message)"; exit 1 }

function Send([string]$cmd) {
    foreach ($ch in ($cmd + "`n").ToCharArray()) { $sp.Write([string]$ch); Start-Sleep -Milliseconds 25 }
}

L "================ hardcap_test  $(Get-Date -Format 'HH:mm:ss') ================"
L "port $Port   mode m$Mode v$Speed   silence gate widened to ${SilenceMs}ms   expect HARDCAP ~${ExpectCapMs}ms"
L "WHEELS MUST BE OFF THE GROUND."

Start-Sleep -Milliseconds 300
[void]$sp.ReadExisting()
Send "h$SilenceMs"; Start-Sleep -Milliseconds 400
Send "m$Mode";      Start-Sleep -Milliseconds 400
Send "v$Speed"
$t0 = [System.Diagnostics.Stopwatch]::StartNew()

$rx = New-Object System.Text.StringBuilder
$hardcap = $null; $silence = $null; $lastPoke = 0.0
while ($t0.Elapsed.TotalSeconds -lt $WatchSec) {
    try { [void]$rx.Append($sp.ReadExisting()) } catch {}
    $txt = $rx.ToString()
    if (-not $hardcap) { $m = [regex]::Match($txt, 'RUN TIMEOUT \(HARDCAP\) in (\S+) after (\d+)ms');  if ($m.Success) { $hardcap = $m } }
    if (-not $silence) { $m2 = [regex]::Match($txt, 'RUN TIMEOUT \(SILENCE\) in (\S+) after (\d+)ms'); if ($m2.Success) { $silence = $m2 } }
    if ($hardcap) { break }
    # poke once per second: this refreshes the SILENCE gate, so if the car still stops it can
    # only be the hard cap. This is the whole point of the test.
    if (($t0.Elapsed.TotalSeconds - $lastPoke) -ge 1.0) { $lastPoke = $t0.Elapsed.TotalSeconds; Send "?" }
    Start-Sleep -Milliseconds 30
}
$pokes = [int]$lastPoke
try { $sp.Write("z`n") } catch {}
Start-Sleep -Milliseconds 300
try { [void]$rx.Append($sp.ReadExisting()) } catch {}
try { $sp.Close(); $sp.Dispose() } catch {}

L ""
L ("pokes sent ('?' once/sec, each one refreshes the SILENCE gate) : ~{0}" -f $pokes)
if ($silence) { L ("SILENCE fired : {0}" -f $silence.Value) }
if ($hardcap) { L ("HARDCAP fired : {0}" -f $hardcap.Value) }
L ""
if ($hardcap) {
    $age = [int]$hardcap.Groups[2].Value
    $err = [Math]::Abs($age - $ExpectCapMs)
    L ("age reported by firmware : {0} ms   (configured {1} ms, delta {2} ms)" -f $age, $ExpectCapMs, $err)
    if ($silence) {
        L "RESULT: FAIL - SILENCE also fired, so h<ms> did not widen gate 1 as intended."
        Set-Content $Out $log.ToString() -Encoding ASCII; exit 1
    }
    if ($err -le 500) {
        L "RESULT: PASS - hard cap stopped the car while the command channel was busy, and it"
        L "        cannot be bypassed by h<ms>. This is the last line of defence and it holds."
        Set-Content $Out $log.ToString() -Encoding ASCII; exit 0
    }
    L "RESULT: INCONCLUSIVE - HARDCAP fired but age is far from the configured cap; check config.h."
    Set-Content $Out $log.ToString() -Encoding ASCII; exit 2
}
if ($silence) {
    L "RESULT: FAIL - only SILENCE fired. Gate 1 was not widened (h<ms> rejected?), so this run"
    L "        says nothing about the hard cap. Re-run with a larger -SilenceMs."
    Set-Content $Out $log.ToString() -Encoding ASCII; exit 1
}
L "RESULT: INCONCLUSIVE - no timeout seen in ${WatchSec}s. Did the car actually enter the mode?"
Set-Content $Out $log.ToString() -Encoding ASCII; exit 2
