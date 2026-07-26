# _tools.ps1 -- single place that resolves toolchain paths. Dot-source it:
#     . (Join-Path $PSScriptRoot '_tools.ps1')
#
# WHY THIS EXISTS (2026-07-27):
#   flash.ps1 / unbrick.ps1 / unbrick_flash.ps1 / dbg.ps1 each hard-coded
#   "C:\ti\xpack-openocd-0.12.0-7". This repo is provably worked on from >=2
#   machines/clones and they do NOT install to the same drive (this PC installs
#   under D:\toolchains because C: only had ~23GB free; the other one uses C:\ti).
#   Result: every flashing/unbricking script died instantly on this machine --
#   worst possible timing, since the chip was in double-fault lockup and
#   unbrick_flash.ps1 was exactly the script needed to recover it.
#   Add new candidate roots HERE ONLY; never re-hard-code a path in a script.
#
# ASCII-only on purpose: Windows PowerShell 5.1 mangles UTF-8 in .ps1 files.

$script:ToolRoots = @(
    'D:\toolchains',   # this PC (2026-07-27); see .kiro/steering/工程事实SSOT.md section D
    'C:\ti',           # TI default layout / the other machine
    'C:\'
)

function Find-Openocd {
    <#
      .SYNOPSIS Locate openocd.exe + its scripts dir.
      .OUTPUTS  Hashtable @{ Exe = <path>; Scripts = <forward-slash path> }
                Throws if not found, so callers fail loudly instead of running
                openocd from PATH by accident.
    #>
    foreach ($root in $script:ToolRoots) {
        if (-not (Test-Path $root)) { continue }
        $dirs = Get-ChildItem -Path $root -Directory -Filter 'xpack-openocd*' -ErrorAction SilentlyContinue |
                Sort-Object Name -Descending
        foreach ($d in $dirs) {
            $exe = Join-Path $d.FullName 'bin\openocd.exe'
            $scr = Join-Path $d.FullName 'openocd\scripts'
            if ((Test-Path $exe) -and (Test-Path $scr)) {
                return @{ Exe = $exe; Scripts = ($scr -replace '\\', '/') }
            }
        }
    }
    # last resort: whatever is on PATH
    $cmd = Get-Command openocd -ErrorAction SilentlyContinue
    if ($cmd) {
        $base = Split-Path (Split-Path $cmd.Source -Parent) -Parent
        $scr = Join-Path $base 'openocd\scripts'
        if (Test-Path $scr) { return @{ Exe = $cmd.Source; Scripts = ($scr -replace '\\', '/') } }
    }
    throw "openocd not found. Looked under: $($script:ToolRoots -join ', ') (xpack-openocd*) and PATH. Install it or add the root to _tools.ps1."
}

function Find-ArmTool {
    <#
      .SYNOPSIS Locate an arm-none-eabi-* executable (gdb, size, objdump, ...).
      .PARAMETER Name e.g. 'arm-none-eabi-gdb'
    #>
    param([Parameter(Mandatory = $true)][string]$Name)

    $exe = if ($Name -like '*.exe') { $Name } else { "$Name.exe" }

    foreach ($root in $script:ToolRoots) {
        if (-not (Test-Path $root)) { continue }
        $dirs = Get-ChildItem -Path $root -Directory -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match 'arm-gnu|arm-none-eabi|gcc' } |
                Sort-Object Name -Descending
        foreach ($d in $dirs) {
            $p = Join-Path $d.FullName "bin\$exe"
            if (Test-Path $p) { return $p }
        }
    }
    foreach ($pf in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        if (-not $pf) { continue }
        $base = Join-Path $pf 'Arm GNU Toolchain arm-none-eabi'
        if (-not (Test-Path $base)) { continue }
        foreach ($d in (Get-ChildItem $base -Directory -ErrorAction SilentlyContinue | Sort-Object Name -Descending)) {
            $p = Join-Path $d.FullName "bin\$exe"
            if (Test-Path $p) { return $p }
        }
    }
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }

    throw "$exe not found. Looked under: $($script:ToolRoots -join ', '), Program Files, and PATH. Add the root to _tools.ps1."
}

# ============================================================================================
# ADDITIONS (2026-07-27, second machine / parallel line). Everything above is unchanged, so
# flash/unbrick/unbrick_flash/dbg keep working exactly as before. Added here:
#   * environment-variable + per-machine-file override  (config out of version control)
#   * resolvers the block above did not cover: openocd scripts dir as a string, ARM bin dir,
#     make dir, MSPM0 SDK root, SysConfig CLI      <- syscfg_check.ps1 / sdk_find.ps1 need these
#     (they were still hardcoding C:\ti and therefore still dead on the other machine)
#   * version getters, consumed by env_check.ps1 (the doctor) and toolchain.lock (version pin)
#
# WHY the override layer matters (this is the one design point added on top of $ToolRoots):
#   $ToolRoots is a TRACKED list, so adapting a new machine means editing a committed file ->
#   machine config in version control, which is the anti-pattern this whole file fights.
#   Industry shape (Android local.properties / Node .env.local / CMakeUserPresets.json):
#       values stay local + gitignored,  the TEMPLATE is committed.
#   Order: env var  ->  _tools.local.ps1 (gitignored)  ->  $ToolRoots/globs  ->  throw.
#   Template: _tools.local.example.ps1 . Report for your machine: .\env_check.ps1
#
# ONE DELIBERATE DIFFERENCE from the two functions above: they fall back to PATH as a last
# resort. That is acceptable for openocd/gdb, but NOT for the SDK and SysConfig - picking a
# different SysConfig than the SDK expects silently changes GENERATED code, i.e. same source
# compiles to a different binary and nothing reports it. So the resolvers below throw instead.
# ============================================================================================

# Per-machine persistent override, not tracked by git. Sourced here so the env vars it sets are
# visible to every resolver below (and it can also append to $script:ToolRoots if it wants).
$_toolsLocal = Join-Path $PSScriptRoot '_tools.local.ps1'
if (Test-Path -LiteralPath $_toolsLocal) { . $_toolsLocal }

function _tools_FirstExisting {
    param([string[]]$Candidates)
    foreach ($c in $Candidates) {
        if ([string]::IsNullOrWhiteSpace($c)) { continue }
        if ($c -match '[\*\?]') {
            $hit = Get-Item -Path $c -ErrorAction SilentlyContinue |
                   Sort-Object FullName -Descending | Select-Object -First 1
            if ($hit) { return $hit.FullName }
        } elseif (Test-Path -LiteralPath $c) {
            return (Resolve-Path -LiteralPath $c).Path
        }
    }
    return $null
}

function _tools_Fail {
    param([string]$What, [string]$EnvVar, [string[]]$Tried)
    $msg  = "TOOLCHAIN NOT FOUND: $What`n  Looked in:`n"
    foreach ($t in $Tried) { $msg += "    $t`n" }
    $msg += "  Fix (pick one):`n"
    $msg += "    A) `$env:$EnvVar = '<correct path>'   (this shell only)`n"
    $msg += "    B) put that line into  car\_tools.local.ps1  (gitignored, persists)`n"
    $msg += "    C) install the tool under one of the roots in `$ToolRoots at the top of _tools.ps1`n"
    $msg += "  Then run:  .\env_check.ps1"
    throw $msg
}

function _tools_Roots {
    # env override wins, then whatever the top of this file declared
    $r = @()
    if ($env:DIANSAI_TOOL_ROOTS) { $r += ($env:DIANSAI_TOOL_ROOTS -split ';') }
    $r += $script:ToolRoots
    return ($r | Where-Object { $_ })
}

function Find-OpenocdScripts {
    # string form of (Find-Openocd).Scripts, so callers that just want the -s argument are simple
    if ($env:DIANSAI_OPENOCD_ROOT) {
        $s = _tools_FirstExisting @((Join-Path $env:DIANSAI_OPENOCD_ROOT 'openocd\scripts'),
                                    (Join-Path $env:DIANSAI_OPENOCD_ROOT 'share\openocd\scripts'))
        if ($s) { return ($s -replace '\\', '/') }
    }
    return (Find-Openocd).Scripts
}

function Find-ArmBin {
    $tried = @($env:DIANSAI_ARM_BIN)
    foreach ($root in (_tools_Roots)) {
        $tried += (Join-Path $root '*arm*\bin'); $tried += (Join-Path $root '*arm*\*\bin')
    }
    $tried += 'C:\Program Files (x86)\Arm GNU Toolchain arm-none-eabi\*\bin'
    $tried += 'C:\Program Files\Arm GNU Toolchain arm-none-eabi\*\bin'
    $r = _tools_FirstExisting $tried
    if ($r) { return $r }
    # derive it from the (already PATH-aware) Find-ArmTool so we never disagree with it
    try { return (Split-Path (Find-ArmTool arm-none-eabi-gcc) -Parent) } catch { }
    _tools_Fail 'ARM GNU toolchain bin dir' 'DIANSAI_ARM_BIN' $tried
}

function Find-MakeBin {
    $tried = @($env:DIANSAI_MAKE_BIN)
    foreach ($root in (_tools_Roots)) { $tried += (Join-Path $root '*mingw64\bin'); $tried += (Join-Path $root 'build-tools') }
    $tried += "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs*\mingw64\bin"
    $tried += 'C:\msys64\mingw64\bin'
    $r = _tools_FirstExisting $tried
    if ($r) { return $r }
    # make does not affect generated code, so PATH is an acceptable fallback here
    $onPath = Get-Command mingw32-make.exe, make.exe -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($onPath) { return (Split-Path $onPath.Source -Parent) }
    _tools_Fail 'mingw32-make.exe / make.exe dir' 'DIANSAI_MAKE_BIN' $tried
}

function Find-SdkRoot {
    $tried = @($env:MSPM0_SDK_INSTALL_DIR)
    foreach ($root in (_tools_Roots)) { $tried += (Join-Path $root 'mspm0-sdk'); $tried += (Join-Path $root 'mspm0_sdk*') }
    $r = _tools_FirstExisting $tried
    if (-not $r) { _tools_Fail 'MSPM0 SDK root' 'MSPM0_SDK_INSTALL_DIR' $tried }
    return $r
}

function Find-SysConfigCli {
    # AUTHORITATIVE SOURCE = the SDK's own imports.mak SYSCONFIG_TOOL. A machine can carry several
    # SysConfig installs; building with one the SDK does not expect changes the generated
    # ti_msp_dl_config.c => different binary from identical source, with no warning anywhere.
    if ($env:DIANSAI_SYSCONFIG_CLI -and (Test-Path -LiteralPath $env:DIANSAI_SYSCONFIG_CLI)) {
        return (Resolve-Path -LiteralPath $env:DIANSAI_SYSCONFIG_CLI).Path
    }
    $im = Join-Path (Find-SdkRoot) 'imports.mak'
    if (Test-Path -LiteralPath $im) {
        $ln = Select-String -LiteralPath $im -Pattern '^\s*SYSCONFIG_TOOL\s*\??=\s*(.+)$' -Encoding UTF8 | Select-Object -First 1
        if ($ln) {
            $p = $ln.Matches[0].Groups[1].Value.Trim() -replace '/', '\'
            if (Test-Path -LiteralPath $p) { return (Resolve-Path -LiteralPath $p).Path }
        }
    }
    $tried = @('(SDK imports.mak SYSCONFIG_TOOL)')
    foreach ($root in (_tools_Roots)) { $tried += (Join-Path $root 'sysconfig*\sysconfig_cli.bat') }
    $r = _tools_FirstExisting ($tried | Where-Object { $_ -notlike '(*' })
    if (-not $r) { _tools_Fail 'sysconfig_cli.bat' 'DIANSAI_SYSCONFIG_CLI' $tried }
    return $r
}

# ---- version getters (used by env_check.ps1 to compare against toolchain.lock) --------------
function Get-SdkVersion {
    $pj = Join-Path (Find-SdkRoot) '.metadata\product.json'
    if (-not (Test-Path -LiteralPath $pj)) { return $null }
    try { return ((Get-Content -LiteralPath $pj -Raw -Encoding UTF8) | ConvertFrom-Json).version } catch { return $null }
}
function Get-SysConfigVersion {
    $p = Find-SysConfigCli
    $m = [regex]::Match($p, 'sysconfig[_-]?([0-9][0-9\.]*)')
    if ($m.Success) { return $m.Groups[1].Value }
    return (Split-Path (Split-Path $p -Parent) -Leaf)
}
function Get-ArmGccVersion {
    try {
        $o = & (Find-ArmTool arm-none-eabi-gcc) --version 2>&1 | Select-Object -First 1
        $m = [regex]::Match($o, '\)\s+([0-9][0-9\.]*)')
        if ($m.Success) { return $m.Groups[1].Value }
        return $o
    } catch { return $null }
}
