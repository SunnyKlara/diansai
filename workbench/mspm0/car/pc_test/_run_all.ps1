# =====================================================================
#  _run_all.ps1 - build and run EVERY pure-algorithm unit test in this
#  folder, then print one table: test file / assertions passed / verdict.
#
#  Why this exists: the design report claims a per-module verification
#  level (Appendix C). That claim must be reproducible by one command,
#  otherwise it is just a sentence. Run this, read the last table.
#
#  ASCII-only by repo rule (PowerShell 5.1 parses .ps1 as ANSI/GBK, so a
#  UTF-8 file with CJK comments fails to PARSE, not just to display).
#
#  Usage : powershell -ExecutionPolicy Bypass -File _run_all.ps1
#  Output: console table + _all_tests.txt (the verdict, for the agent to read)
# =====================================================================
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# ---- locate gcc (same probe order as _tools.ps1 in car/) --------------
$gcc = (Get-Command gcc -ErrorAction SilentlyContinue).Source
if (-not $gcc) {
    $cands = @(
        "$env:LOCALAPPDATA\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin\gcc.exe",
        "C:\msys64\mingw64\bin\gcc.exe",
        "C:\mingw64\bin\gcc.exe"
    )
    foreach ($c in $cands) { if (Test-Path $c) { $gcc = $c; break } }
}
if (-not $gcc) { Write-Host "[X] gcc not found" -ForegroundColor Red; exit 1 }
Write-Host "gcc = $gcc" -ForegroundColor DarkGray

# ---- which .c does each test need linked in? -------------------------
#      (header-only modules need nothing: cmd_gate / vservo are static inline)
$LINK = @{
    "test_attitude.c"   = @("..\attitude.c")
    "test_ball.c"       = @("..\ball.c")
    "test_beep.c"       = @("..\beep.c")
    "test_cmd_gate.c"   = @()
    "test_disp_run.c"   = @("..\disp_run.c")
    "test_line.c"       = @("..\line.c")
    "test_lineframe.c"  = @("..\lineframe.c")
    "test_linesens.c"   = @()
    # nav.c calls attitude_wrap180() -> attitude.c must come along
    "test_nav.c"        = @("..\nav.c", "..\attitude.c")
    # servo.c includes ti_msp_dl_config.h (HAL) and CANNOT build on PC;
    # test_servo.c therefore tests the pure pulse-width math on its own.
    "test_servo.c"      = @()
    "test_task.c"       = @("..\task.c")
    "test_uart_frame.c" = @("..\uart_frame.c")
}

$rows = @()
$anyFail = $false

foreach ($t in (Get-ChildItem -File -Filter "test_*.c" | Sort-Object Name)) {
    $name = $t.Name
    $exe  = [System.IO.Path]::ChangeExtension($name, ".exe")
    $deps = if ($LINK.ContainsKey($name)) { $LINK[$name] } else { @() }

    $args = @("-O2", "-Wall", "-I..", "-o", $exe, $name) + $deps + @("-lm")
    $prevEAP = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    $buildOut = & $gcc @args 2>&1
    $bc = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP

    if ($bc -ne 0) {
        $rows += [pscustomobject]@{ test = $name; pass = 0; fail = 0; verdict = "BUILD FAIL" }
        $anyFail = $true
        Write-Host "  [X] $name : build failed" -ForegroundColor Red
        $buildOut | Select-Object -First 6 | ForEach-Object { Write-Host "      $_" }
        continue
    }

    $ErrorActionPreference = "Continue"
    $out = & ".\$exe" 2>&1
    $rc  = $LASTEXITCODE
    $ErrorActionPreference = $prevEAP

    $txt  = ($out | Out-String)
    $np   = ([regex]::Matches($txt, "\[PASS\]")).Count
    $nf   = ([regex]::Matches($txt, "\[FAIL\]")).Count
    # Not every test prints per-assertion [PASS] lines. Two other formats are
    # in use in this folder; parse them rather than reporting a bogus 0.
    if ($np -eq 0) {
        $m = [regex]::Match($txt, "passed\s*=\s*(\d+)\s+failed\s*=\s*(\d+)")
        if ($m.Success) { $np = [int]$m.Groups[1].Value; $nf = [int]$m.Groups[2].Value }
    }
    if ($np -eq 0) {
        # e.g. test_servo: "ALL PASS: 30/30 checks passed"
        $m = [regex]::Match($txt, "(\d+)\s*/\s*(\d+)\s*checks")
        if (-not $m.Success) { $m = [regex]::Match($txt, "(\d+)\s*/\s*(\d+)\s*(PASS|passed)") }
        if ($m.Success) { $np = [int]$m.Groups[1].Value }
    }
    $verdict = if ($rc -eq 0 -and $nf -eq 0) { "PASS" } else { "FAIL" }
    if ($verdict -ne "PASS") { $anyFail = $true }
    $rows += [pscustomobject]@{ test = $name; pass = $np; fail = $nf; verdict = $verdict }
}

$report = @()
$report += "=== pc_test: all pure-algorithm unit tests ==="
$report += ("{0,-22} {1,6} {2,6}  {3}" -f "test", "pass", "fail", "verdict")
foreach ($r in $rows) {
    $report += ("{0,-22} {1,6} {2,6}  {3}" -f $r.test, $r.pass, $r.fail, $r.verdict)
}
$report += ("total assertions passed : {0}" -f ($rows | Measure-Object -Property pass -Sum).Sum)
$report += ("files                   : {0}" -f $rows.Count)
$report += if ($anyFail) { "RESULT: FAIL" } else { "RESULT: PASS" }

$report | ForEach-Object { Write-Host $_ }
Set-Content -Path "_all_tests.txt" -Value $report -Encoding UTF8
Remove-Item *.exe -ErrorAction SilentlyContinue
if ($anyFail) { exit 1 }
