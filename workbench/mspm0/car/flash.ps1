# flash.ps1 - reflash car.out to a HEALTHY MSPM0 (DAP works, NOT bricked).
#   * Bricked? (openocd prints "Could not find MEM-AP") -> use unbrick_flash.ps1 instead.
#   * AFTER a good flash: PRESS the physical RST button once to cold-boot
#     (openocd soft-reset can't launch the app on MSPM0 - only a real POR does).
#   * Run this in YOUR terminal so an agent timeout never interrupts the flash write.
Set-Location $PSScriptRoot
$oo  = "C:\ti\xpack-openocd-0.12.0-7\bin\openocd.exe"
$scr = "C:/ti/xpack-openocd-0.12.0-7/openocd/scripts"
if (-not (Test-Path "gcc\car.out")) { Write-Host "gcc/car.out not found - build first (mingw32-make in gcc/)" -ForegroundColor Red; exit 1 }
# TWO SESSIONS ON PURPOSE (2026-07-27):
#   1) write only  - "program ... exit", NO verify. The in-session verify uses a CRC helper that
#      must be uploaded to target RAM and run with the core halted; on this board it intermittently
#      times out -> prints "** Verify Failed **" EVEN THOUGH the flash write succeeded.
#      That false alarm tempts you to re-flash, and repeated fast re-flashing is exactly what
#      bricked this chip once (double-fault lockup / "Could not find MEM-AP").
#   2) verify separately - fresh session, read-only "verify_image" = plain host-side byte compare.
#      Real evidence looks like: "verified NNNNN bytes in X.XXs".
#   Rule: trust "** Programming Finished **" + step 2's "verified N bytes". Never re-flash on a
#   bare "Verify Failed" - prove it with step 2 first (see 跨题坑库).
Write-Host "[1/2] writing flash (no in-session verify)..." -ForegroundColor Cyan
& $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "program gcc/car.out exit"
Write-Host "`n[2/2] verifying in a separate read-only session..." -ForegroundColor Cyan
& $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "init" -c "halt" -c "verify_image gcc/car.out" -c "shutdown"
Write-Host "`n>>> Saw '** Programming Finished **' and 'verified NNNNN bytes'? Flash OK." -ForegroundColor Yellow
Write-Host ">>> Now PRESS the physical RST button once to cold-boot (MSPM0 soft reset will NOT launch the app). <<<" -ForegroundColor Yellow
