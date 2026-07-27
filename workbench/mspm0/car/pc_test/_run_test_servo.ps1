# Build + run the servo PC unit test. ASCII-only by repo rule.
# Reuses the same host gcc that ships next to the WinLibs mingw32-make used by build.ps1.
$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here

. (Join-Path $here '..\_tools.ps1')

$gcc = $null
try {
    $makeBin = Find-MakeBin
    $cand = Join-Path $makeBin 'gcc.exe'
    if (Test-Path $cand) { $gcc = $cand }
} catch { }

if (-not $gcc) {
    foreach ($p in @(
        "$env:LOCALAPPDATA\Programs\mingw64\bin\gcc.exe",
        'C:\mingw64\bin\gcc.exe',
        'C:\msys64\mingw64\bin\gcc.exe')) {
        if (Test-Path $p) { $gcc = $p; break }
    }
}
if (-not $gcc) { throw 'host gcc.exe not found (looked next to mingw32-make and in common mingw64 roots)' }

Write-Output "gcc = $gcc"
& $gcc -O2 -Wall -Wextra -I.. -o test_servo.exe test_servo.c 2>&1 | ForEach-Object { Write-Output $_ }
if ($LASTEXITCODE -ne 0) { Write-Output "RESULT: FAIL - compile error"; exit 1 }

& (Join-Path $here 'test_servo.exe') 2>&1 | ForEach-Object { Write-Output $_ }
$rc = $LASTEXITCODE
Write-Output "RESULT: $(if ($rc -eq 0) {'PASS'} else {'FAIL'}) - test exit=$rc"
exit $rc
