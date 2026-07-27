# verify_addr.ps1 - Read-only re-check of specific flash addresses after a 'diff' report.
#
# WHY THIS EXISTS (SSOT section D, second class of false failure):
#   openocd's verify_image falls back to a host-side byte compare when the on-target CRC
#   helper times out. That fallback has been observed reading STALE (pre-erase) data, so it
#   reports 'diff N address 0x...' while the flash content is actually correct.
#   Re-flashing on that report costs ~115s and adds one more brick opportunity for nothing.
#
# WHAT IT DOES: opens an INDEPENDENT read-only openocd session, reads the words that were
#   reported as differing, and compares them against the real bytes in the binary image.
#   No erase, no write, no halt of a running app beyond openocd's own reset-on-init.
#
# NOTE: every openocd 'init' resets the chip (SSOT section D) -> the app restarts once.
#
# Usage:  powershell -File verify_addr.ps1 -Addresses 0x4,0x7510
# ASCII-only by repo rule.
# TRAP: do NOT name the openocd argument array $args - that is a reserved automatic variable
#       in PowerShell and splatting it (@args) silently passes the wrong thing.

param(
    [string[]] $Addresses = @('0x4', '0x7510'),
    [int]      $Words     = 2,
    [string]   $Elf       = 'gcc\car.out'
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $here
. (Join-Path $here '_tools.ps1')

# NOTE: Find-Openocd returns @{Exe;Scripts} - a hashtable, not a path (API at top of _tools.ps1).
$openocd = (Find-Openocd).Exe
$scripts = Find-OpenocdScripts
$objcopy = Find-ArmTool 'arm-none-eabi-objcopy'

if (-not (Test-Path $Elf)) { throw "ELF not found: $Elf" }

# --- expected bytes: flatten the ELF to a raw flash image (load address 0 = flash base) ---
$bin = Join-Path $env:TEMP 'car_verify_addr.bin'
& $objcopy -O binary $Elf $bin
if ($LASTEXITCODE -ne 0) { throw 'objcopy failed' }
$img = [System.IO.File]::ReadAllBytes($bin)
Write-Output "image: $Elf -> $($img.Length) bytes"

# --- read back the same addresses from the chip, read-only ---
# TRAP: with 'powershell -File', "-Addresses 0x0,0x7510" arrives as ONE token, and PowerShell
# has already rendered the hex literals as decimal ("0,29968"). Passing that straight to
# openocd gives "address option value ('0,29968') is not valid". So normalize here: split on
# commas, accept both 0x.. and decimal, and re-emit canonical hex.
$addrList = @()
foreach ($a in $Addresses) {
    foreach ($piece in ("$a" -split ',')) {
        $p = $piece.Trim()
        if (-not $p) { continue }
        if ($p -match '^0[xX]([0-9a-fA-F]+)$') { $addrList += [Convert]::ToInt64($Matches[1], 16) }
        else                                   { $addrList += [int64]$p }
    }
}
if ($addrList.Count -eq 0) { throw 'no usable address given' }
Write-Output ("addresses: " + (($addrList | ForEach-Object { '0x{0:x}' -f $_ }) -join ' '))

$ooArgs = @('-s', $scripts, '-f', 'interface/cmsis-dap.cfg', '-f', 'target/ti_mspm0.cfg',
          '-c', 'adapter speed 500', '-c', 'init')
foreach ($a in $addrList) { $ooArgs += @('-c', ('mdw 0x{0:x} {1}' -f $a, $Words)) }
$ooArgs += @('-c', 'exit')

# TRAP: openocd writes EVERYTHING (banner, mdw output, errors) to stderr. With
# $ErrorActionPreference='Stop', PowerShell turns that into a terminating error and the
# script dies even though openocd succeeded. So: redirect all streams to a file, read it back.
$log = Join-Path $env:TEMP 'car_verify_addr.log'
$prevEA = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& $openocd @ooArgs *> $log
$ErrorActionPreference = $prevEA
$raw = @(Get-Content -LiteralPath $log -ErrorAction SilentlyContinue)
$raw | ForEach-Object { Write-Output "  openocd: $_" }

# openocd prints:  0x00000004: aabbccdd 11223344
$read = @{}
foreach ($line in $raw) {
    if ($line -match '^\s*0x([0-9a-fA-F]{8}):\s*((?:[0-9a-fA-F]{8}\s*)+)$') {
        $base = [Convert]::ToInt64($Matches[1], 16)
        $i = 0
        foreach ($w in ($Matches[2].Trim() -split '\s+')) {
            $read[$base + 4 * $i] = [Convert]::ToUInt32($w, 16)
            $i++
        }
    }
}
if ($read.Count -eq 0) { Write-Output 'RESULT: INCONCLUSIVE - could not parse any mdw output'; exit 2 }

$bad = 0
Write-Output ''
Write-Output 'addr        on-chip    in-image   verdict'
foreach ($addr in ($read.Keys | Sort-Object)) {
    if ($addr + 3 -ge $img.Length) { continue }
    $want = [uint32]$img[$addr] -bor ([uint32]$img[$addr + 1] -shl 8) -bor `
            ([uint32]$img[$addr + 2] -shl 16) -bor ([uint32]$img[$addr + 3] -shl 24)
    $got = $read[$addr]
    $ok = ($got -eq $want)
    if (-not $ok) { $bad++ }
    Write-Output ('0x{0:x8}  0x{1:x8}  0x{2:x8}  {3}' -f $addr, $got, $want, $(if ($ok) { 'MATCH' } else { 'MISMATCH' }))
}

Write-Output ''
if ($bad -eq 0) {
    Write-Output 'RESULT: PASS - the reported diffs were STALE READS; flash content is correct.'
    Write-Output '  => Do NOT re-flash. Confirm with a functional fingerprint over serial.'
    exit 0
}
Write-Output "RESULT: FAIL - $bad word(s) really differ on-chip; the image is genuinely wrong."
exit 1
