# flash.ps1 - reflash car.out to a HEALTHY MSPM0 (DAP works, NOT bricked).
#   * Bricked? (openocd prints "Could not find MEM-AP") -> use unbrick_flash.ps1 instead.
#   * FULLY AUTOMATIC: no finger on the RST button. See "SRST" below.
#
# ============================ 2026-07-27 REWRITE - read this ============================
# Two discoveries from the rescue on 2026-07-27 changed how this works:
#
# (1) SRST IS WIRED AND USABLE, BUT ONLY IN PUSH-PULL MODE.
#     openocd's default for this adapter is srst_open_drain, and in that mode the board's
#     nRESET never actually goes low -> every reset silently did nothing. That is the real
#     reason this project believed "cold boot only works with a physical RST press".
#     With  reset_config srst_only srst_push_pull  the reset works, which means:
#        - `reset run` at the end cold-starts the app  => NO human finger needed
#        - the MSPM0 factory-reset (unbrick) sequence works unattended (it needs nRESET
#          toggling by design - see openocd's target/ti_mspm0.cfg comments)
#     Proven on 2026-07-27: "Factory reset success!" + "wrote 28552 bytes" + "verified
#     28552 bytes", start to finish with nobody touching the board.
#
# (2) ONE SESSION IS ENOUGH. (Corrected 2026-07-27 after the first real run.)
#     `flash write_image erase` + `verify_image` fits in one session. BUT the earlier claim that
#     "verify_image is a plain host-side byte compare" is WRONG: verify_image FIRST tries a
#     target-side CRC helper (same helper `program` uses) and only falls back to a host-side
#     byte compare when that fails. On this board the CRC helper does time out, so you WILL see
#       Error: timed out while waiting for target halted
#       Error: error executing cortex_m crc algorithm
#       Error: checksum mismatch - attempting binary compare
#     Those three lines are EXPECTED and harmless - what matters is what the binary compare says.
#     ⇒ The real pass/fail signal is: any "diff <n> address 0x..." line = CONTENT MISMATCH = FAIL.
#
# SUCCESS = "wrote N bytes" AND "verified N bytes", where N matches text+data from
#   arm-none-eabi-size (do NOT memorise N - it changes every time the code changes).
# =======================================================================================
#
# Timing note: the write takes ~75-90s at 500 kHz. Do NOT interrupt it, and do not run this
#   inside anything that might time out and kill the process mid-write.
#
# Halting before erase is REQUIRED (learned 2026-07-27): if the app is running, erase fails with
#   "Please halt target for erasing flash" and nothing gets written. The rescue path never hit
#   this because mspm0_factory_reset ends with its own halt.
#   We use `reset halt` (not plain `halt`): a plain halt of a running app produced
#   "target was in unknown state when halt was requested" and that run came out with a 16-byte
#   flash mismatch. `reset halt` puts the core in a known state first (SRST then halt).
#   ⚠ Halting freezes the PWM outputs at whatever duty they had. Send `z` (full stop) BEFORE
#   flashing if the motors could be driving - otherwise a wheel keeps spinning while halted.

Set-Location $PSScriptRoot
# Toolchain paths come from ONE place (_tools.ps1). Hardcoding them here used to make this
# script fail outright on the other development machine - see _tools.ps1 header.
. "$PSScriptRoot\_tools.ps1"
# NOTE: Find-Openocd returns @{Exe;Scripts} (API defined at the top of _tools.ps1).
$oo  = (Find-Openocd).Exe
$scr = Find-OpenocdScripts
if (-not (Test-Path "gcc\car.out")) {
    Write-Host "gcc/car.out not found - build first (mingw32-make in gcc/)" -ForegroundColor Red
    exit 1
}

# Expected image size, so the "wrote N bytes" line can be checked instead of eyeballed.
$expect = $null
$sizeExe = $null
try { $sizeExe = Find-ArmTool arm-none-eabi-size } catch {
    Write-Host "  (arm-none-eabi-size unresolved - skipping the expected-size cross-check)" -ForegroundColor Gray
}
if ($sizeExe) {
    $line = (& $sizeExe "gcc\car.out" | Select-Object -Last 1) -split '\s+' | Where-Object { $_ -ne "" }
    if ($line.Count -ge 2) { $expect = [int]$line[0] + [int]$line[1] }   # text + data
}
if ($expect) { Write-Host ("expected image size = " + $expect + " bytes (text+data)") -ForegroundColor Gray }

Write-Host "writing + verifying (one session, SRST push-pull, ~75s - DO NOT INTERRUPT)..." -ForegroundColor Cyan
# ORDER MATTERS - do NOT hoist srst_only to the top (learned the hard way 2026-07-27):
#   openocd's own target/ti_mspm0.cfg says it plainly:
#     "MSPM0 requires board level NRST ... However this cannot be the default configuration as
#      this PREVENTS reset init / reset halt from functioning properly since the Debug Subsystem
#      (debugss) or coresight seems impacted by nRST."
#   With srst_only active, `reset halt` times out ("TARGET: Not halted") and nothing gets written.
#   That is also exactly why the official mspm0_board_reset proc SAVES and RESTORES reset_config
#   around its own temporary srst_only.
#   ⇒ Correct split:  halt/erase/write/verify with the target default (sysresetreq),
#                     then switch to SRST push-pull only for the final `reset run` (true POR).
$log = & $oo -s $scr -f interface/cmsis-dap.cfg `
    -c "adapter speed 500" `
    -f target/ti_mspm0.cfg `
    -c "init" `
    -c "reset halt" `
    -c "flash write_image erase gcc/car.out" `
    -c "verify_image gcc/car.out" `
    -c "reset_config srst_only srst_push_pull connect_deassert_srst" `
    -c "adapter srst pulse_width 50" `
    -c "adapter srst delay 200" `
    -c "reset run" `
    -c "shutdown" 2>&1
$log | ForEach-Object { Write-Host $_ }
$txt = $log -join "`n"

$mW = [regex]::Match($txt, 'wrote (\d+) bytes')
$mV = [regex]::Match($txt, 'verified (\d+) bytes')

Write-Host ""
if ($txt -match "Could not find MEM-AP") {
    Write-Host "RESULT: FAIL - chip is LOCKED (double-fault lockup)." -ForegroundColor Red
    Write-Host "  Rescue:  .\unbrick_flash.ps1    (also fully automatic now)" -ForegroundColor Yellow
    exit 1
}
if (-not $mW.Success) {
    Write-Host "RESULT: FAIL - no 'wrote N bytes'. The write did not happen." -ForegroundColor Red
    Write-Host "  DO NOT immediately re-run. A write that dies mid-way is how this chip got bricked." -ForegroundColor Yellow
    Write-Host "  First: re-seat the 3 SWD wires, confirm board power is on/steady, then try once more." -ForegroundColor Yellow
    Write-Host "  If it dies DURING the write again, lower the clock: edit 'adapter speed 500' -> 200." -ForegroundColor Yellow
    exit 1
}
# Content mismatch is the one thing that MUST be caught. The CRC helper times out on this board,
# so verify falls back to a byte compare which prints "diff <n> address 0x...". The first version
# of this script only looked for "verified N bytes", so it classified a real 16-byte mismatch as
# INCONCLUSIVE *and advised not to re-flash* - exactly backwards. Check diffs BEFORE anything else.
$mD = [regex]::Matches($txt, 'diff \d+ address 0x[0-9a-fA-F]+')
if ($mD.Count -gt 0) {
    Write-Host ("RESULT: FAIL - flash content does NOT match the binary (" + $mD.Count + " differing byte(s)).") -ForegroundColor Red
    Write-Host "  The chip is running a partially wrong image - it may work, may crash. Re-flash." -ForegroundColor Yellow
    Write-Host "  This is NOT the known false 'Verify Failed'; a byte compare found real diffs." -ForegroundColor Yellow
    exit 1
}
if (-not $mV.Success) {
    if ($txt -match "No more differences found") {
        Write-Host ("RESULT: PASS - wrote " + $mW.Groups[1].Value + " bytes; byte compare found no differences.") -ForegroundColor Green
        Write-Host "  (The CRC-helper timeout above is a known, harmless quirk of this board.)" -ForegroundColor Gray
        exit 0
    }
    Write-Host ("RESULT: INCONCLUSIVE - wrote " + $mW.Groups[1].Value + " bytes but verify never reported.") -ForegroundColor Yellow
    Write-Host "  Neither 'verified N bytes' nor a byte-compare result appeared. Verify manually" -ForegroundColor Yellow
    Write-Host "  (boot banner / telemetry) before assuming the image is good." -ForegroundColor Yellow
    exit 2
}
$w = [int]$mW.Groups[1].Value; $v = [int]$mV.Groups[1].Value
if ($expect -and ($w -ne $expect)) {
    Write-Host ("RESULT: INCONCLUSIVE - wrote " + $w + " bytes but expected " + $expect + ".") -ForegroundColor Yellow
    Write-Host "  Stale car.out? Rebuild (mingw32-make in gcc/) and check for a stale-dependency miss." -ForegroundColor Yellow
    exit 2
}
if ($w -ne $v) {
    Write-Host ("RESULT: FAIL - wrote " + $w + " but verified " + $v + " bytes.") -ForegroundColor Red
    exit 1
}
Write-Host ("RESULT: PASS - wrote and verified " + $w + " bytes; 'reset run' issued (SRST push-pull).") -ForegroundColor Green
Write-Host "The app should already be running - no RST press needed. Check telemetry to confirm." -ForegroundColor Green
exit 0
