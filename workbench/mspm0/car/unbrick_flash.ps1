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
#
# -Speed (added 2026-07-27): SWD clock in kHz. DEFAULT 500 = the value that actually rescued this
#   board on 2026-07-24, so ATTEMPT 1 SHOULD USE THE DEFAULT (known-good, don't add variables).
#   Only if attempt 1 dies *during the flash write* (not at unlock) try: .\unbrick_flash.ps1 -Speed 200
#   Rationale: a mid-write death points at signal integrity / supply, and lowering the clock is the
#   cheapest single-variable test for that. Do NOT lower it pre-emptively - you would then never
#   know whether it was the clock or the re-seated SWD wire that fixed it.
param([int]$Speed = 500)
Set-Location (Join-Path $PSScriptRoot 'gcc')
# Toolchain paths resolved in ONE place (_tools.ps1) - hardcoding them broke these
# scripts outright on the other dev machine (incl. the unbrick path). See _tools.ps1.
. "$PSScriptRoot\_tools.ps1"
# NOTE: Find-Openocd returns @{Exe;Scripts} (API defined at the top of _tools.ps1).
$oo  = (Find-Openocd).Exe
$scr = Find-OpenocdScripts
Write-Host "=== PRESS ENTER, then TAP RST continuously. STOP the instant you HEAR THE BEEP. ===" -ForegroundColor Yellow
Write-Host ("=== adapter speed = " + $Speed + " kHz ; expect 'wrote ~27-29 KB' for the current car.out ===") -ForegroundColor Gray
$sawUnlock = $false; $sawWrite = $false
# ★★★ 2026-07-27: srst_push_pull 是这条路能不用人点 RST 的唯一原因 ★★★
#   openocd 对本适配器默认 srst_open_drain, 那个模式下板子 nRESET 根本没被拉低 => 每次 reset 都
#   静默地什么也没做, 所以 MSPM0 的 factory-reset(设计上必须翻转 nRESET, 见 target/ti_mspm0.cfg
#   的注释)只能靠人手点按钮来"人工补上那个复位"。改成 push-pull 后, 全程无人值守即可救砖 —— 已实测:
#   "Factory reset success!" -> "wrote 28552 bytes" -> "verified 28552 bytes", 没人碰板子。
#   ⇒ 手点 RST 从"必需"降级为"push-pull 也不行时的后备手段"。
& $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed $Speed" `
      -c "reset_config srst_only srst_push_pull connect_deassert_srst" `
      -c "adapter srst pulse_width 50" -c "adapter srst delay 200" `
      -f target/ti_mspm0.cfg `
      -c "init" -c "mspm0_factory_reset" `
      -c "flash write_image erase car.out" -c "verify_image car.out" `
      -c "reset run" -c "shutdown" 2>&1 | ForEach-Object {
    Write-Host $_
    if ($_ -match "Factory reset success") {
        $script:sawUnlock = $true
        for ($b = 0; $b -lt 4; $b++) { [console]::beep(1200, 220) }
        Write-Host "`n>>>>>>>>>>  STOP TAPPING RST NOW  -- let it write flash  <<<<<<<<<<`n" -ForegroundColor Black -BackgroundColor Yellow
    }
    if ($_ -match "wrote .* bytes|Verified OK") { $script:sawWrite = $true; Write-Host ">>> flash write OK <<<" -ForegroundColor Green }
}

# Explicit verdict + a decision tree, so nobody has to think while staring at 200 lines of log.
Write-Host ""
if ($sawUnlock -and $sawWrite) {
    Write-Host "RESULT: PASS - unlocked AND flashed. Now PRESS the physical RST button once (cold boot)." -ForegroundColor Green
    exit 0
}
if ($sawUnlock -and -not $sawWrite) {
    Write-Host "RESULT: FAIL - unlock worked, the FLASH WRITE did not." -ForegroundColor Red
    Write-Host "  Most likely: you kept tapping RST past the beep (a reset mid-write kills it)," -ForegroundColor Yellow
    Write-Host "  or signal/supply dropped. Next single-variable step:" -ForegroundColor Yellow
    Write-Host "    .\unbrick_flash.ps1 -Speed 200     <- lower SWD clock, stop tapping AT the beep" -ForegroundColor Yellow
    exit 1
}
Write-Host "RESULT: FAIL - never saw 'Factory reset success' (chip stayed locked)." -ForegroundColor Red
Write-Host "  This is a TIMING/CONTACT problem, not a clock problem. Do NOT lower -Speed yet." -ForegroundColor Yellow
Write-Host "  1) Re-seat the 3 SWD wires (SWDIO/SWCLK/GND); unplug+replug the DAP USB." -ForegroundColor Yellow
Write-Host "  2) Tap RST FASTER and keep tapping the whole time - it took 8 tries once." -ForegroundColor Yellow
Write-Host "  3) Unlock-only mode is safer to hammer:  .\unbrick.ps1 -Tries 20  (over-tapping cannot" -ForegroundColor Yellow
Write-Host "     corrupt a write because it never writes) - then rescue-flash once it reports SUCCESS." -ForegroundColor Yellow
Write-Host "  STOP after ~3 failed rounds and tell the assistant - repeated openocd retries can wedge" -ForegroundColor Yellow
Write-Host "  the DAP USB stack, and then even the rescue path is gone." -ForegroundColor Yellow
exit 2
