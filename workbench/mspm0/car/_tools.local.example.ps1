# _tools.local.example.ps1 - TEMPLATE. Copy to  _tools.local.ps1  and edit for YOUR machine.
#
#   copy _tools.local.example.ps1 _tools.local.ps1
#
# _tools.local.ps1 is gitignored on purpose: machine-specific paths must NOT be committed
# (that is what caused the whole problem this file exists to solve - 11 files, 25 hardcoded
# absolute paths, every board script dead on the second dev machine). This is the same pattern
# as Android's local.properties, Node's .env.local and CMakeUserPresets.json: the VALUES stay
# local, the SHAPE is committed.
#
# You only need the lines for tools that _tools.ps1 cannot find on its own. Run
#     .\env_check.ps1
# first - it prints exactly which ones failed and where it looked. If it says PASS, you do not
# need this file at all.
#
# ASCII only (Windows PowerShell 5.1 reads .ps1 as ANSI).

# --- example: machine with everything under D:\toolchains -------------------------------
# $env:DIANSAI_OPENOCD_ROOT  = 'D:\toolchains\xpack-openocd-0.12.0-7'
# $env:DIANSAI_ARM_BIN       = 'D:\toolchains\arm-none-eabi\12.2 mpacbti-rel1\bin'
# $env:DIANSAI_MAKE_BIN      = 'D:\toolchains\mingw64\bin'
# $env:MSPM0_SDK_INSTALL_DIR = 'D:\toolchains\mspm0-sdk'
# $env:DIANSAI_SYSCONFIG_CLI = 'D:\toolchains\sysconfig_1.23.1\sysconfig_cli.bat'

# --- notes ------------------------------------------------------------------------------
# * MSPM0_SDK_INSTALL_DIR is deliberately the same variable name the gcc/makefile already
#   honours (`MSPM0_SDK_INSTALL_DIR ?= ...`), so one setting serves both make and the scripts.
# * DIANSAI_SYSCONFIG_CLI is usually NOT needed: _tools.ps1 reads the SDK's own
#   imports.mak SYSCONFIG_TOOL, which is the authoritative choice when a machine has several
#   SysConfig installs. Only set it if you know you want a different one.
# * After editing, always re-run  .\env_check.ps1  and check the version-pin section: a
#   SysConfig/SDK version different from toolchain.lock means this machine can produce a
#   different binary from the same source. Read toolchain.lock for the rule about that.
