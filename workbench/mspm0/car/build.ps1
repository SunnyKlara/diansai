# build.ps1 - compile the car firmware ONLY. Never flashes.
#
# WHY THIS EXISTS
#   The one-click bat (build+flash) is a single piece with no way to stop after the build, so every
#   "does it still compile?" check used to cost a flash cycle. That collides head-on with the
#   hard rule of this board: never flash more often than necessary (repeated/interrupted flashing
#   is what drove this MCU into a double-fault lockup once).
#
# WHY IT REPORTS car.out's TIMESTAMP
#   The SysConfig code generator prints ~15 lines before the compiler runs, so on a small rebuild
#   the actual "Building car.obj / linking car.out" lines scroll off and the tail looks like
#   nothing happened. Judge by whether car.out was RE-LINKED, not by the tail of the log.
#   (Got fooled by exactly this on 2026-07-27 and briefly believed a config.h edit had not been
#   picked up.)
#
# WHY IT PRINTS text+data
#   That sum is the `wrote N bytes` figure flash.ps1 must report. It changes on every edit, so it
#   is computed here rather than memorised anywhere.
#
# Tool paths come from _tools.ps1 (single resolution point). Nonexistent candidate roots make its
# globbing noisy, so resolution runs with errors silenced - a genuinely missing tool still throws.
#
# ASCII only on purpose (Windows PowerShell 5.1 mangles non-ASCII in script files).
#
# Usage: powershell -File build.ps1            # normal build
#        powershell -File build.ps1 -Touch     # force car.obj to rebuild first

param(
    [switch]$Touch,      # force a rebuild of car.obj (proves the config.h dependency works)
    [switch]$Quiet
)

$ErrorActionPreference = 'Continue'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $here
try {
    Write-Host ("================ build  " + (Get-Date -Format 'HH:mm:ss') + " ================")

    $ErrorActionPreference = 'SilentlyContinue'
    . (Join-Path $here '_tools.ps1')
    $armBin  = Find-ArmBin
    $mkBin   = Find-MakeBin
    $sdk     = Find-SdkRoot
    $syscfg  = Find-SysConfigCli
    $sizeExe = Find-ArmTool arm-none-eabi-size
    $ErrorActionPreference = 'Continue'

    if (-not $armBin -or -not $mkBin -or -not $sdk -or -not $syscfg) {
        Write-Host "[X] tool resolution failed - run env_check.ps1 to see which one"
        exit 1
    }
    $armRoot = Split-Path $armBin -Parent
    $mkExe = if (Test-Path (Join-Path $mkBin 'make.exe')) { Join-Path $mkBin 'make.exe' } else { Join-Path $mkBin 'mingw32-make.exe' }
    if (-not (Test-Path $mkExe)) { Write-Host "[X] no make.exe / mingw32-make.exe under $mkBin"; exit 1 }

    if (-not $Quiet) {
        Write-Host ("  arm  : $armRoot")
        Write-Host ("  make : $mkExe")
        Write-Host ("  sdk  : $sdk")
        Write-Host ("  scfg : $syscfg")
    }

    # GitHub copies of the SDK ship imports.mak.windows only; the .bat does the same fixup.
    $imports = Join-Path $sdk 'imports.mak'
    if (-not (Test-Path $imports)) {
        $win = Join-Path $sdk 'imports.mak.windows'
        if (Test-Path $win) { Copy-Item $win $imports -Force; Write-Host "  (created SDK imports.mak from imports.mak.windows)" }
        else { Write-Host "[X] neither imports.mak nor imports.mak.windows in $sdk"; exit 1 }
    }

    $out = Join-Path $here 'gcc\car.out'
    $obj = Join-Path $here 'gcc\car.obj'
    $before = if (Test-Path $out) { (Get-Item $out).LastWriteTime } else { [datetime]::MinValue }

    if ($Touch) {
        if (Test-Path $obj) { Remove-Item $obj -Force; Write-Host "  -Touch: deleted gcc\car.obj (never use 'make clean' - it removes SDK startup sources)" }
    }

    $env:PATH = "$armRoot\bin;$env:PATH"
    Push-Location (Join-Path $here 'gcc')
    $log = & $mkExe MSPM0_SDK_INSTALL_DIR="$($sdk -replace '\\','/')" GCC_ARMCOMPILER="$($armRoot -replace '\\','/')" SYSCONFIG_TOOL="$syscfg" 2>&1 | Out-String
    $code = $LASTEXITCODE
    Pop-Location

    Write-Host ""
    Write-Host "---- compiler / linker lines (SysConfig chatter filtered out) ----"
    $interesting = @(($log -split "`r?`n") | Where-Object { $_ -match 'Building|linking|[Ee]rror|[Ww]arning|Nothing to be done' })
    if ($interesting.Count -eq 0) { Write-Host "  (none - nothing needed rebuilding)" }
    else { $interesting | ForEach-Object { Write-Host ("  " + $_.Trim()) } }

    $after = if (Test-Path $out) { (Get-Item $out).LastWriteTime } else { [datetime]::MinValue }
    $relinked = ($after -ne $before)
    Write-Host ""
    Write-Host ("make exit code   : {0}" -f $code)
    Write-Host ("car.out relinked : {0}   ({1:HH:mm:ss} -> {2:HH:mm:ss})" -f $relinked, $before, $after)

    if ($code -ne 0) { Write-Host "RESULT: FAIL - build failed, see the lines above"; exit 1 }
    if (-not (Test-Path $out)) { Write-Host "RESULT: FAIL - car.out was not produced"; exit 1 }

    $sz = @(& $sizeExe $out | Select-Object -Last 1) -split '\s+' | Where-Object { $_ }
    $text = [int]$sz[0]; $data = [int]$sz[1]; $bss = [int]$sz[2]
    Write-Host ("size             : text {0} + data {1} = {2}  <- this is the 'wrote N bytes' figure flash.ps1 must report;  bss {3}" -f $text, $data, ($text + $data), $bss)

    # Was anything newer than car.out left behind? That is the silent-failure mode worth catching.
    $stale = @(Get-ChildItem -Path $here -Filter *.c -File) + @(Get-ChildItem -Path $here -Filter *.h -File)
    $newer = @($stale | Where-Object { $_.LastWriteTime -gt $after })
    if ($newer.Count -gt 0) {
        Write-Host ""
        Write-Host "** these sources are NEWER than car.out - the build did not pick them up:"
        $newer | ForEach-Object { Write-Host ("   " + $_.Name) }
        Write-Host "   Usually a missing dependency line in gcc/makefile. Re-run with -Touch to force it."
        Write-Host "RESULT: INCONCLUSIVE - built, but not from the current sources"
        exit 2
    }

    Write-Host "RESULT: PASS - compiled; NOT flashed (use flash.ps1 for that, and only when needed)"
    exit 0
}
finally { Pop-Location }
