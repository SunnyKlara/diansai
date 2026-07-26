# unbrick.ps1 - MSPM0 "Could not find MEM-AP" lockup recovery (factory-reset unlock).
#   Loops mspm0_factory_reset while YOU tap the board RST button continuously.
#   Does NOT flash here (so over-tapping can't corrupt a write) — flash separately after SUCCESS.
#   ASCII-only (Win PowerShell 5.1 mangles UTF-8).
param([int]$Tries = 20)
. (Join-Path $PSScriptRoot '_tools.ps1')   # path resolution lives in ONE place - see _tools.ps1 header
$ocd = Find-Openocd
$oo  = $ocd.Exe
$scr = $ocd.Scripts
Write-Output "=== KEEP TAPPING the board RST button fast + continuously until you see UNLOCK SUCCESS ==="
for ($i = 1; $i -le $Tries; $i++) {
    Write-Output "--- factory_reset attempt $i/$Tries ---"
    $out = & $oo -s $scr -f interface/cmsis-dap.cfg -c "adapter speed 500" -f target/ti_mspm0.cfg -c "init" -c "mspm0_factory_reset" -c "shutdown" 2>&1
    $out | ForEach-Object { Write-Output $_ }
    if ($out -match "Factory reset success") { Write-Output "=== UNLOCK SUCCESS (attempt $i) -- STOP TAPPING NOW ==="; exit 0 }
    Start-Sleep -Milliseconds 400
}
Write-Output "=== NOT unlocked after $Tries tries -- check RST button / retry ==="
exit 1
