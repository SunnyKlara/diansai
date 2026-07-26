# esp_boot_risk.ps1 - measure the REAL risk that the car-side ESP's power-up chatter gets parsed
#                     as a firmware command by car.c's run_cmd().
#
# Why this matters: in the final wiring the ESP's TXD goes to the MCU's UART RX. The ESP8266 dumps
# a boot log (partly at a different baud -> pure garbage bytes) every time it powers up or browns
# out. car.c's poll_uart()/run_cmd() has NO frame header: it buffers chars and, on CR/LF, treats
# the first char as the command letter. So boot garbage can silently fire 'x'/'y' (direct PWM),
# 'm' (mode change), 't' (target) etc. while the car sits on the ground.
#
# What this does:
#   1. escapes passthrough, issues AT+RST, captures the boot bytes RAW (no encoding mangling)
#   2. replays those exact bytes through a faithful PowerShell copy of poll_uart()/run_cmd()
#   3. reports every command that WOULD have fired, tagged by danger level
#
# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File esp_boot_risk.ps1 -Port COM5
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#   4. -ReplayHex <file> : skip the hardware entirely and replay a previously captured hex dump.
#      Use this to PC-verify a fix without a board: it replays the SAME bytes through BOTH the old
#      (ungated) and the new (CMD_MUTE_MS + cmd_format_ok) parser and diffs what fires.
param(
    [string]$Port     = "COM5",
    [int]$Baud        = 115200,
    [int]$CaptureMs   = 5000,
    [switch]$NoReset,                     # just listen, do not reset (e.g. user power-cycles by hand)
    [string]$ReplayHex = "",              # replay a saved hex dump instead of touching hardware
    [string]$Out      = "esp_boot_risk_out.txt",
    [string]$HexOut   = "esp_boot_bytes.hex"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) { [void]$log.AppendLine($s); Write-Host $s }

$bytes = New-Object System.Collections.Generic.List[byte]
$replay = ($ReplayHex.Length -gt 0)

if ($replay) {
    L "==== esp_boot_risk REPLAY from $ReplayHex (no hardware)  $(Get-Date -Format 'HH:mm:ss') ===="
    if (-not (Test-Path $ReplayHex)) { L "FILE_NOT_FOUND: $ReplayHex"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
    foreach ($ln in (Get-Content -Path $ReplayHex)) {
        # hex dump line looks like:  0000: 41 54 2B ... |AT+...|
        $m = [regex]::Match($ln, '^[0-9A-Fa-f]{4}:\s+((?:[0-9A-Fa-f]{2}\s|\s{3})+)\|')
        if (-not $m.Success) { continue }
        foreach ($tok in ($m.Groups[1].Value -split '\s+')) {
            if ($tok.Length -eq 2) { $bytes.Add([Convert]::ToByte($tok, 16)) }
        }
    }
    L "loaded $($bytes.Count) bytes from hex dump"
}

if (-not $replay) {

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout  = 200
$sp.WriteTimeout = 1000
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false
try { $sp.Open() } catch { L "OPEN_FAIL: $($_.Exception.Message)"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
Start-Sleep -Milliseconds 250

L "==== esp_boot_risk on $Port  $(Get-Date -Format 'HH:mm:ss') ===="

if (-not $NoReset) {
    # leave passthrough so AT+RST is interpreted locally instead of being sent over the air
    Start-Sleep -Milliseconds 1100
    try { $sp.Write("+++") } catch {}
    Start-Sleep -Milliseconds 1100
    try { $sp.Write("`r`n") } catch {}
    Start-Sleep -Milliseconds 400
    try { $sp.DiscardInBuffer() } catch {}
    L "escaped passthrough, issuing AT+RST ..."
    try { $sp.Write("AT+RST`r`n") } catch {}
} else {
    L "NoReset: listening ${CaptureMs}ms - power-cycle the module NOW"
}

# ---- raw byte capture (typed Read() avoids any text decoding) ----
$tmp = New-Object byte[] 4096
$sw = [System.Diagnostics.Stopwatch]::StartNew()
while ($sw.Elapsed.TotalMilliseconds -lt $CaptureMs) {
    $n = 0
    try { $n = $sp.BytesToRead } catch {}
    if ($n -gt 0) {
        if ($n -gt 4096) { $n = 4096 }
        $read = 0
        try { $read = $sp.Read($tmp, 0, $n) } catch {}
        for ($i = 0; $i -lt $read; $i++) { $bytes.Add($tmp[$i]) }
    } else {
        Start-Sleep -Milliseconds 10
    }
}
try { $sp.Close() } catch {}

L "captured $($bytes.Count) bytes"

}   # end if (-not $replay)

# hex dump for the record
$hex = New-Object System.Text.StringBuilder
for ($i = 0; $i -lt $bytes.Count; $i += 16) {
    $line = "{0:X4}: " -f $i
    $asc = ""
    for ($j = 0; $j -lt 16; $j++) {
        if (($i + $j) -lt $bytes.Count) {
            $b = $bytes[$i + $j]
            $line += ("{0:X2} " -f $b)
            if ($b -ge 32 -and $b -le 126) { $asc += [char]$b } else { $asc += "." }
        } else { $line += "   " }
    }
    [void]$hex.AppendLine($line + "|" + $asc + "|")
}
Set-Content -Path $HexOut -Value $hex.ToString() -Encoding ASCII
L "hex dump -> $HexOut"

# ---- faithful replay of car.c poll_uart() / run_cmd() ----
# poll_uart: on '\r' or '\n' -> if clen>0 run_cmd(cbuf); else if clen<15 cbuf[clen++]=ch
# run_cmd  : c = s[0]; switch(c) { m t p i d z f w e x y g ? }
$danger = @{
    'x' = "CRITICAL - direct PWM on M1 (motor spins)"
    'y' = "CRITICAL - direct PWM on M2 (motor spins)"
    'm' = "HIGH     - mode switch (can enter a driving mode)"
    't' = "HIGH     - changes the setpoint"
    'p' = "HIGH     - overwrites Kp"
    'i' = "HIGH     - overwrites Ki"
    'd' = "HIGH     - overwrites Kd"
    'w' = "MED      - changes deadzone feed-forward"
    'e' = "MED      - changes position tolerance"
    'z' = "LOW      - forces IDLE / stops (disruptive, not unsafe)"
    'f' = "LOW      - changes telemetry period"
    'g' = "NONE     - IMU dump"
    '?' = "NONE     - status print"
}

# split into the lines that poll_uart() would hand to run_cmd() (buffer capped at 15 like cbuf[16])
$cbuf = ""
$fired = @()
foreach ($b in $bytes) {
    $ch = [char]$b
    if ($ch -eq "`r" -or $ch -eq "`n") {
        if ($cbuf.Length -gt 0) { $fired += $cbuf ; $cbuf = "" }
    } else {
        if ($cbuf.Length -lt 15) { $cbuf += $ch }
    }
}

# faithful copy of the NEW gate in car.c:
#   static int cmd_format_ok(const char *s, int n)
#   { int i=1; if(n<1) return 0; if(i<n && s[i]=='-') i++;
#     for(; i<n; i++){ if(s[i]<'0'||s[i]>'9') return 0; } return 1; }
function CmdFormatOk([string]$s) {
    $n = $s.Length
    if ($n -lt 1) { return $false }
    $i = 1
    if ($i -lt $n -and $s[$i] -eq '-') { $i++ }
    for (; $i -lt $n; $i++) { if ($s[$i] -lt '0' -or $s[$i] -gt '9') { return $false } }
    return $true
}

L ""
L "---- replay: $($fired.Count) line(s) reach run_cmd()'s doorstep ----"
L "     OLD = no gate (what the firmware did before 2026-07-27)"
L "     NEW = cmd_format_ok() strict format gate + optional '#' prefix"
L ""
$hitsOld = @{}
$hitsNew = @{}
foreach ($f in $fired) {
    $p = $f
    if ($p.StartsWith('#')) { $p = $p.Substring(1) }
    if ($p.Length -lt 1) { continue }
    $c = $p.Substring(0,1)
    $known = $danger.ContainsKey($c)
    $passNew = CmdFormatOk $p

    $oldTag = "-"
    if ($known) {
        $oldTag = $danger[$c]
        if (-not $hitsOld.ContainsKey($c)) { $hitsOld[$c] = 0 }
        $hitsOld[$c] = $hitsOld[$c] + 1
    }
    $newTag = "REJECTED by format gate"
    if ($passNew) {
        $newTag = "PASSES gate"
        if ($known) {
            if (-not $hitsNew.ContainsKey($c)) { $hitsNew[$c] = 0 }
            $hitsNew[$c] = $hitsNew[$c] + 1
        }
    }
    $printable = ($p -replace '[^\x20-\x7E]', '.')
    $mark = "    "
    if ($known -and $passNew) { $mark = "!!! " }
    L ("{0}line=<{1}>" -f $mark, $printable)
    L ("      OLD: {0}" -f $oldTag)
    L ("      NEW: {0}" -f $newTag)
}

L ""
L "================ VERDICT ================"
if ($hitsOld.Count -eq 0) {
    L "OLD parser: no recognised command letter hit in THIS capture."
} else {
    L "OLD parser: $($hitsOld.Count) distinct command letter(s) WOULD HAVE FIRED:"
    foreach ($k in $hitsOld.Keys) { L ("     '{0}' x{1}  -> {2}" -f $k, $hitsOld[$k], $danger[$k]) }
}
if ($hitsNew.Count -eq 0) {
    L "NEW parser: ZERO commands fire - every line above is rejected by the format gate."
} else {
    L "NEW parser: STILL LEAKS $($hitsNew.Count) command letter(s):"
    foreach ($k in $hitsNew.Keys) { L ("     '{0}' x{1}  -> {2}" -f $k, $hitsNew[$k], $danger[$k]) }
}
L ""
L "NOTE: garbage bytes are not deterministic - a clean run does NOT prove safety on its own."
L "      That is why the firmware also has CMD_MUTE_MS (ignore RX for the first 2s after boot),"
L "      and why '#' framing stays available (firmware already accepts an optional '#' prefix)."
L "      Fully immune alternative: one-way wiring (do NOT connect ESP TXD to the MCU RX)."

Set-Content -Path $Out -Value $log.ToString() -Encoding ASCII
