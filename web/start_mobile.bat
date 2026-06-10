@echo off
chcp 65001 >nul
title 电赛刷题网页 - 手机访问模式

set PORT=8765

REM 切到仓库根目录（向上一级），让网页能 fetch 到所有真题/积木 markdown
cd /d "%~dp0.."

where python >nul 2>nul
if %errorlevel%==0 (
    python web\tools\mobile_serve.py --port %PORT%
    goto :eof
)

where py >nul 2>nul
if %errorlevel%==0 (
    py web\tools\mobile_serve.py --port %PORT%
    goto :eof
)

echo [错误] 未检测到 Python，请安装 Python 3 后重试。
pause
