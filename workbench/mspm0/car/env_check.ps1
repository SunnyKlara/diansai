# env_check.ps1 - "doctor" for this project. RUN THIS FIRST on any machine, and again at the
#                 contest venue as step 0 of the capability check.
#
# Pattern borrowed from the industry standard preflight tools (flutter doctor / rustup show /
# west doctor / pio system info): resolve every required tool, print ONE table, exit non-zero
# if anything is missing. The point is to turn "works on my machine" from a confusing failure
# in the middle of a flash into a 5-second answer before you start.
#
#   .\env_check.ps1              full report
#   .\env_check.ps1 -Quiet       only the RESULT line (for scripting)
#
# Exit codes:  0 = PASS (all tools resolved)   1 = FAIL (something missing)   2 = SKEW
#   SKEW = every tool was found, but a version differs from toolchain.lock. NOT fatal, but it
#   means this machine can produce a DIFFERENT binary from the same source (SysConfig is a code
#   generator; SDK carries driverlib source). See toolchain.lock for what to do about it.
#
# ASCII only (Windows PowerShell 5.1 reads .ps1 as ANSI).

param([switch]$Quiet)

Set-Location $PSScriptRoot
. "$PSScriptRoot\_tools.ps1"

$rows = @()
$fail = 0
$skew = 0

function Row {
    param([string]$Item, [scriptblock]$Resolve)
    $o = [ordered]@{ Item = $Item; Status = 'FAIL'; Value = '' }
    try {
        $v = & $Resolve
        if ($v) { $o.Status = 'OK'; $o.Value = "$v" } else { $o.Value = '(empty result)'; $script:fail++ }
    } catch {
        $o.Value = ($_.Exception.Message -split "`n")[0]
        $script:fail++
    }
    $script:rows += (New-Object psobject -Property $o)
    return $o.Value
}

if (-not $Quiet) {
    Write-Host ""
    Write-Host "==== diansai / MSPM0 environment check ====" -ForegroundColor Cyan
    Write-Host ("machine: {0}   user: {1}   {2}" -f $env:COMPUTERNAME, $env:USERNAME, (Get-Date -Format 'yyyy-MM-dd HH:mm')) -ForegroundColor Gray
    Write-Host ""
}

[void](Row 'openocd.exe'        { (Find-Openocd).Exe })
[void](Row 'openocd scripts'    { Find-OpenocdScripts })
[void](Row 'arm bin'            { Find-ArmBin })
[void](Row 'arm-none-eabi-gcc'  { Find-ArmTool arm-none-eabi-gcc })
[void](Row 'arm-none-eabi-size' { Find-ArmTool arm-none-eabi-size })
[void](Row 'arm-none-eabi-gdb'  { Find-ArmTool arm-none-eabi-gdb })
[void](Row 'mingw32-make dir'   { Find-MakeBin })
[void](Row 'MSPM0 SDK'          { Find-SdkRoot })
[void](Row 'sysconfig_cli.bat'  { Find-SysConfigCli })

# versions (only meaningful if the tools resolved)
$vSdk = $null; $vSys = $null; $vGcc = $null
if ($fail -eq 0) {
    $vSdk = Row 'SDK version'       { Get-SdkVersion }
    $vSys = Row 'SysConfig version' { Get-SysConfigVersion }
    $vGcc = Row 'gcc version'       { Get-ArmGccVersion }
}

# build artifact (not a tool, but the thing every flash judgement is compared against)
$outInfo = 'gcc\car.out MISSING (run mingw32-make in gcc/)'
if (Test-Path 'gcc\car.out') {
    try {
        $sz = & (Find-ArmTool arm-none-eabi-size) 'gcc\car.out' | Select-Object -Last 1
        $p  = ($sz -split '\s+') | Where-Object { $_ -ne '' }
        $outInfo = ("text+data = {0} bytes  (this is the 'wrote N bytes' value for THIS machine)" -f ([int]$p[0] + [int]$p[1]))
    } catch { $outInfo = 'present, size unavailable' }
}

if (-not $Quiet) {
    $rows | Format-Table -AutoSize | Out-String -Width 200 | Write-Host
    Write-Host ("car.out : " + $outInfo) -ForegroundColor Gray
}

# ---- version pin comparison (toolchain.lock) -------------------------------------------
$lock = Join-Path $PSScriptRoot 'toolchain.lock'
if ((Test-Path $lock) -and ($fail -eq 0)) {
    $want = @{}
    foreach ($l in (Get-Content -LiteralPath $lock -Encoding UTF8)) {
        if ($l -match '^\s*([A-Za-z_]+)\s*=\s*(\S+)') { $want[$Matches[1]] = $Matches[2] }
    }
    $cmp = @(
        @{ k='SDK';       want=$want['SDK'];       got=$vSdk },
        @{ k='SYSCONFIG'; want=$want['SYSCONFIG']; got=$vSys },
        @{ k='GCC';       want=$want['GCC'];       got=$vGcc }
    )
    if (-not $Quiet) { Write-Host ""; Write-Host "---- version pin (toolchain.lock) ----" -ForegroundColor Cyan }
    foreach ($c in $cmp) {
        if (-not $c.want) { continue }
        if ("$($c.got)" -eq "$($c.want)") {
            if (-not $Quiet) { Write-Host ("  {0,-10} MATCH  {1}" -f $c.k, $c.got) -ForegroundColor Green }
        } else {
            $skew++
            if (-not $Quiet) {
                Write-Host ("  {0,-10} SKEW   this machine = {1}   reference = {2}" -f $c.k, $c.got, $c.want) -ForegroundColor Yellow
            }
        }
    }
    if ($skew -gt 0 -and -not $Quiet) {
        Write-Host "  => Same source can compile to a DIFFERENT binary here than on the reference" -ForegroundColor Yellow
        Write-Host "     machine (SysConfig generates code; SDK carries driverlib source)." -ForegroundColor Yellow
        Write-Host "     Rule while this SKEW exists: the flashed binary must come from ONE machine only." -ForegroundColor Yellow
        Write-Host "     See toolchain.lock." -ForegroundColor Yellow
    }
}

Write-Host ""
if ($fail -gt 0) {
    Write-Host ("RESULT: FAIL - {0} tool(s) unresolved. Fix them before building or flashing." -f $fail) -ForegroundColor Red
    Write-Host "  Each FAIL row above prints where it looked; override with an env var or _tools.local.ps1." -ForegroundColor Yellow
    exit 1
}
if ($skew -gt 0) {
    Write-Host ("RESULT: SKEW - all tools present, {0} version(s) differ from toolchain.lock." -f $skew) -ForegroundColor Yellow
    exit 2
}
Write-Host "RESULT: PASS - all tools resolved, versions match the pin." -ForegroundColor Green
exit 0
