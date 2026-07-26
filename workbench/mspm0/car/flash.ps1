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
$w = & $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "program gcc/car.out exit" 2>&1
$w | ForEach-Object { Write-Host $_ }

# GATE (added 2026-07-27 after this script's first real run FAILED and it still printed the
# reassuring "Flash OK?" banner - a script must never sound calm about a failed write):
#   If stage 1 did not print "** Programming Finished **", stage 2 MUST NOT run.
#   Reason: stage 2 does init+halt. Halting a chip whose flash write just died is exactly how
#   this MCU got pushed from "failed write" into full double-fault lockup ("Could not find
#   MEM-AP"). Stop, report, and point at the rescue path instead. NEVER auto-retry the write.
if (($w -join "`n") -notmatch [regex]::Escape("** Programming Finished **")) {
    Write-Host "`n================ FLASH WRITE FAILED - STOPPING ================" -ForegroundColor Red
    Write-Host "Did NOT see '** Programming Finished **'. Stage 2 (init+halt) skipped on purpose." -ForegroundColor Red
    if (($w -join "`n") -match "Could not find MEM-AP|Examination failed") {
        Write-Host "Chip looks LOCKED (double-fault lockup)." -ForegroundColor Red
    }
    Write-Host "DO NOT re-run this script. Do this instead:" -ForegroundColor Yellow
    Write-Host "  1) Check the 3 SWD wires (SWDIO/SWCLK/GND) are firmly seated, and board power is on/steady." -ForegroundColor Yellow
    Write-Host "  2) Confirm the chip is really dead: open COM30 - no telemetry at all = firmware not running." -ForegroundColor Yellow
    Write-Host "  3) Rescue with:  .\unbrick_flash.ps1   (run it in YOUR terminal; tap RST until the BEEP, then stop)" -ForegroundColor Yellow
    exit 1
}

Write-Host "`n[2/2] verifying in a separate read-only session..." -ForegroundColor Cyan
$v = & $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "init" -c "halt" -c "verify_image gcc/car.out" -c "shutdown" 2>&1
$v | ForEach-Object { Write-Host $_ }

if (($v -join "`n") -match "verified \d+ bytes") {
    Write-Host "`nRESULT: PASS - 'Programming Finished' + 'verified N bytes'." -ForegroundColor Green
    Write-Host ">>> Now PRESS the physical RST button once to cold-boot (MSPM0 soft reset will NOT launch the app). <<<" -ForegroundColor Yellow
    exit 0
}
# Write succeeded but the separate verify could not confirm it -> genuinely unknown, say so.
Write-Host "`nRESULT: INCONCLUSIVE - write reported Finished, but no 'verified N bytes' line." -ForegroundColor Yellow
Write-Host "The write most likely took (stage 1 passed). Press RST and check the boot banner/telemetry" -ForegroundColor Yellow
Write-Host "for the new build stamp. Do NOT re-flash just because verify was quiet." -ForegroundColor Yellow
exit 2
