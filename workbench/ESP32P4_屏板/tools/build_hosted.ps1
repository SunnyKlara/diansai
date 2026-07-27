# ASCII only (PowerShell 5.1 encoding pitfall).
#
# set-target + build for the ESP-Hosted bring-up projects under workbench\esp32p4:
#
#   c5_cp         target esp32c5   co-processor (radio) firmware   -> flash via USB-TTL on the 6P header
#   p4_scan_host  target esp32p4   host app: AP scan  (NO credentials needed)
#   p4_sta_host   target esp32p4   host app: connect to AP + DHCP  (needs SSID/password)
#
# NOTE on set-target: it is REQUIRED here (fresh projects, no sdkconfig yet) and safe.
# The repo's "never run set-target" rule is specific to workbench\esp32p4\p4_lcd, whose
# checked-in sdkconfig carries the anti-tearing panel settings. Do not confuse the two.
#
# -Flash writes over the given port. For p4_* that is the P4's USB port (COM7 by default).
# The C5 CANNOT be flashed over the P4's USB port -- it needs a USB-TTL adapter on the
# 6P header (3V3 / GND / C5_TXD0 / C5_RXD0 / C5_EN / C5_BOOT).

param(
    [ValidateSet("c5_cp", "p4_scan_host", "p4_sta_host", "p4_softap_host", "p4_cp_ota", "all")]
    [string]$Project = "all",
    [string]$Root    = "d:\diansai\workbench\esp32p4",
    [string]$Port    = "COM7",
    [switch]$Flash,
    [switch]$Clean
)

$ErrorActionPreference = "Continue"
. "$PSScriptRoot\idf_shell.ps1"

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Output "BUILD_ABORT: idf.py not available (IDF env not activated)"
    exit 90
}

$targets = @{ "c5_cp" = "esp32c5"; "p4_scan_host" = "esp32p4"; "p4_sta_host" = "esp32p4"; "p4_softap_host" = "esp32p4"; "p4_cp_ota" = "esp32p4" }
$list = if ($Project -eq "all") { @("c5_cp", "p4_scan_host", "p4_sta_host", "p4_softap_host", "p4_cp_ota") } else { @($Project) }

$fail = 0
foreach ($p in $list) {
    $dir = Join-Path $Root $p
    $tgt = $targets[$p]
    Write-Output ""
    Write-Output "################ $p  (target $tgt) ################"
    if (-not (Test-Path $dir)) {
        Write-Output "SKIP_MISSING: $dir"
        $fail = 1
        continue
    }
    Set-Location $dir

    if ($Clean) { Remove-Item -Recurse -Force "build", "sdkconfig" -ErrorAction SilentlyContinue }

    # set-target only when the project has no sdkconfig yet, or the target changed.
    $needSetTarget = $true
    if (Test-Path "sdkconfig") {
        $cur = Select-String -Path "sdkconfig" -Pattern '^CONFIG_IDF_TARGET="(.*)"$' -ErrorAction SilentlyContinue
        if ($cur -and $cur.Matches[0].Groups[1].Value -eq $tgt) { $needSetTarget = $false }
    }
    if ($needSetTarget) {
        Write-Output "--- idf.py set-target $tgt ---"
        idf.py set-target $tgt
        Write-Output "SET_TARGET_EXIT=$LASTEXITCODE"
        if ($LASTEXITCODE -ne 0) { $fail = 1; continue }
    } else {
        Write-Output "--- target already $tgt, skipping set-target ---"
    }

    Write-Output "--- idf.py build ---"
    idf.py build
    $code = $LASTEXITCODE
    Write-Output "BUILD_EXIT[$p]=$code"
    if ($code -ne 0) { $fail = 1; continue }

    # verdict must rest on the artifact, not on stdout (this machine has swallowed
    # tool output before and still returned exit 0)
    $bin = Get-ChildItem "build\*.bin" -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -notmatch 'bootloader|partition' } | Select-Object -First 1
    if ($bin) {
        Write-Output ("ARTIFACT[$p]=" + $bin.Name + "  " + [int]($bin.Length / 1024) + " KB  " + $bin.LastWriteTime)
    } else {
        Write-Output "ARTIFACT[$p]=MISSING"
        $fail = 1
    }

    if ($Flash) {
        if ($p -eq "c5_cp") {
            Write-Output "FLASH_SKIP: c5_cp must be flashed through a USB-TTL adapter on the 6P header, not $Port"
        } else {
            idf.py -p $Port flash
            Write-Output "FLASH_EXIT[$p]=$LASTEXITCODE"
        }
    }
}

Write-Output ""
if ($fail -eq 0) { Write-Output "RESULT: PASS" } else { Write-Output "RESULT: FAIL" }
exit $fail
