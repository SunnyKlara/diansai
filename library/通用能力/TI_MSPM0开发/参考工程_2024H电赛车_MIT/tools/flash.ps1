param(
    [string]$Program = "",
    [int]$SpeedKHz = 500
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$openocd = "C:\Enviroment\openocd_1.3.1.50\bin\openocd.exe"
if (-not $Program) {
    $Program = Join-Path $projectRoot "Debug\2024hVibe.out"
}
$programTcl = $Program.Replace("\", "/")

function Invoke-CheckedOpenOcd {
    param([string[]]$Arguments, [string]$Stage)

    $previousPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $output = (& $openocd @Arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $previousPreference
    Write-Host $output
    if (($exitCode -ne 0) -or
        ($output -match "(?m)^Error:") -or
        ($output -match "(?m)^diff \d+ address")) {
        throw "$Stage failed"
    }
}

$common = @(
    "-f", "interface/cmsis-dap.cfg",
    "-c", "adapter speed $SpeedKHz"
)

Invoke-CheckedOpenOcd -Stage "Flash write" -Arguments ($common + @(
    "-f", "target/ti_mspm0.cfg",
    "-c", "init; reset halt; wait_halt 5000; flash write_image erase {$programTcl}; reset halt; wait_halt 5000; shutdown"
))

# Disable the Cortex-M work area so verify_image compares flash bytes directly
# instead of running the unreliable target-side CRC helper over wireless DAPLink.
Invoke-CheckedOpenOcd -Stage "Direct flash verify" -Arguments ($common + @(
    "-c", "set WORKAREASIZE 0",
    "-f", "target/ti_mspm0.cfg",
    "-c", "init; reset halt; wait_halt 5000; verify_image {$programTcl}; reset run; shutdown"
))

Write-Host "Flash write, direct verify, and reset run completed."
