# syscfg_check.ps1 - run this right after editing car.syscfg; catch mistakes BEFORE build/board.
#   Wraps .kiro/skills/mspm0-ccs/scripts/{check_syscfg.py, run_sysconfig.py}
#   (borrowed from mc3545dada/mspm0-skill, MIT), pinned to THIS project's tool + paths.
#   ASCII-only on purpose (Win PowerShell 5.1 mangles UTF-8 .ps1 -> parse errors).
#
# Usage (from car/):
#   .\syscfg_check.ps1              # static check + trial generation into a temp dir (recommended)
#   .\syscfg_check.ps1 -StaticOnly  # static check only (instant, does not call SysConfig CLI)
#   .\syscfg_check.ps1 -Keep        # keep the temp generated output for manual diffing
#
# Why -Tool is pinned: two SysConfig 1.28.0 installs exist here (standalone + the one inside CCS),
#   and car.syscfg declares no @versions -> the upstream script refuses to guess. We pin the one
#   from the SDK's imports.mak (SYSCONFIG_TOOL), i.e. the tool the BUILD actually uses.
#   Validating with a different version would be meaningless.
# Note: generation only writes a temp dir; gcc/ti_msp_dl_config.* is untouched.
#   Real regeneration still happens through mingw32-make.
param(
    [switch]$StaticOnly,
    [switch]$Keep
)
Set-Location $PSScriptRoot
$env:PYTHONIOENCODING = 'utf-8'

$skill   = Join-Path (Resolve-Path "$PSScriptRoot\..\..\..") '.kiro\skills\mspm0-ccs\scripts'
$tool    = 'C:\ti\sysconfig_1.28.0\sysconfig_cli.bat'          # = SDK imports.mak SYSCONFIG_TOOL
$product = 'C:\ti\mspm0-sdk\.metadata\product.json'
$outFile = Join-Path $PSScriptRoot 'syscfg_check_out.txt'

# python stdout through a PowerShell pipe gets mangled (GBK) or silently swallowed
# -> write to a file first, then read it back. (Known pit in this repo.)
"=== check_syscfg.py (static) ===" | Out-File $outFile -Encoding UTF8
python (Join-Path $skill 'check_syscfg.py') $PSScriptRoot 2>&1 | Out-File $outFile -Encoding UTF8 -Append

if (-not $StaticOnly) {
    "" | Out-File $outFile -Encoding UTF8 -Append
    "=== run_sysconfig.py (generate into temp dir) ===" | Out-File $outFile -Encoding UTF8 -Append
    $a = @((Join-Path $skill 'run_sysconfig.py'), $PSScriptRoot, '--tool', $tool, '--product', $product)
    if ($Keep) { $a += '--keep-output' }
    python @a 2>&1 | Out-File $outFile -Encoding UTF8 -Append
}

Get-Content $outFile -Encoding UTF8
Write-Host ""
Write-Host "[syscfg_check] full log -> $outFile" -ForegroundColor Cyan
Write-Host "[syscfg_check] Read the WARNING lines too: a warning-producing generation is NOT 'clean'." -ForegroundColor Yellow
Write-Host "[syscfg_check] Report generation / compile / flash / board behaviour as SEPARATE levels." -ForegroundColor Yellow
