# ASCII only (PowerShell 5.1 encoding pitfall).
#
# Fetch ESP-IDF tool archives with curl.exe instead of idf_tools.py's own downloader.
#
# WHY THIS EXISTS (2026-07-27, this machine):
#   idf_tools.py could not download anything:
#     1st symptom: "urlopen error [Errno 2] No such file or directory"
#        -> root cause was NOT the network: the bundled idf-python 3.11.2 has NO CA bundle
#           (ssl.get_default_verify_paths().cafile is None, certifi missing), so loading the
#           cert file raised ENOENT and got wrapped into a misleading Errno 2.
#     2nd symptom (after pointing SSL_CERT_FILE at git's ca-bundle.crt):
#        "EOF occurred in violation of protocol (_ssl.c:992)"
#        -> Python's TLS handshake to dl.espressif.cn gets torn down on this link,
#           while curl.exe on the very same URL sustains ~3.4 MB/s.
#   idf_tools.py reuses any archive already present in $IDF_TOOLS_PATH\dist as long as
#   size + sha256 match, so we side-step its downloader entirely.
#
# Input TSV columns: name <TAB> version <TAB> filename <TAB> size <TAB> sha256 <TAB> url
# Generate it with gen_tool_urls.py (parses IDF's own tools/tools.json).

param(
    [string]$Tsv  = "d:\diansai\.tmp_pdf\esp32p4\tool_dl.tsv",
    [string]$Dist = "D:\esp32\Espressif\dist"
)

$ErrorActionPreference = "Continue"
New-Item -ItemType Directory -Force -Path $Dist | Out-Null

$rows = Get-Content $Tsv | Where-Object { $_.Trim() -ne "" }
Write-Output "TOOLS_TO_FETCH=$($rows.Count)"

$failed = @()
foreach ($line in $rows) {
    $c = $line -split "`t"
    $name = $c[0]; $fname = $c[2]; $size = [int64]$c[3]; $sha = $c[4].ToLower(); $url = $c[5]
    $dest = Join-Path $Dist $fname

    # Skip if a byte-identical copy is already there
    if ((Test-Path $dest) -and ((Get-Item $dest).Length -eq $size)) {
        $h = (Get-FileHash $dest -Algorithm SHA256).Hash.ToLower()
        if ($h -eq $sha) { Write-Output "SKIP  $name ($fname already valid)"; continue }
    }

    Write-Output "FETCH $name -> $fname ($([math]::Round($size/1MB,1)) MB)"
    & curl.exe -L --retry 8 --retry-delay 3 -C - --progress-bar -o $dest $url

    if (-not (Test-Path $dest)) { Write-Output "FAIL  $name (no file)"; $failed += $name; continue }
    $len = (Get-Item $dest).Length
    $h = (Get-FileHash $dest -Algorithm SHA256).Hash.ToLower()
    if ($len -ne $size) { Write-Output "FAIL  $name (size $len != $size)"; $failed += $name; continue }
    if ($h -ne $sha)    { Write-Output "FAIL  $name (sha256 mismatch)";    $failed += $name; continue }
    Write-Output "OK    $name (size + sha256 verified)"
}

# idf_tools.py does NOT always look for os.path.basename(url) in dist/.
# When the upstream archive name carries no version (e.g. ninja-win.zip) it expects
# "<stem>-v<version><ext>" (ninja-win-v1.12.1.zip). Drop an extra copy under that name
# so the installer finds it either way. Cheap, and beats a failed install at 95%.
foreach ($line in $rows) {
    $c = $line -split "`t"
    $name = $c[0]; $ver = $c[1]; $fname = $c[2]
    if ($fname -like "*$ver*") { continue }        # version already in the name, nothing to do
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($fname)
    $ext  = [System.IO.Path]::GetExtension($fname)
    $alias = "$stem-v$ver$ext"
    $src = Join-Path $Dist $fname
    $dst = Join-Path $Dist $alias
    if ((Test-Path $src) -and (-not (Test-Path $dst))) {
        Copy-Item $src $dst -Force
        Write-Output "ALIAS $name -> $alias (idf_tools expects the versioned name)"
    }
}

if ($failed.Count -gt 0) { Write-Output ("FETCH_FAILED=" + ($failed -join ",")) }
else { Write-Output "FETCH_ALL_OK" }
