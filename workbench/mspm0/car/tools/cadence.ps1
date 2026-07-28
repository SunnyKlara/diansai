# cadence.ps1 - print the task cadence (scheduling) table STRAIGHT FROM config.h + car.syscfg,
#               and check the consistency traps that would silently invalidate it.
#
# WHY THIS IS A SCRIPT AND NOT A DOCUMENT:
#   The defense/report needs an answer to "what is your control period, and why".  If that table
#   is typed into a .md it becomes a stale copy the moment somebody tunes config.h - exactly the
#   rot this repo has been bitten by (ENC_CPR=899, Kp0.03, "remaining days").  So the numbers are
#   PARSED, never typed.  Run it right before writing the report / walking into the defense.
#
#   .\tools\cadence.ps1            full table + checks
#   .\tools\cadence.ps1 -Quiet     only the RESULT line
#
# Exit codes: 0 = PASS   1 = FAIL (a real inconsistency, see below)   2 = INCONCLUSIVE (cannot parse)
#
# The four checks are not decoration - each one is a known silent-failure mode:
#   C1 ST_HZ % 1000 == 0      ST_PER_MS is an INTEGER divide (config.h:26).  ST_HZ=4500 truncates
#                             5000/1000 -> 4, and then EVERY ms-based period, timeout gate and dt
#                             conversion is off by 12.5% with no error anywhere.
#   C2 MOTOR_PWM_PERIOD       must equal car.syscfg PWM1.timerCount, else the "20kHz" you claim in
#                             the report is not the frequency on the pin.
#   C3 SERVO_PWM_PERIOD       same for PWM3 (50Hz servo).  A servo at the wrong frame rate just
#                             twitches, which looks like a broken servo, not a config mismatch.
#   C4 CFG_IMU_MS <= SPEED_MS < POS_MS
#                             cascade bandwidth ordering.  If the outer loop runs at or above the
#                             inner loop rate the cascade fights itself and reads as "PID won't tune".
#
# ASCII only (Windows PowerShell 5.1 reads .ps1 as ANSI).

param([switch]$Quiet)

$car = Split-Path -Parent $PSScriptRoot
$cfgPath = Join-Path $car 'config.h'
$sysPath = Join-Path $car 'car.syscfg'

$inconclusive = 0
$fail = 0
$notes = @()

function Read-TextUtf8 {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    return [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8)
}

$cfg = Read-TextUtf8 $cfgPath
$sys = Read-TextUtf8 $sysPath
if (-not $cfg) { Write-Host "RESULT: INCONCLUSIVE - cannot read $cfgPath"; exit 2 }

# --- parse "#define NAME value" (value = up to end of line or start of a comment) ---
function Get-Macro {
    param([string]$Name)
    $m = [regex]::Match($cfg, ('(?m)^\s*#define\s+' + [regex]::Escape($Name) + '\s+([^\r\n]+)'))
    if (-not $m.Success) { $script:inconclusive++; $script:notes += "macro not found: $Name"; return $null }
    $v = $m.Groups[1].Value
    $v = ($v -split '/\*')[0]
    $v = ($v -split '//')[0]
    $v = $v.Trim().Trim('(', ')').Trim()
    $v = [regex]::Replace($v, '(?i)(ul|u|l|f)$', '')
    $d = 0.0
    if (-not [double]::TryParse($v, [Globalization.NumberStyles]::Float, [Globalization.CultureInfo]::InvariantCulture, [ref]$d)) {
        $script:inconclusive++; $script:notes += "macro not numeric: $Name = '$v'"; return $null
    }
    return $d
}

function Get-SyscfgCount {
    param([string]$Inst)
    if (-not $sys) { return $null }
    $m = [regex]::Match($sys, ('(?m)^\s*' + [regex]::Escape($Inst) + '\.timerCount\s*=\s*(\d+)'))
    if (-not $m.Success) { $script:inconclusive++; $script:notes += "syscfg timerCount not found: $Inst"; return $null }
    return [double]$m.Groups[1].Value
}

$CPUCLK   = Get-Macro 'CPUCLK_HZ'
$ST_HZ    = Get-Macro 'ST_HZ'
$IMU_MS   = Get-Macro 'CFG_IMU_MS'
$SPEED_MS = Get-Macro 'SPEED_MS'
$POS_MS   = Get-Macro 'POS_MS'
$PRINT_MS = Get-Macro 'PRINT_MS'
$DISP_MS  = Get-Macro 'DISP_MS'
$MPWM     = Get-Macro 'MOTOR_PWM_PERIOD'
$SPWM     = Get-Macro 'SERVO_PWM_PERIOD'
$CUR_N    = Get-Macro 'CUR_AVG_N'
$CAL_N    = Get-Macro 'CFG_IMU_CAL_N'
$HARDCAP  = Get-Macro 'CFG_RUN_MS_HARDCAP'
$ENC_CPR  = Get-Macro 'ENC_CPR'
$PWM_CAP  = Get-Macro 'PWM_CAP'

$pwm1 = Get-SyscfgCount 'PWM1'
$pwm3 = Get-SyscfgCount 'PWM3'

if ($inconclusive -gt 0) {
    Write-Host ""
    foreach ($n in $notes) { Write-Host "  ! $n" -ForegroundColor Yellow }
    Write-Host "RESULT: INCONCLUSIVE - could not parse $inconclusive item(s); table would be wrong" -ForegroundColor Yellow
    exit 2
}

function Hz { param([double]$ms) if ($ms -le 0) { return 0 } else { return 1000.0 / $ms } }

$rows = @()
function Row {
    param([string]$Task, [string]$Where, [string]$Period, [string]$Rate, [string]$Macro)
    $script:rows += (New-Object psobject -Property ([ordered]@{
        Task = $Task; RunsIn = $Where; Period = $Period; Rate = $Rate; Macro = $Macro }))
}

$stUs = 1000000.0 / $ST_HZ
Row 'encoder 4x quadrature sample' 'SysTick ISR' ("{0:0.#} us" -f $stUs) ("{0:0} Hz" -f $ST_HZ) 'ST_HZ'
Row 'time base counter g_st++'     'SysTick ISR' ("{0:0.#} us" -f $stUs) ("{0:0} Hz" -f $ST_HZ) 'ST_HZ'
Row 'IMU read + attitude/yaw integrate' 'main loop' ("{0:0} ms" -f $IMU_MS)   ("{0:0.#} Hz" -f (Hz $IMU_MS))   'CFG_IMU_MS'
Row 'speed measure window + speed PI'   'main loop' ("{0:0} ms" -f $SPEED_MS) ("{0:0.#} Hz" -f (Hz $SPEED_MS)) 'SPEED_MS'
Row 'nav (m8/m9) + visual servo (m10)'  'main loop' ("{0:0} ms" -f $SPEED_MS) ("{0:0.#} Hz" -f (Hz $SPEED_MS)) 'SPEED_MS (shares speed tick)'
Row 'position outer loop (PD -> rpm)'   'main loop' ("{0:0} ms" -f $POS_MS)   ("{0:0.#} Hz" -f (Hz $POS_MS))   'POS_MS'
Row 'telemetry line'                    'main loop' ("{0:0} ms" -f $PRINT_MS) ("{0:0.#} Hz" -f (Hz $PRINT_MS)) 'PRINT_MS (runtime: f<ms>)'
Row 'LCD refresh'                       'main loop' ("{0:0} ms" -f $DISP_MS)  ("{0:0.#} Hz" -f (Hz $DISP_MS))  'DISP_MS'
Row 'motor PWM carrier'                 'TIMA0 hw'  ("{0:0} counts" -f $MPWM) ("{0:0.###} kHz" -f ($CPUCLK / $MPWM / 1000.0)) 'MOTOR_PWM_PERIOD'
Row 'servo PWM frame'                   'TIMG12 hw' ("{0:0} counts" -f $SPWM) ("{0:0.###} Hz" -f ($CPUCLK / $SPWM))          'SERVO_PWM_PERIOD'

if (-not $Quiet) {
    Write-Host ""
    Write-Host "==== task cadence (parsed from config.h / car.syscfg) ====" -ForegroundColor Cyan
    Write-Host ("cpu {0:0.###} MHz   source: {1}" -f ($CPUCLK / 1e6), $cfgPath) -ForegroundColor Gray
    Write-Host ""
    $rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
    Write-Host "derived facts you will be asked about:" -ForegroundColor Cyan
    Write-Host ("  dt is measured, not assumed: every ms-period above is compared against the live")
    Write-Host ("  SysTick counter, so a stalled main loop shifts nothing (see car.c speed window).")
    Write-Host ("  encoder ticks per output rev (4x)  : {0}" -f $ENC_CPR)
    Write-Host ("  counts per speed window at 100 rpm : {0:0.#}  (quantisation floor {1:0.##} rpm)" -f `
        ($ENC_CPR * 100.0 / 60.0 * ($SPEED_MS / 1000.0)), (60000.0 / ($ENC_CPR * $SPEED_MS)))
    Write-Host ("  current sample averaging           : {0} samples/read" -f $CUR_N)
    Write-Host ("  gyro bias calibration ('k')        : {0} samples @ {1:0.#} Hz = {2:0.##} s still" -f `
        $CAL_N, (Hz $IMU_MS), ($CAL_N * $IMU_MS / 1000.0))
    Write-Host ("  motion hard cap                    : {0:0.#} s from mode entry (no command extends it)" -f ($HARDCAP / 1000.0))
    Write-Host ("  pwm duty ceiling                   : {0}%" -f $PWM_CAP)
    Write-Host ""
}

# ---------------- consistency checks ----------------
$checks = @()
function Check {
    param([string]$Id, [string]$What, [bool]$Ok, [string]$Detail)
    if (-not $Ok) { $script:fail++ }
    $script:checks += (New-Object psobject -Property ([ordered]@{
        Id = $Id; Check = $What; Status = $(if ($Ok) { 'OK' } else { 'FAIL' }); Detail = $Detail }))
}

Check 'C1' 'ST_HZ is a multiple of 1000' (($ST_HZ % 1000) -eq 0) `
    ("ST_HZ={0} -> ST_PER_MS={1} (integer divide)" -f $ST_HZ, [math]::Floor($ST_HZ / 1000))
Check 'C2' 'MOTOR_PWM_PERIOD == PWM1.timerCount' ($MPWM -eq $pwm1) `
    ("config.h={0} syscfg={1}" -f $MPWM, $pwm1)
Check 'C3' 'SERVO_PWM_PERIOD == PWM3.timerCount' ($SPWM -eq $pwm3) `
    ("config.h={0} syscfg={1}" -f $SPWM, $pwm3)
Check 'C4' 'cascade ordering IMU <= SPEED < POS' (($IMU_MS -le $SPEED_MS) -and ($SPEED_MS -lt $POS_MS)) `
    ("{0} <= {1} < {2} ms" -f $IMU_MS, $SPEED_MS, $POS_MS)

if (-not $Quiet) {
    $checks | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
}

if ($fail -gt 0) {
    Write-Host ("RESULT: FAIL - {0} consistency check(s) failed; fix before quoting these numbers" -f $fail) -ForegroundColor Red
    exit 1
}
Write-Host "RESULT: PASS" -ForegroundColor Green
exit 0
