# _refresh.ps1 -- external reference repo mirror: clone / update
#
# WHY ASCII-ONLY: Windows PowerShell 5.1 reads a BOM-less UTF-8 script as GBK,
#   which mangles Chinese string literals and breaks the parser (already burned us once).
#   All Chinese notes live in 00_INDEX.md and the per-bucket README.md files. Keep this file ASCII.
#
# WHAT: shallow-clone every repo indexed in 00_INDEX.md so they are available OFFLINE
#   at the contest site (no network there).
#
# LAYOUT (organised by PURPOSE, not by my evaluation tier -- tiers go stale, purpose does not):
#   car/            active: the car problem we are working on now
#   topics/         contest problem originals + past-year code (useful whatever the problem is)
#   agent/          agent-skill peers (read how they organise rules, not their code)
#   other/maglev    collection: magnetic levitation / balancing (校赛B kin, black-swan ready)
#   other/vision    collection: vision + gimbal / dual-MCU
#   other/dsp       collection: instrument / signal processing
#
# PATH BUDGET (hard constraint, do not ignore): Windows MAX_PATH is 260 chars.
#   car/periph_ex holds a 6551-file TI-style tree whose deepest file sits at 254 chars.
#   That is why bucket names are short (car/topics/agent/other) and that repo has a short name.
#   Before renaming anything here, re-measure:
#     Get-ChildItem . -Recurse -File -Force | % { $_.FullName.Length } | Measure-Object -Maximum
#
# GIT: only 00_INDEX.md, this script and the per-bucket README.md files are committed.
#   Most listed repos have NO LICENSE file -> we may keep a local copy to read ideas,
#   but must not redistribute them from our public repo. Check 00_INDEX.md before copying code.
#
# USAGE:
#   .\_refresh.ps1                 # clone missing, fetch-update existing
#   .\_refresh.ps1 -SkipExisting   # only clone what is missing (use after partial failure)
#   .\_refresh.ps1 -Only car/      # only paths starting with car/
#   .\_refresh.ps1 -Force          # delete and re-clone
#
# NOTE on failures: first full run on 2026-07-27 got 19 ok / 11 FAILED, and the failed
#   URLs were verified reachable with `git ls-remote` right after -> the failures were
#   transient network resets (this machine already hits `Connection was reset` on GitHub,
#   see CONTINUATION_GUIDE). Hence -Retries below. Do NOT conclude "repo gone" from one
#   failed clone; check `git ls-remote --heads <url>` first.

param(
    [string]$Only = "",
    [switch]$Force,
    [switch]$SkipExisting,
    [int]$Retries = 3
)

$ErrorActionPreference = "Continue"
$root = $PSScriptRoot

# Abort a stalled transfer instead of hanging forever. Without this, one bad repo
# (2026-07-27: zuoliangyu/NUEDC_TOPIC) hangs the whole queue and blocks every repo after it.
# Semantics: if throughput stays below LOW_SPEED_LIMIT bytes/s for LOW_SPEED_TIME seconds, git errors out.
$env:GIT_HTTP_LOW_SPEED_LIMIT = "1000"
$env:GIT_HTTP_LOW_SPEED_TIME  = "30"

# path = location under this folder (bucket/name) / url = upstream
$repos = @(
    # ================= car/  : active, current car problem =================
    @{ path = "car/2024H_abcuer";           url = "https://github.com/abcuer/2024-NUEDC-H-TI_CAR" }
    @{ path = "car/2024H_zlc_periph";       url = "https://github.com/ZhijianLi2003/ZLC_MSPM0_Peripheral_Library" }
    @{ path = "car/kit_k230_vision";        url = "https://github.com/2262727886-stack/mspm0g3507-car-kit" }
    @{ path = "car/car26_k1l0m";            url = "https://github.com/K1L0M/MSPM0G3507-26-" }
    @{ path = "car/gray_pid_5ee511";        url = "https://github.com/5ee511/MSPM0G3507-Car" }
    @{ path = "car/gyro_damp_thorn";        url = "https://github.com/Thorn-ym/TI_Car" }
    @{ path = "car/imu_yaw_mpu6050";        url = "https://github.com/monokumakuki/2026TI_MPU6050_Yaw" }
    @{ path = "car/periph_modules";         url = "https://github.com/Torris-Yin/mspm0-modules" }
    @{ path = "car/periph_driverlib";       url = "https://github.com/dzzz-qcxf-studio/MSPM0_Driver_Lib" }
    @{ path = "car/periph_ex";              url = "https://github.com/ZhiKong0/MSPM03507-module-examples" }
    @{ path = "car/chassis_4wd";            url = "https://github.com/xy1092/mspm0g3507-4wd-template" }

    # ================= topics/ : problem originals + past-year code =================
    @{ path = "topics/nuedc_1994_2026";     url = "https://github.com/CCBP/NUEDC_Topic" }
    @{ path = "topics/nuedc_mirror";        url = "https://github.com/zuoliangyu/NUEDC_TOPIC" }
    @{ path = "topics/control_code_21_25";  url = "https://github.com/menoking/NUEDC-ControlTopic-Code" }

    # ================= agent/ : agent-skill peers =================
    @{ path = "agent/skill_3507_dvoid128";  url = "https://github.com/Dvoid128/3507-skill" }
    @{ path = "agent/skill_mspm0_ibook";    url = "https://github.com/Ibook000/mspm0-skill" }
    @{ path = "agent/skill_mspm0_lmysang";  url = "https://github.com/Lmy-sang/MSPM0G3507-SKILL" }

    # ================= other/maglev : levitation / balancing collection =================
    @{ path = "other/maglev/ball_observer"; url = "https://github.com/shiivashaakeri/Control-Magnetic-Levitation-Ball" }
    @{ path = "other/maglev/ti_tm4c";       url = "https://github.com/ussserrr/maglev" }
    @{ path = "other/maglev/hardware_pcb";  url = "https://github.com/ussserrr/maglev-hardware" }
    @{ path = "other/maglev/stm32_thesis";  url = "https://github.com/dvdvideo1234/SystemMaglevThesis" }
    @{ path = "other/maglev/avr_hall";      url = "https://github.com/flannelhead/avr-maglev" }
    @{ path = "other/maglev/review_article";url = "https://github.com/felipecacique/MagneticLevitation" }
    @{ path = "other/maglev/arduino_pid";   url = "https://github.com/fsantagostinobietti/mymaglev" }
    @{ path = "other/maglev/labkit";        url = "https://github.com/Hansolini/Take-home-Maglev-lab" }
    @{ path = "other/maglev/fb_design";     url = "https://github.com/hsm-0510/magnetic_levitation_feedback_control_system_design" }

    # ================= other/vision : vision + gimbal / dual-MCU =================
    @{ path = "other/vision/2025E_dualmcu"; url = "https://github.com/wengqidaifeng/2025-e-smartcar" }
    @{ path = "other/vision/2023E_movetrack"; url = "https://github.com/MenHimChan/MoveTrackSys_TIcup_2023" }

    # ================= other/dsp : instrument / signal processing =================
    @{ path = "other/dsp/signal_analyzer";  url = "https://github.com/IllusionMZX/NEU-EEContest2025-SignalDev" }
    @{ path = "other/dsp/ad9851_dds";       url = "https://github.com/WitBlue6/AD9851-AD9850-Driver" }
)

if ($Only -ne "") { $repos = $repos | Where-Object { $_.path -like "$Only*" } }

$results = @()
$i = 0
foreach ($r in $repos) {
    $i++
    $dst = Join-Path $root ($r.path -replace '/', '\')
    $parent = Split-Path $dst -Parent
    if (-not (Test-Path $parent)) { New-Item -ItemType Directory -Path $parent -Force | Out-Null }
    Write-Host ("[{0}/{1}] {2}" -f $i, $repos.Count, $r.path) -ForegroundColor Cyan

    if ($Force -and (Test-Path $dst)) { Remove-Item -Recurse -Force $dst }

    $err = ""
    if (Test-Path (Join-Path $dst ".git")) {
        if ($SkipExisting) {
            Write-Host "    skip (already present)" -ForegroundColor DarkGray
            $action = "present"
        } else {
            git -C $dst fetch --depth 1 origin 2>&1 | Out-Null
            git -C $dst reset --hard FETCH_HEAD 2>&1 | Out-Null
            $action = "updated"
        }
    } else {
        $action = "FAILED"
        for ($a = 1; $a -le $Retries; $a++) {
            if (Test-Path $dst) { Remove-Item -Recurse -Force $dst -ErrorAction SilentlyContinue }
            $out = (git clone --depth 1 $r.url $dst 2>&1 | Out-String)
            if ($LASTEXITCODE -eq 0 -and (Test-Path (Join-Path $dst ".git"))) {
                $action = "cloned"; break
            }
            $err = ($out -replace "`r?`n", " / ").Trim()
            Write-Host ("    attempt {0}/{1} failed: {2}" -f $a, $Retries,
                $err.Substring(0, [Math]::Min(120, $err.Length))) -ForegroundColor DarkYellow
            Start-Sleep -Seconds (3 * $a)
        }
    }

    $sha = ""; $date = ""; $lic = "none"; $mb = 0; $files = 0
    if (Test-Path (Join-Path $dst ".git")) {
        $sha  = (git -C $dst rev-parse --short HEAD 2>$null)
        $date = (git -C $dst log -1 --format=%cs 2>$null)
        $licf = Get-ChildItem $dst -File -Force -ErrorAction SilentlyContinue |
                Where-Object { $_.Name -match '^(LICENSE|COPYING|LICENCE)' } | Select-Object -First 1
        if ($licf) {
            $head = ((Get-Content $licf.FullName -TotalCount 25 -ErrorAction SilentlyContinue) -join ' ')
            if     ($head -match 'MIT License')                { $lic = 'MIT' }
            elseif ($head -match 'GNU GENERAL PUBLIC LICENSE') { $lic = if ($head -match 'Version 3') { 'GPL-3.0' } else { 'GPL-2.0' } }
            elseif ($head -match 'Apache License')             { $lic = 'Apache-2.0' }
            elseif ($head -match 'BSD')                        { $lic = 'BSD' }
            elseif ($head -match 'Mozilla')                    { $lic = 'MPL' }
            else                                               { $lic = 'other' }
        }
        $all = Get-ChildItem $dst -Recurse -File -Force -ErrorAction SilentlyContinue |
               Where-Object { $_.FullName -notlike "*\.git\*" }
        $files = ($all | Measure-Object).Count
        $mb = [math]::Round((($all | Measure-Object -Property Length -Sum).Sum / 1MB), 2)
        if ($action -eq "FAILED") { $action = "cloned" }
    } else { $action = "FAILED" }

    $results += [pscustomobject]@{
        path = $r.path; action = $action; sha = $sha; commit_date = $date
        license = $lic; files = $files; size_MB = $mb; url = $r.url; error = $err
    }
    Write-Host ("    {0}  {1}  {2}  lic={3}  {4} files  {5}MB" -f `
        $action, $sha, $date, $lic, $files, $mb)
}

# Result file guards (two separate holes, both hit for real):
#   1) a filtered run that matched NOTHING must not blank out a good result file
#      (2026-07-27: `-Only zzz_none` used as a syntax self-check wiped all 30 rows);
#   2) a filtered run that matched SOME must not overwrite the full-set result either
#      -> partial runs get their own file, so _refresh_result.csv always means "all 30".
if ($results.Count -eq 0) {
    Write-Host "no repo matched -Only '$Only' -> result csv left untouched" -ForegroundColor Yellow
} elseif ($Only -ne "") {
    $csv = Join-Path $root "_refresh_result_partial.csv"
    $results | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
    Write-Host "filtered run -> _refresh_result_partial.csv (full-set csv untouched)" -ForegroundColor DarkGray
} else {
    $csv = Join-Path $root "_refresh_result.csv"
    $results | Export-Csv -Path $csv -NoTypeInformation -Encoding UTF8
}
Write-Host ""
Write-Host ("DONE  ok={0}  failed={1}  total={2}MB" -f `
    ($results | Where-Object { $_.action -ne "FAILED" }).Count, `
    ($results | Where-Object { $_.action -eq "FAILED" }).Count, `
    [math]::Round((($results | Measure-Object -Property size_MB -Sum).Sum), 1)) -ForegroundColor Green
$results | Where-Object { $_.action -eq "FAILED" } | ForEach-Object { Write-Host ("  FAILED: " + $_.path) -ForegroundColor Red }
if ($results.Count -ne 30 -and $Only -eq "") {
    Write-Host ("WARN: expected 30 repos in the table, saw {0} -- did an entry get dropped?" -f $results.Count) -ForegroundColor Yellow
}

# Path budget guard -- warn loudly if a future rename pushes us toward MAX_PATH.
$maxLen = (Get-ChildItem $root -Recurse -File -Force -ErrorAction SilentlyContinue |
           ForEach-Object { $_.FullName.Length } | Measure-Object -Maximum).Maximum
$flag = if ($maxLen -ge 250) { "WARN" } else { "ok" }
Write-Host ("max full path = {0} chars (MAX_PATH 260) [{1}]" -f $maxLen, $flag) `
    -ForegroundColor $(if ($maxLen -ge 250) { "Yellow" } else { "DarkGray" })
# Report the file actually written -- never a hardcoded name (a filtered run writes a
# different file, and printing the wrong one is exactly the kind of reassuring-but-false
# closing line we banned in the pitfall log).
if ($results.Count -eq 0) { Write-Host "result -> (none written)" }
else { Write-Host ("result -> " + (Split-Path $csv -Leaf)) }
