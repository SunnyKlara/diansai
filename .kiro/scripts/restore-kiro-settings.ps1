<#
  restore-kiro-settings.ps1  --  Kiro user-level environment restore (idempotent)

  WHY THIS SCRIPT EXISTS
    Kiro blocks the AI agent from writing ~/.kiro/settings/** (permissions.yaml,
    mcp.json) on purpose: an agent must not edit its own trust config.
    So those two layers have to be written by YOU running this script.
    Running it = your explicit authorization.

  WHAT IT DOES (all idempotent, keeps existing content)
    1. merge 8 keys into %APPDATA%\Kiro\User\settings.json
    2. write ~/.kiro/settings/permissions.yaml  (allow-all + ask on dangerous shell)
    3. merge  ~/.kiro/settings/mcp.json         (markitdown / playwright / context7)
    4. install user extensions via Kiro CLI
    5. report ~/.kiro/argv.json locale (does NOT rewrite the JSONC file)

  USAGE
    powershell -ExecutionPolicy Bypass -File .kiro/scripts/restore-kiro-settings.ps1
    optional:  -SkipExtensions   -SkipPermissions   -Force (overwrite permissions.yaml)

  NOTE: all console output is ASCII on purpose -- PowerShell 5.1 mangles UTF-8
        Chinese output under a GBK console (see knowledge/跨题坑库.md).
#>

[CmdletBinding()]
param(
    [switch]$SkipExtensions,
    [switch]$SkipPermissions,
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Write-Step { param($m) Write-Host "`n=== $m ===" -ForegroundColor Cyan }
function Write-Ok   { param($m) Write-Host "  [OK]   $m" -ForegroundColor Green }
function Write-Add  { param($m) Write-Host "  [ADD]  $m" -ForegroundColor Yellow }
function Write-Skip { param($m) Write-Host "  [SKIP] $m" -ForegroundColor DarkGray }
function Write-Warn2{ param($m) Write-Host "  [WARN] $m" -ForegroundColor Magenta }

# UTF-8 without BOM -- YAML/JSON consumers choke on BOM
function Save-Utf8NoBom {
    param([string]$Path, [string]$Text)
    $dir = Split-Path -Parent $Path
    if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
    [System.IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding($false)))
}

# ---------------------------------------------------------------- paths
$isWin = $true
$userSettings = Join-Path $env:APPDATA 'Kiro\User\settings.json'
$kiroHome     = Join-Path $env:USERPROFILE '.kiro'
$permYamlPath = Join-Path $kiroHome 'settings\permissions.yaml'
$userMcpPath  = Join-Path $kiroHome 'settings\mcp.json'
$argvPath     = Join-Path $kiroHome 'argv.json'

# ---------------------------------------------------------------- 1. settings.json
# SOURCE OF TRUTH for these keys. Keep in sync with:
#   .kiro/steering/ (if documented there) and the "换机提示词" doc.
$desired = [ordered]@{
    'window.autoDetectColorScheme'        = $true
    'workbench.preferredLightColorTheme'  = 'Kiro Dark'
    'workbench.editor.empty.hint'         = 'hidden'
    'kiroAgent.modelSelection'            = 'claude-opus-4.8'   # fallback key only; real model = UI picker
    'kiroAgent.agentAutonomy'             = 'Autopilot'
    'kiroAgent.trustedCommands'           = @('*')              # legacy (pre Trust v2) -- kept for old builds
    'kiroAgent.trustedTools'              = @('web_fetch', 'remote_web_search')  # legacy
    'files.encoding'                      = 'utf8'              # this repo is Chinese/UTF-8 throughout
}

Write-Step '1/5  settings.json'
$existing = [ordered]@{}
if (Test-Path $userSettings) {
    $raw = [System.IO.File]::ReadAllText($userSettings)
    # strip // and /* */ comments so JSONC-ish settings still parse
    $stripped = [regex]::Replace($raw, '(?m)^\s*//.*$', '')
    $stripped = [regex]::Replace($stripped, '/\*.*?\*/', '', 'Singleline')
    if ($stripped.Trim()) {
        try {
            ($stripped | ConvertFrom-Json).PSObject.Properties | ForEach-Object {
                $existing[$_.Name] = $_.Value
            }
        } catch {
            Write-Warn2 "existing settings.json not parseable, backing up and starting fresh"
            Copy-Item $userSettings "$userSettings.bak-$(Get-Date -f yyyyMMddHHmmss)"
        }
    }
    Write-Ok "found existing settings.json ($($existing.Keys.Count) keys)"
} else {
    Write-Add 'settings.json does not exist, creating'
}

$added = @()
foreach ($k in $desired.Keys) {
    if (-not $existing.Contains($k)) { $existing[$k] = $desired[$k]; $added += $k }
}
Save-Utf8NoBom $userSettings (($existing | ConvertTo-Json -Depth 10))
if ($added.Count) { $added | ForEach-Object { Write-Add "key $_" } } else { Write-Skip 'all 8 keys already present' }
Write-Ok $userSettings

# ---------------------------------------------------------------- 2. permissions.yaml
# Model: allow everything, then ASK on dangerous shell (ask wins over allow).
$permYaml = @'
# Kiro Trust v2 permissions (user scope)
# Model: allow-all capabilities + ask-before-run on destructive shell commands.
# `ask` rules take precedence over `allow`.
rules:
  - { capability: fs_read,     effect: allow, match: ["*"] }
  - { capability: fs_write,    effect: allow, match: ["*"] }
  - { capability: web_fetch,   effect: allow, match: ["*"] }
  - { capability: web_search,  effect: allow, match: ["*"] }
  - { capability: mcp,         effect: allow, match: ["*"] }
  - { capability: context,     effect: allow, match: ["*"] }
  - { capability: diagnostics, effect: allow, match: ["*"] }
  - { capability: subagent,    effect: allow, match: ["*"] }
  - { capability: skill,       effect: allow, match: ["*"] }
  - { capability: power,       effect: allow, match: ["*"] }
  - { capability: shell,       effect: allow, match: ["*"] }
  - capability: shell
    effect: ask
    match:
      - "rm *"
      - "*rm -rf*"
      - "*rm -fr*"
      - "del *"
      - "erase *"
      - "rmdir *"
      - "rd /s*"
      - "*Remove-Item*"
      - "*Clear-Content*"
      - "*--force*"
      - "git push -f*"
      - "*reset --hard*"
      - "git clean*"
      - "git branch -D*"
      - "git checkout -- *"
      - "git checkout .*"
      - "git checkout -f*"
      - "git restore *"
      - "git stash clear*"
      - "git stash drop*"
      - "sudo *"
      - "*shutdown*"
      - "*reboot*"
      - "*chmod 777*"
      - "*format *"
      - "*mkfs*"
      - "*dd if=*"
      - "*diskpart*"
      - "* > /dev/*"
      - "*Invoke-Expression*"
      - "*| iex*"
      - "*:(){*"
'@

Write-Step '2/5  permissions.yaml (Trust v2)'
if ($SkipPermissions) {
    Write-Skip 'skipped by -SkipPermissions'
} elseif ((Test-Path $permYamlPath) -and -not $Force) {
    $cur = [System.IO.File]::ReadAllText($permYamlPath)
    if ($cur -match '(?m)^\s*rules\s*:' -and $cur -match 'capability') {
        Write-Skip "already has non-empty rules (use -Force to overwrite): $permYamlPath"
    } else {
        Copy-Item $permYamlPath "$permYamlPath.bak-$(Get-Date -f yyyyMMddHHmmss)"
        Save-Utf8NoBom $permYamlPath $permYaml
        Write-Add "rewrote empty/invalid permissions.yaml (old file backed up)"
    }
} else {
    if (Test-Path $permYamlPath) { Copy-Item $permYamlPath "$permYamlPath.bak-$(Get-Date -f yyyyMMddHHmmss)" }
    Save-Utf8NoBom $permYamlPath $permYaml
    Write-Add "wrote $permYamlPath"
}

# ---------------------------------------------------------------- 3. user-level mcp.json
# markitdown -> needs uvx (PDF/Office/image -> Markdown; useful for 赛题 PDFs)
# playwright  -> needs npx (browser automation / web verification)
# context7    -> disabled on purpose until you paste your own API key
$wantedMcp = [ordered]@{
    markitdown = [ordered]@{
        command     = 'uvx'
        args        = @('markitdown-mcp')
        env         = [ordered]@{ FASTMCP_LOG_LEVEL = 'ERROR' }
        disabled    = $false
        autoApprove = @('convert_to_markdown')
    }
    playwright = [ordered]@{
        command  = 'npx'
        args     = @('-y', '@playwright/mcp@latest')
        disabled = $false
    }
    context7 = [ordered]@{
        command     = 'npx'
        args        = @('-y', '@upstash/context7-mcp')
        env         = [ordered]@{ CONTEXT7_API_KEY = 'PASTE_YOUR_FREE_KEY_HERE' }
        disabled    = $true
        autoApprove = @('resolve-library-id', 'get-library-docs')
    }
}

Write-Step '3/5  user-level mcp.json'
$mcpRoot = [ordered]@{ mcpServers = [ordered]@{} }
if (Test-Path $userMcpPath) {
    try {
        $parsed = [System.IO.File]::ReadAllText($userMcpPath) | ConvertFrom-Json
        if ($parsed.mcpServers) {
            $parsed.mcpServers.PSObject.Properties | ForEach-Object {
                $mcpRoot.mcpServers[$_.Name] = $_.Value
            }
        }
        Write-Ok "found existing mcp.json ($($mcpRoot.mcpServers.Keys.Count) servers)"
    } catch {
        Write-Warn2 'existing mcp.json not parseable, backing up'
        Copy-Item $userMcpPath "$userMcpPath.bak-$(Get-Date -f yyyyMMddHHmmss)"
    }
}
foreach ($name in $wantedMcp.Keys) {
    if ($mcpRoot.mcpServers.Contains($name)) {
        Write-Skip "server '$name' already configured, left untouched"
    } else {
        $mcpRoot.mcpServers[$name] = $wantedMcp[$name]
        Write-Add "server '$name'"
    }
}
Save-Utf8NoBom $userMcpPath (($mcpRoot | ConvertTo-Json -Depth 10))
Write-Ok $userMcpPath

# ---------------------------------------------------------------- 4. argv.json locale
Write-Step '4/5  argv.json locale (report only)'
if (Test-Path $argvPath) {
    $argvRaw = [System.IO.File]::ReadAllText($argvPath)
    if ($argvRaw -match '"locale"\s*:\s*"zh-cn"') {
        Write-Ok 'locale = zh-cn (restart Kiro fully if you just changed it)'
    } else {
        Write-Warn2 "no zh-cn locale in $argvPath"
        Write-Host '         fix: add   "locale": "zh-cn",   inside the JSON object'
        Write-Host '         or:  Command Palette -> Configure Display Language'
    }
} else {
    Write-Warn2 "$argvPath not found (Kiro creates it on first run)"
}

# ---------------------------------------------------------------- deps
Write-Step 'prerequisites'
foreach ($d in 'git', 'node', 'npx', 'uv', 'uvx') {
    if (Get-Command $d -ErrorAction SilentlyContinue) { Write-Ok $d } else { Write-Warn2 "$d MISSING" }
}
Write-Host '  uv/uvx missing => markitdown MCP is dead config (playwright still fine).'
Write-Host '  install: winget install astral-sh.uv     (or) pip install uv'

Write-Host "`nDONE (config layers). Run 'Developer: Reload Window'. If argv.json changed, restart Kiro completely." -ForegroundColor Cyan

# ---------------------------------------------------------------- 5. extensions  (LAST ON PURPOSE)
# WHY LAST: calling kiro.cmd TERMINATES this host PowerShell process on return --
# a foreach loop only runs its first iteration and ALL later output (even literal
# strings) vanishes while exit code stays 0. Verified 2026-07-27, see
# .kiro/steering/knowledge/跨题坑库.md "PowerShell 5.1 ... 外部 CLI 杀掉宿主 shell".
# So everything important above has already printed by now; if output ends abruptly
# in this section, that is EXPECTED, not a failure -- verify by listing the ext dir.
#
# C/embedded toolchain (STM32H750 / MSPM0G3507) + zh-CN UI + PDF/Office reading.
# NOT included: espressif.esp-idf-extension -- ESP32 only, unused in this repo.
#   want it anyway?  kiro --install-extension espressif.esp-idf-extension
$wantedExtensions = @(
    'llvm-vs-code-extensions.vscode-clangd',
    'ms-vscode.cmake-tools',
    'franneck94.c-cpp-runner',
    'vadimcn.vscode-lldb',
    'ms-vscode.cpptools-themes',
    'ms-ceintl.vscode-language-pack-zh-hans',
    'cweijan.vscode-office',
    'tomoki1207.pdf',
    'zhuangtongfa.material-theme'
)

Write-Step '5/5  extensions'
$extDir = Join-Path $kiroHome 'extensions'
if ($SkipExtensions) {
    Write-Skip 'skipped by -SkipExtensions'
} else {
    # report current state FIRST -- output after the kiro call may never appear
    if (Test-Path $extDir) {
        $have = Get-ChildItem $extDir -Directory | Select-Object -ExpandProperty Name
        $missing = @($wantedExtensions | Where-Object { -not ($have -match [regex]::Escape($_)) })
        Write-Host "  before: $($have.Count) installed, $($missing.Count) of $($wantedExtensions.Count) wanted missing"
        foreach ($m in $missing) { Write-Warn2 "missing: $m" }
        if ($missing.Count -eq 0) {
            Write-Ok 'all wanted extensions already present -- skipping Kiro CLI call'
            Write-Host "`nALL DONE.`n" -ForegroundColor Cyan
            return
        }
    }
    $kiroCli = Get-Command kiro -ErrorAction SilentlyContinue
    $kiroExe = if ($kiroCli) { $kiroCli.Source } else { Join-Path $env:LOCALAPPDATA 'Programs\Kiro\bin\kiro.cmd' }
    if (-not (Test-Path $kiroExe)) {
        Write-Warn2 'Kiro CLI not found. Install manually: kiro --install-extension <id>'
    } else {
        # ALL ids in ONE call -- see the WHY LAST note above.
        $argList = @()
        foreach ($e in $wantedExtensions) { $argList += '--install-extension'; $argList += $e }
        $argList += '--force'
        Write-Host "  calling Kiro CLI with $($wantedExtensions.Count) extensions in a single call."
        Write-Host '  (this shell may die right after -- expected; re-run the script to re-check)'
        & $kiroExe @argList
    }
}
Write-Host "`nALL DONE.`n" -ForegroundColor Cyan
