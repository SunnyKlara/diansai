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
