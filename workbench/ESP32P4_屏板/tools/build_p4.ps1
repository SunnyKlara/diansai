# ASCII only (PowerShell 5.1 encoding pitfall).
#
# Build (and optionally flash + monitor) the ESP32-P4 6.2in MIPI LCD project.
#
#   -Flash   also write the firmware over the USB-Serial-JTAG port
#   -Port    serial port (default COM7 = the Type-C silkscreened "UART")
#
# HARD RULE: never run `idf.py set-target` on this project.
# The checked-in sdkconfig carries the settings that keep the panel from tearing/garbling
# (IDF_EXPERIMENTAL_FEATURES, PSRAM HEX 200MHz + XIP, TASK_WDT off, LVGL PPA rotation).
# set-target regenerates sdkconfig from defaults and silently drops them.
#
# Project path must stay ASCII-only: ESP-IDF/CMake choke on non-ASCII paths
# (same class of bug as the MSPM0 "GCC cannot handle Chinese paths" rule in this repo).

param(
    [string]$Project = "d:\diansai\workbench\esp32p4\p4_lcd",
    [string]$Port    = "COM7",
    [switch]$Flash,
    [switch]$Monitor
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot\idf_shell.ps1"

# Guard: a missing idf.py used to leave $LASTEXITCODE at 0 from a previous command,
# so the script reported BUILD_EXIT=0 while nothing had been built. Fail loudly instead.
if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Output "BUILD_ABORT: idf.py not available (IDF env not activated)"
    exit 90
}

Set-Location $Project
Write-Output "=== PROJECT: $Project ==="
Write-Output "=== PANEL  : $((Select-String -Path 'main\lcd_panel_select.h' -Pattern '^#define LCD_PANEL_ACTIVE').Line) ==="

idf.py build
$buildExit = $LASTEXITCODE
Write-Output "BUILD_EXIT=$buildExit"
if ($buildExit -ne 0) { exit $buildExit }

if ($Flash) {
    idf.py -p $Port flash
    Write-Output "FLASH_EXIT=$LASTEXITCODE"
}
if ($Monitor) {
    Write-Output "NOTE: monitor is interactive; use tools\p4_boot_read.ps1 for scripted log capture."
}
