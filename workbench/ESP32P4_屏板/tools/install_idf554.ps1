# ASCII only (PowerShell 5.1 encoding pitfall).
# Install ESP-IDF v5.5.4 toolchain for ESP32-P4 on this machine.
#
# Layout notes (this machine, 2026-07-27):
#   - An official ESP-IDF Windows installer already put v5.1.2 at D:\esp32\Espressif
#   - We REUSE that IDF_TOOLS_PATH so cmake/ninja/openocd/ccache are shared,
#     and only the new riscv toolchain + a new python venv get downloaded.
#   - IDF source came from the release zip on the China CDN (dl.espressif.cn),
#     NOT from `git clone --recursive` (GitHub submodules ran at ~40 KiB/s here).
#
# China mirrors used:
#   IDF_GITHUB_ASSETS -> dl.espressif.cn/github_assets   (tool downloads)
#   PIP_INDEX_URL     -> tsinghua pypi                    (python deps)

$ErrorActionPreference = "Continue"

$env:IDF_PATH          = "D:\esp32\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH    = "D:\esp32\Espressif"
$env:IDF_GITHUB_ASSETS = "dl.espressif.cn/github_assets"
# pip index: measured on 2026-07-27 from idf-python's own urlopen --
#   aliyun OK / pypi.org OK / tsinghua FAIL / tencent FAIL / ustc FAIL
#   (the failures are all "EOF occurred in violation of protocol" = SNI-level interference
#    on this link, not a config problem; curl.exe reaches the same hosts fine)
$env:PIP_INDEX_URL     = "https://mirrors.aliyun.com/pypi/simple/"
# The bundled idf-python 3.11.2 ships with NO CA bundle -> urlopen raises a misleading
# "[Errno 2] No such file or directory". Point it at git's ca-bundle so HTTPS works at all.
$env:SSL_CERT_FILE     = "D:\esp32\Espressif\tools\idf-git\2.43.0\mingw64\etc\ssl\certs\ca-bundle.crt"
# NOTE: even with certs fixed, Python's TLS to dl.espressif.cn dies with
# "EOF occurred in violation of protocol" on this link, so the tool archives are
# pre-fetched with curl into $IDF_TOOLS_PATH\dist by fetch_idf_tools_via_curl.ps1.
# idf_tools.py then reuses them (size + sha256 match) without touching the network.

# Prefer the installer-bundled python 3.11.2 over the system Python 3.11.0rc2
$env:PATH = "D:\esp32\Espressif\tools\idf-python\3.11.2;D:\esp32\Espressif\tools\idf-git\2.43.0\cmd;" + $env:PATH

Write-Output "IDF_PATH       = $($env:IDF_PATH)"
Write-Output "IDF_TOOLS_PATH = $($env:IDF_TOOLS_PATH)"
& python --version
Write-Output "--- running install.ps1 esp32p4 ---"

Set-Location $env:IDF_PATH
& .\install.ps1 esp32p4
Write-Output "INSTALL_EXIT=$LASTEXITCODE"
