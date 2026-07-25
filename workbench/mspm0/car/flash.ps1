# flash.ps1 - reflash car.out to a HEALTHY MSPM0 (DAP works, NOT bricked).
#   * Bricked? (openocd prints "Could not find MEM-AP") -> use unbrick_flash.ps1 instead.
#   * AFTER a good flash: PRESS the physical RST button once to cold-boot
#     (openocd soft-reset can't launch the app on MSPM0 - only a real POR does).
#   * Run this in YOUR terminal so an agent timeout never interrupts the flash write.
Set-Location $PSScriptRoot
$oo  = "C:\ti\xpack-openocd-0.12.0-7\bin\openocd.exe"
$scr = "C:/ti/xpack-openocd-0.12.0-7/openocd/scripts"
if (-not (Test-Path "gcc\car.out")) { Write-Host "gcc/car.out not found - build first (mingw32-make in gcc/)" -ForegroundColor Red; exit 1 }
& $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "program gcc/car.out verify exit"
Write-Host "`n>>> See 'Verified OK' / 'Programming Finished'? Flash OK. Now PRESS the RST button to cold-boot. <<<" -ForegroundColor Yellow
