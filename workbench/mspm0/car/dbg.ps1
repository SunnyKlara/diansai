# dbg.ps1 - headless SWD debug for Tianmengxing MSPM0G3507 (AI-drivable, no IDE / no F5).
#   Wraps .kiro/skills/mspm0-ccs/scripts/openocd_debug.py (MIT, mc3545dada/mspm0-skill)
#   with THIS project's verified paths: xpack openocd + target/ti_mspm0.cfg + gcc/car.out.
#   ASCII-only on purpose (Win PowerShell 5.1 mangles UTF-8 in .ps1).
#
# WHY this exists: until now the AI loop was "flash + printf over UART" only. When the
#   firmware hangs / faults / never reaches main, printf tells you nothing. This halts the
#   core over SWD and reads PC/SP/LR, or breakpoints a symbol - WITHOUT reflashing.
#
# Usage (run from this folder):
#   .\dbg.ps1 probe            # connect, halt, print target state, resume   (non-destructive)
#   .\dbg.ps1 registers        # halt, print PC/SP/LR/xPSR, resume          <- hang/fault triage
#   .\dbg.ps1 run-to-symbol    # reset + break at main (default), print regs + backtrace
#   .\dbg.ps1 run-to-symbol -Symbol encoder_poll
#   .\dbg.ps1 flash            # flash gcc/car.out + verify + reset run (flash.ps1 still fine)
#   .\dbg.ps1 reset            # reset run   (NOTE: MSPM0 soft reset often will NOT launch the
#                              #  app - press the physical RST button, see 编译烧录操作手册 §8)
#
# SAFETY (control hardware):
#   probe/registers HALT the CPU for ~1s -> control loops stop, PWM freezes at its last duty
#   (motor keeps whatever duty it had!), then resume. run-to-symbol RESETS (motors stop).
#   Before halting a spinning motor: send 'z' (stop) over UART first, or accept the freeze.
#   Never run this while another openocd/flash session is live - one operation per probe.
#
# BRICK POLICY: this helper never mass-erases / factory-resets. If it reports
#   target_locked_or_protected -> stop, use unbrick_flash.ps1 manually (needs RST tapping).
param(
    [Parameter(Position = 0)]
    [ValidateSet('probe', 'registers', 'flash', 'run-to-symbol', 'run', 'reset')]
    [string]$Action = 'probe',
    [string]$Symbol = 'main',
    # 500 kHz first: the only speed this board+wiring is verified at (flash.ps1 uses 500).
    [string]$Speeds = '500,1000',
    [int]$TimeoutSec = 30,
    [switch]$NoVerify
)

$ErrorActionPreference = 'Continue'
Set-Location $PSScriptRoot

. (Join-Path $PSScriptRoot '_tools.ps1')   # path resolution lives in ONE place - see _tools.ps1 header
$ocd     = Find-Openocd
$oocd    = $ocd.Exe
$scripts = $ocd.Scripts
$gdb     = Find-ArmTool 'arm-none-eabi-gdb'
$helper  = Join-Path (Resolve-Path "$PSScriptRoot\..\..\..") '.kiro\skills\mspm0-ccs\scripts\openocd_debug.py'
$program = Join-Path $PSScriptRoot 'gcc\car.out'

foreach ($p in @($oocd, $helper)) {
    if (-not (Test-Path $p)) { Write-Host "MISSING: $p" -ForegroundColor Red; exit 1 }
}
# openocd must find interface/cmsis-dap.cfg + target/ti_mspm0.cfg; xpack keeps them outside
# the layout the helper probes, so hand it over via env (openocd honours OPENOCD_SCRIPTS).
$env:OPENOCD_SCRIPTS = $scripts
$env:PYTHONIOENCODING = 'utf-8'

$pyArgs = @(
    $helper, $PSScriptRoot, '--openocd', $oocd,
    '--target', 'target/ti_mspm0.cfg',          # ours is ti_mspm0.cfg, NOT ti/mspm0.cfg
    '--speeds', $Speeds,
    '--process-timeout', $TimeoutSec,
    $Action
)
switch ($Action) {
    'flash' {
        $pyArgs += @('--program', $program)
        if ($NoVerify) { $pyArgs += '--no-verify' }
    }
    'run-to-symbol' {
        $pyArgs += @('--program', $program, '--symbol', $Symbol, '--gdb', $gdb)
    }
}
if ($Action -in @('probe', 'registers')) {
    Write-Host '[dbg] halting the core for ~1s - PWM freezes at its last duty, then resumes.' -ForegroundColor Yellow
}

python @pyArgs
$rc = $LASTEXITCODE
Write-Host "`n[dbg] $Action exit=$rc  (JSON lines above: attempt / attempt_result / completed|failed carry the diagnosis)" -ForegroundColor Cyan
exit $rc
