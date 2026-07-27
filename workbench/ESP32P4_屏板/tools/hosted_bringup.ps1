# ASCII only (PowerShell 5.1 encoding pitfall).
#
# One shot: flash an ESP-Hosted host app to the ESP32-P4 over COM7 (USB-Serial-JTAG),
# capture the boot log, and judge the C5 link with a PASS/FAIL/INCONCLUSIVE verdict.
#
#   .\hosted_bringup.ps1                     # p4_scan_host, 25 s of log
#   .\hosted_bringup.ps1 -Project p4_sta_host
#   .\hosted_bringup.ps1 -NoFlash            # just re-read the log from the running app
#
# What the verdict is based on (strings emitted by esp_hosted 3.0.5's host stack):
#   "slave chip id:"        -> transport up AND init-event handshake done  (the real gate)
#   "capabilities:"         -> feature negotiation done
#   "Total APs scanned ="   -> Wi-Fi actually ran on the co-processor
#   "transport[host]: SDIO" -> host-side config echo; printed even when the slave is dead,
#                              so it proves nothing about the link. Used only to confirm
#                              the pins/width the firmware really compiled with.
# Per the official debugging order (docs/getting-started-mcu.md): if the init event never
# arrives, it is a wiring/transport-config problem -- do NOT start debugging Wi-Fi.
#
# NOTE this overwrites the P4's application (the p4_lcd LVGL panel demo).
# Restore it with:  .\build_p4.ps1 -Flash
#
# VERIFICATION STATUS (2026-07-27): exercised end to end on real hardware --
#   p4_scan_host -> PASS (Total APs scanned = 51)
#   p4_sta_host  -> PASS (PING SUMMARY tx=5 rx=5 loss=0%), incl. the static-fallback note
#   no board attached -> INCONCLUSIVE guard
# Untested branches left: the PARTIAL paths (chip-id-but-no-scan / ip-but-no-ping)
# and the "no transport line" INCONCLUSIVE path.

param(
    [ValidateSet("p4_scan_host", "p4_sta_host")]
    [string]$Project = "p4_scan_host",
    [string]$Root    = "d:\diansai\workbench\esp32p4",
    [string]$Port    = "COM7",
    [int]$Seconds    = 25,
    [switch]$NoFlash
)

$ErrorActionPreference = "Continue"
$log = "d:\diansai\.tmp_pdf\esp32p4\hosted_bringup_$Project.txt"

# Fail loudly instead of "succeeding" against a board that is not plugged in.
$ports = [System.IO.Ports.SerialPort]::GetPortNames()
Write-Output ("PORTS_SEEN: " + (($ports -join ",") -replace '^$', '<none>'))
if ($ports -notcontains $Port) {
    Write-Output "RESULT: INCONCLUSIVE"
    Write-Output "REASON: $Port not present -- plug the board into the Type-C silkscreened 'UART' and power it on."
    exit 3
}

if (-not $NoFlash) {
    . "$PSScriptRoot\idf_shell.ps1"
    if (-not (Get-Command idf.py -ErrorAction SilentlyContinue)) {
        Write-Output "RESULT: INCONCLUSIVE"
        Write-Output "REASON: idf.py not on PATH (IDF env not activated)"
        exit 90
    }
    Set-Location (Join-Path $Root $Project)
    idf.py -p $Port flash
    $code = $LASTEXITCODE
    Write-Output "FLASH_EXIT=$code"
    if ($code -ne 0) {
        Write-Output "RESULT: FAIL"
        Write-Output "REASON: flashing failed"
        exit $code
    }
    Start-Sleep -Seconds 1
}

& "$PSScriptRoot\p4_boot_read.ps1" -Port $Port -Seconds $Seconds -Out $log
if (-not (Test-Path $log)) {
    Write-Output "RESULT: INCONCLUSIVE"
    Write-Output "REASON: no log captured"
    exit 3
}

$text = Get-Content $log -Raw -Encoding UTF8
Write-Output ""
Write-Output "---------------- log excerpt (hosted / wifi lines) ----------------"
Get-Content $log -Encoding UTF8 |
    Select-String -Pattern 'transport\[host\]|slave chip id|capabilities|WLAN over|Features supported|RPC|rpc|Total APs|SSID|RSSI|Channel|E \(|W \(|Timeout|timeout|reset' |
    Select-Object -First 60 | ForEach-Object { "  " + $_.Line.Trim() }
Write-Output "-------------------------------------------------------------------"

$hasTransport = $text -match 'transport\[host\]: SDIO'
$hasChipId    = $text -match 'slave chip id:'
$hasCaps      = $text -match 'capabilities: 0x'
$scanned      = [regex]::Match($text, 'Total APs scanned = (\d+)')
$gotIp        = [regex]::Match($text, 'got ip:([0-9.]+)')
$pingSum      = [regex]::Match($text, 'PING SUMMARY[^\r\n]*')

Write-Output ""
Write-Output ("MARKER transport[host]: SDIO   = " + $hasTransport)
Write-Output ("MARKER slave chip id          = " + $hasChipId)
Write-Output ("MARKER capabilities           = " + $hasCaps)
Write-Output ("MARKER Total APs scanned      = " + $(if ($scanned.Success) { $scanned.Groups[1].Value } else { "n/a" }))
Write-Output ("MARKER got ip                 = " + $(if ($gotIp.Success) { $gotIp.Groups[1].Value } else { "n/a" }))
Write-Output ("MARKER ping                   = " + $(if ($pingSum.Success) { $pingSum.Value.Trim() } else { "n/a" }))
Write-Output ("LOG   = " + $log + "  (" + $text.Length + " bytes)")
Write-Output ""

# p4_sta_host: the verdict is the ping (acceptance criterion "P4 can ping")
if ($pingSum.Success -and $pingSum.Value -match 'PING_OK') {
    Write-Output "RESULT: PASS  -- associated and ICMP replies came back over the C5 radio."
    if ($text -match 'falling back to static') {
        Write-Output "NOTE: the address came from the STATIC fallback, not DHCP -- the AP handed out no lease."
        Write-Output "      ICMP working on a static address still proves the P4->SDIO->C5->air path both ways."
    }
    exit 0
}
# p4_scan_host: the verdict is a non-empty AP list
if ($scanned.Success -and [int]$scanned.Groups[1].Value -gt 0) {
    Write-Output "RESULT: PASS  -- C5 radio is alive: SDIO link up and the co-processor returned real scan results."
    exit 0
}
if ($gotIp.Success) {
    Write-Output "RESULT: PARTIAL -- got a DHCP lease (so the air+SDIO path works both ways) but ICMP did not come back."
    Write-Output "NEXT: is the gateway answering pings at all? try another AP, or lengthen -Seconds."
    exit 1
}
if ($hasChipId) {
    Write-Output "RESULT: PARTIAL -- SDIO link + handshake OK (slave chip id seen) but no AP list yet."
    Write-Output "NEXT: longer -Seconds, or check that an AP is in range."
    exit 1
}
if ($hasTransport) {
    Write-Output "RESULT: FAIL -- host transport started but the co-processor never sent its init event."
    Write-Output "NEXT: that means wiring or transport config, NOT Wi-Fi. Most likely the C5 is not running"
    Write-Output "      ESP-Hosted co-processor firmware yet -> flash workbench\esp32p4\c5_cp via a USB-TTL"
    Write-Output "      adapter on the 6P header (3V3/GND/C5_TXD0/C5_RXD0/C5_EN/C5_BOOT)."
    exit 1
}
Write-Output "RESULT: INCONCLUSIVE -- no esp_hosted transport line in the log; is this really the host app?"
exit 3
