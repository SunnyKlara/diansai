# esp_fake_mcu.ps1 - MCU EMULATOR on a serial port. Pretends to be the car's car.c firmware so the
#                    whole wireless toolchain can be verified end-to-end WITHOUT the MSPM0 board.
#
# What it emulates (faithfully enough for the PC-side tools):
#   * telemetry line, exact car.c format:
#       [ctl] <MODE> tgt=<t> | I:a,b | V:a,b | PWM:a,b | C:a,b | D:a,b | Y:<yaw*10> W:<w*100>
#   * command set: m<n> t<v> p/i/d<milli> f<ms> w<v> e<v> x/y<v> z ?
#   * the 2026-07-27 command hardening that is NOT yet on real silicon:
#       - power-on mute window (CMD_MUTE_MS): RX drained but not parsed
#       - strict format gate (cmd_format_ok): only "<letter>[-][digits]", optional '#' prefix
#       - rej=<n> counter surfaced in the status line
#   * a first-order plant whose speed of response scales with Kp, so a gain command that actually
#     arrived is VISIBLE in the captured step (otherwise the test could pass on a dead link).
#
# Why this exists: it lets `tune_step.ps1` / `read_serial.ps1` / `uart_send.ps1` run against a real
# serial port over the ESP-01S bridge, which verifies the things a one-way byte dump cannot:
#   full duplex (commands while telemetry streams), ReadLine() line integrity across the air,
#   and that the new format gate does not break the existing scripts.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File esp_fake_mcu.ps1 -Port COM5 -Seconds 60 -NoMute
#   -NoMute  : behave like an MCU that has been running a while (skip the power-on mute window)
#   -Legacy  : disable the gate, i.e. emulate the OLD firmware (to A/B the hardening)
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
param(
    [string]$Port    = "COM5",
    [int]$Baud       = 115200,
    [int]$Seconds    = 60,
    [int]$MuteMs     = 2000,
    [switch]$NoMute,
    [switch]$Legacy,
    [switch]$Quiet,
    [string]$Out     = "esp_fake_mcu_out.txt"
)

$log = New-Object System.Text.StringBuilder
function L([string]$s) {
    [void]$log.AppendLine($s)
    if (-not $Quiet) { Write-Host $s }
}

$sp = New-Object System.IO.Ports.SerialPort $Port, $Baud, None, 8, one
$sp.ReadTimeout  = 50
$sp.WriteTimeout = 1000
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false
try { $sp.Open() } catch { L "OPEN_FAIL: $($_.Exception.Message)"; Set-Content $Out $log.ToString() -Encoding ASCII; exit 1 }
Start-Sleep -Milliseconds 200

# ---------------- emulated firmware state ----------------
$modeName = @("IDLE","CURR","SPEED","POS","OPEN","DUAL","DIFFO","DIFFC")
$mode     = 0
$target   = 0
$kp = @(0.0, 0.0, 0.150, 0.200)     # [curr, spare, speed, pos]  (speed Kp0.15 / pos Kp0.20 per SSOT)
$ki = @(0.0, 0.0, 0.020, 0.000)
$kd = @(0.0, 0.0, 0.000, 0.050)
$printMs = 100
$pwmDz   = 12
$posTol  = 15
$rej     = 0
$accepted = 0

# plant
$v1 = 0.0; $v2 = 0.0
$c1 = 0;   $c2 = 0
$pwm1 = 0; $pwm2 = 0
$yaw = 0.0

function LoopIndex($m) {
    if ($m -eq 1) { return 0 }
    if ($m -eq 2) { return 2 }
    if ($m -eq 3) { return 3 }
    return -1
}

# faithful copy of car.c cmd_format_ok(): "<letter>[-][digits]" only
function CmdFormatOk([string]$s) {
    $n = $s.Length
    if ($n -lt 1) { return $false }
    $i = 1
    if ($i -lt $n -and $s[$i] -eq '-') { $i++ }
    for (; $i -lt $n; $i++) { if ($s[$i] -lt '0' -or $s[$i] -gt '9') { return $false } }
    return $true
}
# faithful copy of car.c parse_int(): stops at first non-digit
function ParseInt([string]$s) {
    $sg = 1; $i = 0; $v = 0
    if ($i -lt $s.Length -and $s[$i] -eq '-') { $sg = -1; $i++ }
    for (; $i -lt $s.Length; $i++) {
        if ($s[$i] -lt '0' -or $s[$i] -gt '9') { break }
        $v = $v * 10 + ([int][char]$s[$i] - 48)
    }
    return $sg * $v
}

function Send-Line([string]$s) { try { $sp.Write($s + "`n") } catch {} }

function Print-Status {
    $li = LoopIndex $mode
    $s = "[ctl] mode=" + $modeName[$mode] + " tgt=" + $target
    if ($li -ge 0) {
        $s += " Kp*1e3=" + [int]($script:kp[$li] * 1000)
        $s += " Ki*1e3=" + [int]($script:ki[$li] * 1000)
        $s += " Kd*1e3=" + [int]($script:kd[$li] * 1000)
    }
    $s += " (PWM_CAP=60%) rej=" + $script:rej
    Send-Line $s
}

function Run-Cmd([string]$s) {
    $c = $s.Substring(0,1)
    $v = ParseInt $s.Substring(1)
    $li = LoopIndex $script:mode
    switch ($c) {
        'm' { if ($v -ge 0 -and $v -lt 8) { $script:mode = $v; $script:target = 0; $script:v1 = 0.0; $script:v2 = 0.0 } }
        't' { $script:target = $v }
        'p' { if ($li -ge 0) { $script:kp[$li] = $v / 1000.0 } }
        'i' { if ($li -ge 0) { $script:ki[$li] = $v / 1000.0 } }
        'd' { if ($li -ge 0) { $script:kd[$li] = $v / 1000.0 } }
        'z' { $script:mode = 0; $script:target = 0 }
        'f' { if ($v -ge 5 -and $v -le 2000) { $script:printMs = $v } }
        'w' { if ($v -ge 0 -and $v -le 30)  { $script:pwmDz = $v } }
        'e' { if ($v -ge 0 -and $v -le 300) { $script:posTol = $v } }
        'x' { }
        'y' { }
        '?' { }
        default { }
    }
    $script:accepted++
    Print-Status
}

# ---------------- main loop ----------------
$muteUntil = 0
if (-not $NoMute) { $muteUntil = $MuteMs }
$gateOn = (-not $Legacy)

L "==== fake MCU on $Port @ $Baud for ${Seconds}s   $(Get-Date -Format 'HH:mm:ss') ===="
if ($gateOn) { L "gate: ON  (strict format + mute ${muteUntil}ms)  <- emulates the 2026-07-27 car.c hardening" }
else         { L "gate: OFF (legacy behaviour: any line, first char = command)" }
L ""

$cbuf = ""
$sw = [System.Diagnostics.Stopwatch]::StartNew()
$lastPrint = 0.0
$lastPlant = 0.0
$cmdSeen = @()

while ($sw.Elapsed.TotalSeconds -lt $Seconds) {
    $nowMs = $sw.Elapsed.TotalMilliseconds

    # ---- RX ----
    $chunk = ""
    try { $chunk = $sp.ReadExisting() } catch {}
    if ($chunk.Length -gt 0) {
        foreach ($ch in $chunk.ToCharArray()) {
            if ($gateOn -and $nowMs -lt $muteUntil) { $cbuf = ""; continue }   # drained, not parsed
            if ($ch -eq "`r" -or $ch -eq "`n") {
                if ($cbuf.Length -gt 0) {
                    $p = $cbuf
                    if ($p.StartsWith('#')) { $p = $p.Substring(1) }
                    $ok = $true
                    if ($gateOn) { $ok = ($p.Length -gt 0) -and (CmdFormatOk $p) }
                    if ($ok -and $p.Length -gt 0) {
                        $cmdSeen += ("{0,6:N0}ms  ACCEPT  <{1}>" -f $nowMs, $p)
                        Run-Cmd $p
                    } else {
                        $rej++
                        $cmdSeen += ("{0,6:N0}ms  REJECT  <{1}>" -f $nowMs, ($p -replace '[^\x20-\x7E]','.'))
                    }
                    $cbuf = ""
                }
            } elseif ($cbuf.Length -lt 15) {
                $cbuf += $ch
            }
        }
    }

    # ---- plant (every ~20ms) ----
    if (($nowMs - $lastPlant) -ge 20) {
        $lastPlant = $nowMs
        $li = LoopIndex $mode
        $k = 0.0
        if ($li -ge 0) { $k = $kp[$li] * 0.5 }
        if ($k -gt 0.9) { $k = 0.9 }
        $tgt = 0.0
        if ($mode -ne 0) { $tgt = [double]$target }
        $e1 = $tgt - $v1
        $e2 = $tgt - $v2
        $v1 = $v1 + $e1 * $k + (Get-Random -Minimum -15 -Maximum 16) / 10.0
        $v2 = $v2 + $e2 * $k * 0.92 + (Get-Random -Minimum -15 -Maximum 16) / 10.0
        $pwm1 = [int]([Math]::Max(-60, [Math]::Min(60, $e1 * $k * 3)))
        $pwm2 = [int]([Math]::Max(-60, [Math]::Min(60, $e2 * $k * 3)))
        $c1 = $c1 + [int]($v1 * 0.27)
        $c2 = $c2 + [int]($v2 * 0.27)
        $yaw = $yaw + ($v1 - $v2) * 0.0009
    }

    # ---- telemetry ----
    if (($nowMs - $lastPrint) -ge $printMs) {
        $lastPrint = $nowMs
        $line = "[ctl] " + $modeName[$mode] + " tgt=" + $target +
                " | I:" + [int]($pwm1 * 8) + "," + [int]($pwm2 * 8) +
                " | V:" + [int]$v1 + "," + [int]$v2 +
                " | PWM:" + $pwm1 + "," + $pwm2 +
                " | C:" + $c1 + "," + $c2 +
                " | D:0,0" +
                " | Y:" + [int]($yaw * 10) + " W:" + [int](($v1 - $v2) * 0.9)
        Send-Line $line
    }
}

L "---- commands the emulated MCU saw ----"
if ($cmdSeen.Count -eq 0) { L "  (none)" } else { foreach ($e in $cmdSeen) { L "  $e" } }
L ""
$liEnd = LoopIndex $mode
$kpEnd = "n/a"
if ($liEnd -ge 0) { $kpEnd = [int]($kp[$liEnd] * 1000) }   # guard: PS treats $kp[-1] as the LAST element
L "accepted=$accepted  rejected=$rej   final: mode=$($modeName[$mode]) tgt=$target Kp*1e3=$kpEnd"
Set-Content -Path $Out -Value $log.ToString() -Encoding ASCII
try { $sp.Close() } catch {}
