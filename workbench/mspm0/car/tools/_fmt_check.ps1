# _fmt_check.ps1 - static check that every -f format string matches its argument count.
#
# WHY THIS EXISTS
#   The bring-up and calibration scripts are full of `L ("... {0} ... {1}" -f $a, $b)`. A wrong
#   placeholder index or a missing argument does NOT show up in a syntax check - it throws at RUNTIME,
#   and only on the branch that happens to be taken. Which means the natural place to discover it is at
#   the bench, mid-measurement, on the error path you least wanted to exercise.
#   These scripts drive real hardware and their error branches are exactly the ones never rehearsed,
#   so a static sweep over every format call is worth more here than in ordinary code.
#
#   It works off the PowerShell AST rather than regex over source text, so it sees the real operator and
#   the real argument list instead of guessing where the arguments end.
#
# LIMITATIONS (stated so nobody over-trusts a PASS)
#   * only literal format strings are checked; a format string held in a variable is skipped and counted
#   * it checks COUNT and INDEX COVERAGE, not type/alignment correctness ({0:N1} on a string still passes)
#
# USAGE
#   powershell -NoProfile -ExecutionPolicy Bypass -File _fmt_check.ps1
#   powershell -NoProfile -ExecutionPolicy Bypass -File _fmt_check.ps1 -Files ball_ident.ps1
# EXIT CODES: 0 = all good, 1 = at least one mismatch.
# ASCII only on purpose.
param(
    [string[]]$Files = @()
)

if ($Files.Count -eq 0) {
    $Files = @(Get-ChildItem -Path $PSScriptRoot -Filter '*.ps1' | ForEach-Object { $_.Name })
}

$bad = 0; $checked = 0; $skipped = 0

foreach ($f in $Files) {
    $path = Join-Path $PSScriptRoot $f
    # A missing file is a FAILURE, not a warning. Reporting PASS while having checked nothing is the
    # worst possible outcome for a checker - it is a false green light, and this very script produced one
    # on its first run (powershell -File flattened the comma list into a single bogus name).
    if (-not (Test-Path $path)) { Write-Host ("  [FAIL] {0} not found - nothing was checked for it" -f $f) -ForegroundColor Red; $bad++; continue }
    $errs = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile($path, [ref]$null, [ref]$errs)
    if ($errs -and $errs.Count) { Write-Host ("  [FAIL] {0} does not parse ({1} error(s))" -f $f, $errs.Count) -ForegroundColor Red; $bad++; continue }

    # Every `X -f Y` in the file, at any nesting depth.
    $fmts = $ast.FindAll({ param($n)
        $n -is [System.Management.Automation.Language.BinaryExpressionAst] -and
        $n.Operator -eq [System.Management.Automation.Language.TokenKind]::Format }, $true)

    foreach ($fx in $fmts) {
        $lhs = $fx.Left
        # Only literal format strings can be checked. Expandable strings ("...$x...") are still literal
        # enough for placeholder counting - the {N} braces are not affected by interpolation.
        $text = $null
        if ($lhs -is [System.Management.Automation.Language.StringConstantExpressionAst]) { $text = $lhs.Value }
        elseif ($lhs -is [System.Management.Automation.Language.ExpandableStringExpressionAst]) { $text = $lhs.Value }
        if ($null -eq $text) { $skipped++; continue }

        # Drop {{ and }} first: those are literal braces, not placeholders.
        $probe = $text -replace '\{\{','' -replace '\}\}',''
        $idx = @([regex]::Matches($probe, '\{(\d+)') | ForEach-Object { [int]$_.Groups[1].Value })
        if ($idx.Count -eq 0) { continue }          # a -f with no placeholders: odd but not broken

        $rhs = $fx.Right
        $argc = if ($rhs -is [System.Management.Automation.Language.ArrayLiteralAst]) { $rhs.Elements.Count } else { 1 }

        $need = ($idx | Measure-Object -Maximum).Maximum + 1
        $checked++
        $line = $fx.Extent.StartLineNumber
        if ($need -gt $argc) {
            Write-Host ("  [FAIL] {0}:{1} uses {{0..{2}}} but only {3} argument(s) supplied" -f $f, $line, ($need-1), $argc) -ForegroundColor Red
            Write-Host ("         {0}" -f ($text.Substring(0, [Math]::Min(90, $text.Length))))
            $bad++
        } elseif ($argc -gt $need) {
            # Extra arguments are harmless to .NET but almost always mean a placeholder was deleted or
            # mistyped, so surface them as a warning rather than silence.
            Write-Host ("  [WARN] {0}:{1} supplies {2} argument(s) but the highest placeholder is {{{3}}}" -f $f, $line, $argc, ($need-1)) -ForegroundColor Yellow
            Write-Host ("         {0}" -f ($text.Substring(0, [Math]::Min(90, $text.Length))))
        }
        # Gaps: "{0} {2}" with 3 args silently drops one. Worth flagging for the same reason.
        for ($i = 0; $i -lt $need; $i++) {
            if ($idx -notcontains $i) {
                Write-Host ("  [WARN] {0}:{1} skips placeholder {{{2}}} (indexes present: {3})" -f $f, $line, $i, (($idx | Sort-Object -Unique) -join ',')) -ForegroundColor Yellow
            }
        }
    }
}

Write-Host ""
Write-Host ("checked {0} format call(s) across {1} file(s); {2} skipped (non-literal format string)" -f $checked, $Files.Count, $skipped)
if ($bad -gt 0) { Write-Host ("RESULT: FAIL - {0} mismatch(es)" -f $bad) -ForegroundColor Red; exit 1 }
Write-Host "RESULT: PASS - every literal format call has enough arguments" -ForegroundColor Green
exit 0
