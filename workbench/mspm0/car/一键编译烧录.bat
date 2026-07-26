@echo off
setlocal enabledelayedexpansion
title Tianmengxing build + flash
rem ============================================================================
rem  Build + flash the MSPM0G3507 car firmware.
rem
rem  2026-07-27: rewritten to AUTO-DETECT every tool path. The old version had
rem  three hard-coded paths and ALL THREE died when this PC was reinstalled
rem  (ARM toolchain dir gone, MAKEBIN still pointed at C:\Users\Klara\..., and
rem  C:\ti\xpack-openocd-0.12.0-7 no longer existed). Auto-detect means it keeps
rem  working no matter where the toolchain gets installed next time.
rem
rem  Needs four things installed (see CONTINUATION_GUIDE.md section 4):
rem    1. ARM GNU toolchain arm-none-eabi   (arm-none-eabi-gcc)
rem    2. TI MSPM0 SDK                      (git clone TexasInstruments/mspm0-sdk)
rem    3. TI SysConfig standalone           (sysconfig_cli.bat)
rem    4. xpack-openocd                     (openocd.exe, for SWD flashing)
rem  plus GNU make (mingw32-make / make).
rem
rem  NOTE: the SDK's git version ships imports.mak.windows, not imports.mak.
rem  This script creates imports.mak automatically, and then overrides the tool
rem  paths on the make command line (the makefile uses ?= so that wins) --
rem  so you never have to hand-edit imports.mak.
rem ============================================================================
rem  Pass "nopause" as the first argument to skip every pause (so an agent / CI can
rem  run it non-interactively and just read the exit code).
set "NOPAUSE="
if /i "%~1"=="nopause" set "NOPAUSE=1"

set "OUT=car.out"
set "SDK="
set "ARMROOT="
set "MAKEEXE="
set "OCDEXE="
set "SYSCFG="

echo.
echo ============ [0/3] LOCATING TOOLS ============

rem  2026-07-27b: D:\toolchains\* candidates added FIRST -- that is where this PC's
rem  toolchain actually lives now (C: was too full). Keep the C:\ti fallbacks so the
rem  script still works on a machine using TI's default layout.

rem ---------- MSPM0 SDK ----------
for %%D in ("D:\toolchains\mspm0-sdk" "C:\ti\mspm0-sdk" "D:\ti\mspm0-sdk" "%USERPROFILE%\ti\mspm0-sdk" "C:\ti\mspm0_sdk" "D:\ti\mspm0_sdk") do (
  if not defined SDK if exist "%%~D\source\ti\driverlib\driverlib.h" set "SDK=%%~D"
)

rem ---------- ARM GNU toolchain (need the ROOT dir, not bin) ----------
for /d %%G in ("D:\toolchains\arm-gnu*") do (
  if not defined ARMROOT if exist "%%~G\bin\arm-none-eabi-gcc.exe" set "ARMROOT=%%~G"
)
if not defined ARMROOT for /f "delims=" %%P in ('where arm-none-eabi-gcc 2^>nul') do (
  if not defined ARMROOT for %%Q in ("%%~dpP..") do set "ARMROOT=%%~fQ"
)
if not defined ARMROOT (
  for /d %%G in ("C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*") do (
    if exist "%%~G\bin\arm-none-eabi-gcc.exe" set "ARMROOT=%%~G"
  )
)
if not defined ARMROOT (
  for /d %%G in ("C:\Program Files\Arm GNU Toolchain arm-none-eabi\*") do (
    if exist "%%~G\bin\arm-none-eabi-gcc.exe" set "ARMROOT=%%~G"
  )
)
if not defined ARMROOT (
  for /d %%G in ("C:\ti\gcc*") do (
    if exist "%%~G\bin\arm-none-eabi-gcc.exe" set "ARMROOT=%%~G"
  )
)

rem ---------- GNU make ----------
rem  D:\toolchains\build-tools\bin holds make.exe ONLY (sh.exe/busybox deliberately
rem  deleted): with no sh.exe around, make keeps SHELL=cmd.exe, which is what the
rem  SDK's imports.mak Windows branch and the SysConfig .bat call both expect.
if exist "D:\toolchains\build-tools\bin\make.exe" set "MAKEEXE=D:\toolchains\build-tools\bin\make.exe"
if not defined MAKEEXE for /f "delims=" %%P in ('where mingw32-make 2^>nul') do if not defined MAKEEXE set "MAKEEXE=%%~fP"
if not defined MAKEEXE for /f "delims=" %%P in ('where make 2^>nul') do if not defined MAKEEXE set "MAKEEXE=%%~fP"
if not defined MAKEEXE if exist "D:\K210\kendryte-toolchain\bin\mingw32-make.exe" set "MAKEEXE=D:\K210\kendryte-toolchain\bin\mingw32-make.exe"

rem ---------- openocd ----------
for /d %%G in ("D:\toolchains\xpack-openocd*") do if exist "%%~G\bin\openocd.exe" set "OCDEXE=%%~G\bin\openocd.exe"
if not defined OCDEXE for /d %%G in ("C:\ti\xpack-openocd*") do if exist "%%~G\bin\openocd.exe" set "OCDEXE=%%~G\bin\openocd.exe"
if not defined OCDEXE for /d %%G in ("C:\xpack-openocd*") do if exist "%%~G\bin\openocd.exe" set "OCDEXE=%%~G\bin\openocd.exe"
if not defined OCDEXE for /f "delims=" %%P in ('where openocd 2^>nul') do if not defined OCDEXE set "OCDEXE=%%~fP"

rem ---------- SysConfig CLI ----------
for /d %%G in ("D:\toolchains\sysconfig*") do (
  if not defined SYSCFG if exist "%%~G\sysconfig_cli.bat" set "SYSCFG=%%~G\sysconfig_cli.bat"
)
if not defined SYSCFG if exist "C:\ti\sysconfig\sysconfig_cli.bat" set "SYSCFG=C:\ti\sysconfig\sysconfig_cli.bat"
if not defined SYSCFG for /d %%G in ("C:\ti\sysconfig_*") do (
  if exist "%%~G\sysconfig_cli.bat" set "SYSCFG=%%~G\sysconfig_cli.bat"
)
if not defined SYSCFG for /d %%G in ("C:\ti\ccs*") do (
  for /d %%H in ("%%~G\ccs\utils\sysconfig_*") do (
    if exist "%%~H\sysconfig_cli.bat" set "SYSCFG=%%~H\sysconfig_cli.bat"
  )
)

rem ---------- report ----------
set "MISSING=0"
call :show "MSPM0 SDK      " "!SDK!"
call :show "ARM GNU gcc    " "!ARMROOT!"
call :show "GNU make       " "!MAKEEXE!"
call :show "SysConfig CLI  " "!SYSCFG!"
call :show "openocd        " "!OCDEXE!"

if "!MISSING!"=="1" (
  echo.
  echo [X] Some tools are missing - cannot build. Install them, then re-run.
  echo     Exact versions/paths that are known to work: .kiro\steering\工程事实SSOT.md  section D
  echo     This PC installs everything under D:\toolchains ^(C: was too full^):
  echo       SDK       : D:\toolchains\mspm0-sdk        2.09.00.00  ^(github tag mspm0_sdk_2_09_00_01^)
  echo                   codeload.github.com/TexasInstruments/mspm0-sdk/zip/refs/tags/mspm0_sdk_2_09_00_01
  echo                   ^(no TI login needed - prebuilt driverlib.a and .metadata\product.json are in the repo^)
  echo                   pick a tag whose .metadata\product.json minToolVersion ^<= your SysConfig version
  echo       ARM gcc   : D:\toolchains\arm-gnu-12.2     12.2.MPACBTI-Rel1  ^(developer.arm.com, zip, no installer^)
  echo       SysConfig : D:\toolchains\sysconfig-1.23.1  1.23.1.4034
  echo                   silent install: sysconfig-1.23.1_4034-setup.exe --mode unattended --prefix ^<dir^>
  echo       openocd   : D:\toolchains\xpack-openocd-0.12.0-7   github.com/xpack-dev-tools/openocd-xpack
  echo       make      : D:\toolchains\build-tools\bin\make.exe  4.4.1  ^(xpack windows-build-tools^)
  echo                   IMPORTANT: delete sh.exe/busybox.exe from that bin dir, else make switches
  echo                   SHELL to sh and the SDK imports.mak / SysConfig .bat calls break.
  echo     Offline copies of all four installers are kept in D:\toolchains\_dl\ - do not delete.
  call :pauseif & exit /b 1
)

rem ---------- SDK git version has imports.mak.windows, not imports.mak ----------
if not exist "!SDK!\imports.mak" (
  if exist "!SDK!\imports.mak.windows" (
    echo   imports.mak missing -^> creating it from imports.mak.windows
    copy /y "!SDK!\imports.mak.windows" "!SDK!\imports.mak" >nul
  ) else (
    echo [X] Neither imports.mak nor imports.mak.windows found in !SDK!
    call :pauseif & exit /b 1
  )
)

rem make wants forward slashes in path variables
set "SDKFWD=!SDK:\=/!"
set "ARMFWD=!ARMROOT:\=/!"

set "PATH=!ARMROOT!\bin;%PATH%"
cd /d "%~dp0gcc"

echo.
echo ============ [1/3] BUILD ============
"!MAKEEXE!" MSPM0_SDK_INSTALL_DIR="!SDKFWD!" GCC_ARMCOMPILER="!ARMFWD!" SYSCONFIG_TOOL="!SYSCFG!"
if errorlevel 1 (
  echo.
  echo [X] build failed - fix errors above, not flashing.
  call :pauseif & exit /b 1
)

echo.
echo ============ [2/3] CHECK OUTPUT ============
if not exist "%OUT%" (
  echo [X] %OUT% was not produced.
  call :pauseif & exit /b 1
)
for %%F in ("%OUT%") do echo   %OUT%  %%~zF bytes  %%~tF

echo.
echo ============ [3/3] FLASH via CMSIS-DAP / SWD ============
rem Deliberately NOT using "program ... verify": the CRC helper needs a stable halt and
rem times out on MSPM0, producing fake byte diffs (see .kiro/steering/knowledge pitfall
rem notes). write_image + a manual RST is what has actually worked on this board.
"!OCDEXE!" -f interface/cmsis-dap.cfg -c "adapter speed 1000" -f target/ti_mspm0.cfg -c "init" -c "halt" -c "flash write_image erase %OUT%" -c "shutdown"
if errorlevel 1 (
  echo.
  echo [X] flash failed. Check: 1.DAP plugged  2.SWD DIO/CLK/GND wired  3.board powered
  echo     If it says "Could not find MEM-AP" the chip is locked up -^> run unbrick_flash.ps1
  call :pauseif & exit /b 1
)

echo.
echo ============ DONE - now press the physical RST button ============
echo   MSPM0 will not start the new app from an openocd soft reset; only POR / RST works.
call :pauseif
exit /b 0

:pauseif
if not defined NOPAUSE pause
exit /b 0

:show
if "%~2"=="" (
  echo   %~1 : MISSING
  set "MISSING=1"
) else (
  echo   %~1 : %~2
)
exit /b 0
