# =====================================================================
#  设计报告一键编译脚本
#  用法：在本目录右键“用 PowerShell 运行”，或：
#        powershell -ExecutionPolicy Bypass -File build.ps1
#  产物：设计报告.pdf（权威版，XeLaTeX）+ 设计报告.docx（可编辑版，pandoc）
# =====================================================================
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

# 刷新 PATH（确保能找到 winget 安装的 xelatex / pandoc）
$env:Path = "$([System.Environment]::GetEnvironmentVariable('Path','Machine'));$([System.Environment]::GetEnvironmentVariable('Path','User'))"

$tex = "设计报告.tex"

Write-Host "[1/3] XeLaTeX 第一遍..." -ForegroundColor Cyan
xelatex -interaction=nonstopmode $tex | Out-Null

Write-Host "[2/3] XeLaTeX 第二遍（目录/交叉引用/表宽收敛）..." -ForegroundColor Cyan
xelatex -interaction=nonstopmode $tex | Out-Null

if (Test-Path "设计报告.pdf") {
    Write-Host "  -> 设计报告.pdf 生成成功 ($((Get-Item '设计报告.pdf').Length) bytes)" -ForegroundColor Green
} else {
    Write-Host "  -> PDF 生成失败，查看 设计报告.log" -ForegroundColor Red
    exit 1
}

Write-Host "[3/3] pandoc 转 Word..." -ForegroundColor Cyan
$pandoc = Get-Command pandoc -ErrorAction SilentlyContinue
if ($pandoc) {
    pandoc $tex -o "设计报告.docx"
    Write-Host "  -> 设计报告.docx 生成成功（注意：TikZ 框图/流程图不会进 Word，需手动插图）" -ForegroundColor Green
} else {
    Write-Host "  -> 未找到 pandoc，跳过 Word 转换" -ForegroundColor Yellow
}

# 清理中间文件
Remove-Item *.aux,*.toc,*.out,*.log -ErrorAction SilentlyContinue
Write-Host "完成。" -ForegroundColor Green
