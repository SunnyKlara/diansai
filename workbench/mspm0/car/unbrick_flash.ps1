# unbrick_flash.ps1 v2 - MSPM0 factory-reset unlock + flash car.out in ONE openocd session,
#   with an AUDIBLE BEEP + big "STOP TAPPING" banner the instant "Factory reset success!" appears,
#   so you stop tapping RST at exactly the right moment (that was why v1 failed: couldn't spot it
#   in the flood, over-tapped -> post-reset halt timed out).
#   ASCII-only (Win PowerShell 5.1 mangles UTF-8).
#
# HOW TO RUN (from the car folder):  .\unbrick_flash.ps1
#   1) The INSTANT you press Enter, tap the board RST button fast + continuously (tap tap tap).
#   2) When you HEAR THE BEEP / see the yellow "STOP TAPPING" banner -> STOP tapping immediately.
#   3) Let it finish: watch for  "wrote ... bytes" + "Verified OK" + "Resetting Target".
#   4) Then press RST once for a clean cold boot. Done -> tell the assistant.
. (Join-Path $PSScriptRoot '_tools.ps1')   # path resolution lives in ONE place - see _tools.ps1 header
Set-Location (Join-Path $PSScriptRoot 'gcc')
$ocd = Find-Openocd
$oo  = $ocd.Exe
$scr = $ocd.Scripts
Write-Host "=== PRESS ENTER, then TAP RST continuously. STOP the instant you HEAR THE BEEP. ===" -ForegroundColor Yellow
& $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg `
      -c "init" -c "mspm0_factory_reset" `
      -c "flash write_image erase car.out" -c "verify_image car.out" `
      -c "reset run" -c "shutdown" 2>&1 | ForEach-Object {
    Write-Host $_
    if ($_ -match "Factory reset success") {
        for ($b = 0; $b -lt 4; $b++) { [console]::beep(1200, 220) }
        Write-Host "`n>>>>>>>>>>  STOP TAPPING RST NOW  -- let it write flash  <<<<<<<<<<`n" -ForegroundColor Black -BackgroundColor Yellow
    }
    if ($_ -match "wrote .* bytes|Verified OK") { Write-Host ">>> flash write OK <<<" -ForegroundColor Green }
}
