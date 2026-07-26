# ASCII only (PowerShell 5.1 encoding pitfall).
#
# Dot-source this file to get a working ESP-IDF v5.5.4 environment in the current session:
#     . d:\diansai\workbench\ESP32P4_屏板\tools\idf_shell.ps1
#
# Machine facts (2026-07-27):
#   v5.5.4 sources  : D:\esp32\Espressif\frameworks\esp-idf-v5.5.4   (from release zip, has .git)
#   shared tools    : D:\esp32\Espressif                             (also serves the older v5.1.2)
#   v5.1.2 still installed side by side -- do NOT rely on the machine-wide IDF_PATH,
#   it points at v5.1.2 which cannot build ESP32-P4. Always dot-source this first.
#
# Network workarounds baked in (all measured, see tools/*.log and the repo pitfall log):
#   - SSL_CERT_FILE: bundled idf-python has no CA bundle at all
#   - PIP_INDEX_URL: aliyun (tsinghua/tencent/ustc all die with TLS "EOF in violation of protocol")

$env:IDF_PATH          = "D:\esp32\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH    = "D:\esp32\Espressif"
$env:IDF_GITHUB_ASSETS = "dl.espressif.cn/github_assets"
$env:PIP_INDEX_URL     = "https://mirrors.aliyun.com/pypi/simple/"
$env:SSL_CERT_FILE     = "D:\esp32\Espressif\tools\idf-git\2.43.0\mingw64\etc\ssl\certs\ca-bundle.crt"

# CRITICAL on this machine: the Windows user name is non-ASCII ("C:\Users\<chinese>").
# export.ps1 writes a temporary .ps1 into $TEMP and dot-sources it; the non-ASCII path
# gets mangled in transit (PS 5.1 decodes python's UTF-8 output as ANSI) and the
# dot-source fails with "ObjectNotFound: C:\Users\????\...ps1".
# Redirecting TEMP/TMP to an ASCII path sidesteps it entirely.
$asciiTmp = "D:\esp32\tmp"
New-Item -ItemType Directory -Force -Path $asciiTmp | Out-Null
$env:TMP  = $asciiTmp
$env:TEMP = $asciiTmp

. "$env:IDF_PATH\export.ps1"

if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
    Write-Output "IDF_SHELL_FAIL: export.ps1 ran but idf.py is still not on PATH"
    exit 90
}
Write-Output "IDF_SHELL_OK: $(idf.py --version)"
