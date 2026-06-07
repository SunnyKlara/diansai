@echo off
chcp 65001 >nul
title 电赛刷题网页 - http://localhost:8765/备赛系统/F_刷题网页/

set PORT=8765

REM 切到仓库根目录（向上两级），让网页能 fetch 到所有真题 markdown
cd /d "%~dp0..\.."

echo.
echo ============================================
echo   电赛备赛刷题网页
echo   服务根：%CD%
echo   访问： http://localhost:%PORT%/备赛系统/F_刷题网页/
echo   关闭此窗口即可停止服务
echo ============================================
echo.

where python >nul 2>nul
if %errorlevel%==0 (
    start "" "http://localhost:%PORT%/%E5%A4%87%E8%B5%9B%E7%B3%BB%E7%BB%9F/F_%E5%88%B7%E9%A2%98%E7%BD%91%E9%A1%B5/"
    python -m http.server %PORT%
    goto :eof
)

where py >nul 2>nul
if %errorlevel%==0 (
    start "" "http://localhost:%PORT%/%E5%A4%87%E8%B5%9B%E7%B3%BB%E7%BB%9F/F_%E5%88%B7%E9%A2%98%E7%BD%91%E9%A1%B5/"
    py -m http.server %PORT%
    goto :eof
)

echo [错误] 未检测到 Python，请安装 Python 3 后重试。
pause
