# ASCII only (PowerShell 5.1 encoding pitfall).
#
# Create the two ESP-Hosted projects needed for "P4 host + C5 co-processor WiFi".
#
#   c5_sta_cp    -> runs on the on-board ESP32-C5   (esp_hosted "cp" = co-processor / slave)
#   p4_sta_host  -> runs on the ESP32-P4            (esp_hosted "mcu_host")
#
# Naming note (cost me one failed attempt): esp_hosted 2.x had a single example
# called "slave"; from 3.0.0 the component was restructured into paired examples
# "<feature>/mcu_host" + "<feature>/cp" and "slave" no longer exists.
#   idf.py create-project-from-example 'espressif/esp_hosted:slave'
#     -> ERROR: Cannot find example "slave" for "espressif/esp_hosted" version "*"
# Ground truth for example names = the component registry API:
#   curl https://components.espressif.com/api/components/espressif/esp_hosted
#   (.versions[] | .version, .targets, .examples[].name)
# esp_hosted 3.0.5 (2026-07-23) needs IDF >= 5.5 and lists esp32c5 in .targets;
# 2.x does NOT list esp32c5 -> 3.x is mandatory for this board.

param(
    [string]$Root    = "d:\diansai\workbench\esp32p4",
    [string]$Version = "3.0.5",
    # feature/example family: wifi/sta gives connect + DHCP (enough to ping),
    # wifi/scan is the smaller "does the transport work at all" test.
    [string]$Feature = "wifi/sta"
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot\idf_shell.ps1"

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Output "CREATE_ABORT: idf.py not available (IDF env not activated)"
    exit 90
}

New-Item -ItemType Directory -Force -Path $Root | Out-Null
Set-Location $Root

# create-project-from-example names the new folder after the LAST path segment
# ("cp" / "mcu_host"), which is useless with two of them side by side -> rename.
$tag  = ($Feature -replace '[/\\]', '_')     # "wifi/scan" -> "wifi_scan"
$tag  = $tag -replace '^wifi_', ''           # keep folder names short
$jobs = @(
    @{ Example = "$Feature/cp";       Created = "cp";       Final = "c5_${tag}_cp"   },
    @{ Example = "$Feature/mcu_host"; Created = "mcu_host"; Final = "p4_${tag}_host" }
)

foreach ($j in $jobs) {
    $final = $j.Final
    if (Test-Path $final) {
        Write-Output "SKIP: $final already exists"
        continue
    }
    if (Test-Path $j.Created) {
        Write-Output "CREATE_ABORT: stale folder '$($j.Created)' in the way"
        exit 91
    }
    $spec = "espressif/esp_hosted^${Version}:$($j.Example)"
    Write-Output "=== create-project-from-example $spec ==="
    idf.py create-project-from-example $spec
    $code = $LASTEXITCODE
    Write-Output "EXIT=$code"
    if ($code -ne 0) { exit $code }
    if (-not (Test-Path $j.Created)) {
        Write-Output "CREATE_ABORT: expected folder '$($j.Created)' was not created"
        exit 92
    }
    Rename-Item -Path $j.Created -NewName $final
    Write-Output "OK: $($j.Example) -> $Root\$final"
}

Write-Output "=== RESULT ==="
Get-ChildItem $Root -Directory | ForEach-Object { "  $($_.Name)" }
