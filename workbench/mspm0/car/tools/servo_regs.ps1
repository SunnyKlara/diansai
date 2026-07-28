# servo_regs.ps1 - read-only dump of the registers that decide "is PA31 actually emitting PWM".
#
# WHY: the servo did not move and "no holding torque" cannot distinguish "no power" from
# "no valid pulse" (H-bridge idle feels the same as unpowered on a metal-gear servo). So
# instead of guessing, read what the MCU side is actually doing. This touches nothing
# (no write, no flash), it only reads memory-mapped registers over SWD.
#
# NOTE: every openocd 'init' resets the chip (SSOT section D) -> the app restarts, runs
# servo_init() again, and (unless a U/S command arrives) leaves the servo at CC=0 (limp).
# So CC1 read here is expected to be 0 right after connect. What we really check:
#   - IOMUX PINCM6  : is PA31 muxed to function 5 (TIMG12_CCP1) and output-enabled?
#   - TIMG12 CTRCTL : is the counter actually enabled (EN bit0)?
#   - TIMG12 LOAD   : is the period 639999 (50Hz)?  proves the timer was configured
#   - TIMG12 CC1    : current compare value (0 = limp right after reset, that's fine)
#   - TIMG12 CTR    : counter value; read twice - if it changes, the counter is RUNNING
#
# ASCII-only by repo rule.

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$proj = Split-Path $here -Parent      # car/  <- 2026-07-28: this script moved into car/tools/
Set-Location $proj
. (Join-Path $proj '_tools.ps1')      # _tools.ps1 stays at the project root, one level up
$openocd = (Find-Openocd).Exe
$scripts = Find-OpenocdScripts

# absolute addresses (from hw_gptimer.h struct offsets + TIMG12_BASE 0x40870000,
# and IOMUX PINCM6 = IOMUX_BASE 0x40428000 + PINCM region). PINCM registers start at
# offset 0x4 with 4 bytes each: PINCM6 = 0x40428000 + 0x4 + (6-1)*4 = 0x40428018.
$addrs = @{
    'IOMUX_PINCM6 (PA31 mux)' = '0x40428018'
    'TIMG12 CTRCTL (bit0=EN)' = '0x40871804'
    'TIMG12 LOAD (period)'    = '0x40871808'
    'TIMG12 CC1 (servo duty)' = '0x40871814'
    'TIMG12 CTR (live count)' = '0x40871800'
    'TIMG12 PWREN (GPRCM)'    = '0x40870800'
}

$ooArgs = @('-s', $scripts, '-f', 'interface/cmsis-dap.cfg', '-f', 'target/ti_mspm0.cfg',
            '-c', 'adapter speed 500', '-c', 'init')
Write-Output '(reading once right after connect, then again after a 500ms settle -'
Write-Output ' the settle read rules out "caught it mid-LCD-init-delay" as a false reading)'
foreach ($a in $addrs.Values) { $ooArgs += @('-c', "mdw $a 1") }
$ooArgs += @('-c', 'sleep 500')
foreach ($a in $addrs.Values) { $ooArgs += @('-c', "mdw $a 1") }
# read CTR a couple more times spaced out, to detect a running counter
$ooArgs += @('-c', 'mdw 0x40871800 1', '-c', 'sleep 50', '-c', 'mdw 0x40871800 1',
             '-c', 'sleep 50', '-c', 'mdw 0x40871800 1', '-c', 'exit')

$log = Join-Path $env:TEMP 'servo_regs.log'
& $openocd @ooArgs *> $log
$raw = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)

# NOTE: several addresses are read TWICE (once immediately, once after a 500ms settle),
# so keep an ORDERED list of (addr,value) pairs, not a dict - a dict would silently keep
# only the last reading and hide exactly the "did it change after settling" signal we want.
$readings = @()
foreach ($line in $raw) {
    if ($line -match '^\s*0x([0-9a-fA-F]{8}):\s*([0-9a-fA-F]{8})') {
        $readings += [pscustomobject]@{ Addr = '0x' + $Matches[1].ToLower(); Val = $Matches[2].ToLower() }
    }
}
function Get-First([string]$addr) { ($readings | Where-Object { $_.Addr -eq $addr } | Select-Object -First 1).Val }
function Get-Last([string]$addr)  { ($readings | Where-Object { $_.Addr -eq $addr } | Select-Object -Last 1).Val }

Write-Output ''
Write-Output 'register                    addr        immediately   after 500ms   changed?'
Write-Output '----------------------------------------------------------------------------------'
foreach ($k in $addrs.Keys) {
    $a  = $addrs[$k].ToLower()
    $v1 = Get-First $a
    $v2 = Get-Last  $a
    if (-not $v1) { $v1 = '????????' }
    if (-not $v2) { $v2 = '????????' }
    $chg = if ($v1 -ne $v2) { 'YES' } else { '' }
    Write-Output ('{0,-26} {1}  0x{2}      0x{3}      {4}' -f $k, $addrs[$k], $v1, $v2, $chg)
}

Write-Output ''
# use the SETTLED (second) reading for the verdicts below - that is the steady-state value
$ctrctl = Get-Last '0x40871804'
$load   = Get-Last '0x40871808'
$pincm  = Get-Last '0x40428018'
if ($ctrctl) {
    $en = [Convert]::ToUInt32($ctrctl, 16) -band 1
    Write-Output ("CTRCTL EN bit = {0}  ({1})" -f $en, $(if ($en) { 'counter ENABLED' } else { 'counter DISABLED - servo_init/startCounter did NOT take' }))
}
if ($load) {
    $lv = [Convert]::ToUInt32($load, 16)
    Write-Output ("LOAD = {0} (expect 639999 for 50Hz; period=LOAD+1)" -f $lv)
}
if ($pincm) {
    $pf = [Convert]::ToUInt32($pincm, 16) -band 0x3f
    $pc = ([Convert]::ToUInt32($pincm, 16) -shr 7) -band 1   # PC = connected/input-enable-ish
    Write-Output ("PINCM6 PF(func) = {0} (expect 5 = TIMG12_CCP1), raw=0x{1}" -f $pf, $pincm)
}
Write-Output ''
Write-Output 'CTR sampled 5 times total (2 above + 3 more, ~50ms apart) - if these values'
Write-Output 'differ from each other, the counter is actually RUNNING (not just enabled):'
$ctrHits = @($readings | Where-Object { $_.Addr -eq '0x40871800' } | ForEach-Object { $_.Val })
$ctrHits | ForEach-Object { Write-Output ('  0x40871800: 0x' + $_) }
$distinct = ($ctrHits | Select-Object -Unique).Count
if ($distinct -gt 1) { Write-Output "  => CTR value changed across reads ($distinct distinct values) - counter IS running." }
elseif ($ctrHits.Count -gt 0) { Write-Output '  => CTR value never changed - counter is NOT running (or LOAD wrapped exactly, unlikely).' }
exit 0
