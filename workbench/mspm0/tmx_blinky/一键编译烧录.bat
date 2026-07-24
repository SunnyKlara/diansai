@echo off
setlocal
title Tianmengxing build + flash

rem ===== tool paths (edit here if installed elsewhere) =====
set "ARMBIN=C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\12.2 mpacbti-rel1\bin"
set "MAKEBIN=C:\Users\Klara\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
set "OCD=C:\ti\xpack-openocd-0.12.0-7"
set "PATH=%ARMBIN%;%MAKEBIN%;%PATH%"

rem ===== firmware output name (change when you rename the project) =====
set "OUT=gpio_toggle_output.out"

cd /d "%~dp0gcc"

echo.
echo ============ [1/2] BUILD ============
mingw32-make
if errorlevel 1 (
  echo.
  echo [X] build failed - fix errors above, not flashing.
  pause & exit /b 1
)

echo.
echo ============ [2/2] FLASH via CMSIS-DAP / SWD ============
"%OCD%\bin\openocd.exe" -s "%OCD%/openocd/scripts" -f interface/cmsis-dap.cfg -c "adapter speed 1000" -f target/ti_mspm0.cfg -c "program %OUT% verify reset exit"
if errorlevel 1 (
  echo.
  echo [X] flash failed. Check: 1.DAP plugged  2.SWD DIO/CLK/GND wired  3.board powered
  pause & exit /b 1
)

echo.
echo ============ DONE - programmed and running ============
pause
