# build_flash.ps1 - headless Keil UV4 build / flash driver (ASCII only for PS5.1 safety)
# Usage:
#   powershell -ExecutionPolicy Bypass -File build_flash.ps1 -Action build
#   powershell -ExecutionPolicy Bypass -File build_flash.ps1 -Action flash
#   powershell -ExecutionPolicy Bypass -File build_flash.ps1 -Action all   (rebuild then flash)
# UV4 return codes: 0=no errors/warnings, 1=warnings, 2=errors, >=3 fatal.
# NOTE: close the Keil GUI for this project first (UV4 batch needs exclusive access).
param([string]$Action = "build")

$uv4  = "C:\Users\Klara\AppData\Local\Keil_v5\UV4\UV4.exe"
$proj = "C:\Users\Klara\Desktop\diansai\workbench\Main\MDK-ARM\STM32H750.uvprojx"
$dir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$blog = Join-Path $dir "_build_log.txt"
$flog = Join-Path $dir "_flash_log.txt"

if (-not (Test-Path $uv4))  { Write-Host "UV4 NOT FOUND: $uv4"; exit 99 }
if (-not (Test-Path $proj)) { Write-Host "PROJECT NOT FOUND: $proj"; exit 99 }

function Run-UV4($flag, $log, $label) {
    if (Test-Path $log) { Remove-Item $log -ErrorAction SilentlyContinue }
    Write-Host ("=== UV4 " + $label + " ===")
    $p = Start-Process -FilePath $uv4 -ArgumentList @("-j0", $flag, $proj, "-o", $log) -Wait -PassThru
    $rc = $p.ExitCode
    if (Test-Path $log) { Get-Content $log | ForEach-Object { Write-Host $_ } }
    Write-Host ("=== " + $label + " EXIT CODE = " + $rc + " ===")
    return $rc
}

switch ($Action.ToLower()) {
    "build" {
        $rc = Run-UV4 "-b" $blog "BUILD"
        exit $rc
    }
    "rebuild" {
        $rc = Run-UV4 "-r" $blog "REBUILD"
        exit $rc
    }
    "flash" {
        $rc = Run-UV4 "-f" $flog "FLASH"
        exit $rc
    }
    "all" {
        $rc = Run-UV4 "-r" $blog "REBUILD"
        if ($rc -ge 2) { Write-Host "BUILD HAD ERRORS, abort flash."; exit $rc }
        $rc2 = Run-UV4 "-f" $flog "FLASH"
        exit $rc2
    }
    default { Write-Host "Unknown action: $Action (use build|rebuild|flash|all)"; exit 98 }
}
