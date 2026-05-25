@echo off
chcp 65001 >nul
REM ============================================================
REM 电赛备赛仓库 一键 Git 初始化与首次推送脚本
REM 仓库地址: https://github.com/SunnyKlara/diansai.git
REM 用法: 双击运行，或在 cmd 中执行
REM ============================================================

setlocal enabledelayedexpansion

echo ============================================================
echo  电赛备赛仓库 Git 初始化
echo ============================================================
echo.

REM 切到脚本所在目录
cd /d "%~dp0"
echo 当前目录: %CD%
echo.

REM 检查 git 是否安装
where git >nul 2>nul
if errorlevel 1 (
    echo [错误] 未检测到 git，请先安装 Git for Windows: https://git-scm.com/download/win
    pause
    exit /b 1
)

REM 1. 检查是否已经是 git 仓库
if exist ".git" (
    echo [跳过] 已经是 git 仓库，跳过 git init
) else (
    echo [步骤 1] 初始化 git 仓库
    git init
    if errorlevel 1 goto :error
)
echo.

REM 2. 设置默认分支为 main
echo [步骤 2] 设置默认分支
git symbolic-ref HEAD refs/heads/main 2>nul
echo.

REM 3. 检查 remote
git remote get-url origin >nul 2>nul
if errorlevel 1 (
    echo [步骤 3] 添加远程仓库 origin
    git remote add origin https://github.com/SunnyKlara/diansai.git
    if errorlevel 1 goto :error
) else (
    echo [跳过] 远程仓库 origin 已存在
    git remote -v
)
echo.

REM 4. 显示状态
echo [步骤 4] 当前 git 状态
echo ------------------------------------------------------------
git status --short
echo ------------------------------------------------------------
echo.

REM 5. 询问是否继续
echo 即将执行:
echo   - git add .（添加所有未忽略文件）
echo   - git commit -m "chore: initial commit - 电赛备赛仓库初始化"
echo   - git push -u origin main
echo.
set /p CONFIRM=继续吗? (Y/N): 
if /i not "%CONFIRM%"=="Y" (
    echo 已取消。你可以稍后手动执行：
    echo   git add .
    echo   git commit -m "your message"
    echo   git push -u origin main
    pause
    exit /b 0
)
echo.

REM 6. 添加文件
echo [步骤 5] git add .
git add .
if errorlevel 1 goto :error
echo.

REM 7. 提交
echo [步骤 6] git commit
git commit -m "chore: initial commit - 电赛备赛仓库初始化"
if errorlevel 1 (
    echo [警告] commit 失败，可能没有变化或需要先配置 user.name/user.email
    echo 如果是首次使用 git，请先执行：
    echo   git config --global user.name "你的名字"
    echo   git config --global user.email "你的邮箱"
    pause
    exit /b 1
)
echo.

REM 8. 推送
echo [步骤 7] git push（首次推送可能需要登录 GitHub）
git push -u origin main
if errorlevel 1 (
    echo.
    echo [警告] push 失败。常见原因：
    echo   1. 远程仓库已有内容 - 可尝试: git pull origin main --allow-unrelated-histories
    echo   2. 未登录 GitHub - 浏览器会弹出认证窗口
    echo   3. 仓库不存在 - 确认 https://github.com/SunnyKlara/diansai 已创建
    pause
    exit /b 1
)
echo.

echo ============================================================
echo  完成！仓库已推送到 https://github.com/SunnyKlara/diansai
echo ============================================================
pause
exit /b 0

:error
echo.
echo [错误] 执行出错，请查看上方信息
pause
exit /b 1
