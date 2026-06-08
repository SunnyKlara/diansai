@echo off
chcp 65001 >nul
title 电赛刷题网页 - http://localhost:8765/web/

set PORT=8765

REM 切到仓库根目录（向上一级），让网页能 fetch 到所有真题/积木 markdown
cd /d "%~dp0.."

echo.
echo ============================================
echo   电赛备赛刷题网页
echo   服务根：%CD%
echo   访问： http://localhost:%PORT%/web/
echo   关闭此窗口即可停止服务
echo ============================================
echo.

where python >nul 2>nul
if %errorlevel%==0 (
    start "" "http://localhost:%PORT%/web/"
    python -m http.server %PORT%
    goto :eof
)

where py >nul 2>nul
if %errorlevel%==0 (
    start "" "http://localhost:%PORT%/web/"
    py -m http.server %PORT%
    goto :eof
)

echo [错误] 未检测到 Python，请安装 Python 3 后重试。
pause
