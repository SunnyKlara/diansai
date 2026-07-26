param(
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $projectRoot $Configuration
$sdk = "C:\ti\mspm0_sdk_2_10_00_04"
$sysconfig = "C:\ti\sysconfig_1.26.2\sysconfig_cli.bat"
$compiler = "C:\ti\ccs2020\ccs\tools\compiler\ti-cgt-armllvm_4.0.3.LTS\bin\tiarmclang.exe"
$startup = Join-Path $sdk "source\ti\devices\msp\m0p\startup_system_files\ticlang\startup_mspm0g350x_ticlang.c"

if (-not (Test-Path -LiteralPath $buildDir)) {
    New-Item -ItemType Directory -Path $buildDir | Out-Null
}

& $sysconfig --script (Join-Path $projectRoot "empty.syscfg") -o $buildDir -s (Join-Path $sdk ".metadata\product.json") --compiler ticlang
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$common = @(
    "@device.opt",
    "-march=thumbv6m",
    "-mcpu=cortex-m0plus",
    "-mfloat-abi=soft",
    "-mlittle-endian",
    "-mthumb",
    "-O2",
    "-I$projectRoot",
    "-I$buildDir",
    "-I$sdk\source\third_party\CMSIS\Core\Include",
    "-I$sdk\source",
    "-gdwarf-3",
    "-Wall"
)

$sources = @(
    (Join-Path $projectRoot "empty.c"),
    (Join-Path $buildDir "ti_msp_dl_config.c"),
    $startup
)
$sources += Get-ChildItem -LiteralPath (Join-Path $projectRoot "APP") -Filter "*.c" | Select-Object -ExpandProperty FullName
$sources += Get-ChildItem -LiteralPath (Join-Path $projectRoot "BSP") -Filter "*.c" | Select-Object -ExpandProperty FullName

$objects = @()
Push-Location $buildDir
try {
    foreach ($source in $sources) {
        $object = [System.IO.Path]::GetFileNameWithoutExtension($source) + ".o"
        $objects += $object
        & $compiler (@("-c") + $common + @("-o", $object, $source))
        if ($LASTEXITCODE -ne 0) {
            exit $LASTEXITCODE
        }
    }

    $link = @(
        "@device.opt",
        "-march=thumbv6m",
        "-mcpu=cortex-m0plus",
        "-mfloat-abi=soft",
        "-mlittle-endian",
        "-mthumb",
        "-O2",
        "-gdwarf-3",
        "-Wall",
        "-Wl,-m2024hVibe.map",
        "-Wl,-i$sdk\source",
        "-Wl,-i$projectRoot",
        "-Wl,-i$buildDir\syscfg",
        "-Wl,-iC:\ti\ccs2020\ccs\tools\compiler\ti-cgt-armllvm_4.0.3.LTS\lib",
        "-Wl,--diag_wrap=off",
        "-Wl,--display_error_number",
        "-Wl,--warn_sections",
        "-Wl,--xml_link_info=2024hVibe_linkInfo.xml",
        "-Wl,--rom_model",
        "-o",
        "2024hVibe.out"
    ) + $objects + @(
        "-Wl,-l./device_linker.cmd",
        "-Wl,-ldevice.cmd.genlibs",
        "-Wl,-llibc.a"
    )

    & $compiler $link
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
