# sdk_find.ps1 - before adding a peripheral, look up how the OFFICIAL examples configure it.
#   Do not guess DriverLib APIs / SysConfig fields from memory (known pit in this repo).
#   Wraps .kiro/skills/mspm0-ccs/scripts/index_syscfg_examples.py
#   (borrowed from mc3545dada/mspm0-skill, MIT): indexes the ~1765 .syscfg files in the local
#   MSPM0 SDK by board + module, and lists the .meta/*.syscfg.js field source of truth.
#   ASCII-only on purpose (Win PowerShell 5.1 mangles UTF-8 .ps1 -> parse errors).
#
# Usage (from car/):
#   .\sdk_find.ps1 ADC12                 # G3507 examples using ADC12
#   .\sdk_find.ps1 ADC12,TIMER           # examples using several modules
#   .\sdk_find.ps1 DMA -AllBoards        # any board (use another board's example as reference)
#   .\sdk_find.ps1 ADC12 -Grep trigger   # only lines whose path/modules match a keyword
#
# Useful hits found on 2026-07-27:
#   current-loop prerequisite (ADC sampling phase-locked to PWM):
#       driverlib\adc12_triggered_by_timer_event
#       driverlib\adc12_simultaneous_trigger_event
#   full motor-control reference (ADC-PWM sync + current loop):
#       motor_control_pmsm_sensorless_foc\*
#   hardware quadrature decode (we use software sampling decode - better under motor EMI,
#       but worth comparing): search QEI
param(
    [Parameter(Position = 0, Mandatory = $true)]
    [string]$Module,
    [switch]$AllBoards,
    [string]$Grep = '',
    [int]$Limit = 60,
    [string]$SdkRoot = 'C:\ti\mspm0-sdk'
)
Set-Location $PSScriptRoot
$env:PYTHONIOENCODING = 'utf-8'

$script  = Join-Path (Resolve-Path "$PSScriptRoot\..\..\..") '.kiro\skills\mspm0-ccs\scripts\index_syscfg_examples.py'
$outFile = Join-Path $PSScriptRoot 'sdk_find_out.txt'

$a = @($script, $SdkRoot, '--module', $Module, '--limit', $Limit)
if (-not $AllBoards) { $a += @('--board', 'LP_MSPM0G3507') }   # Tianmengxing = G3507; LP examples apply

# write to a file first: python output through a PowerShell pipe gets mangled or swallowed
python @a 2>&1 | Out-File $outFile -Encoding UTF8

if ($Grep) {
    Get-Content $outFile -Encoding UTF8 | Select-String -Pattern $Grep | ForEach-Object { $_.Line.Trim() }
} else {
    Get-Content $outFile -Encoding UTF8
}
Write-Host ""
Write-Host "[sdk_find] full log -> $outFile" -ForegroundColor Cyan
Write-Host "[sdk_find] Next: copy fields from that example's .syscfg, copy the DriverLib call order" -ForegroundColor Yellow
Write-Host "[sdk_find] from its .c, then check YOUR generated ti_msp_dl_config.h for the real macro names." -ForegroundColor Yellow
