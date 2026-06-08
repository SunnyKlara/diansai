@echo off
setlocal
chcp 65001 >nul

set "YEAR="
set "PROBLEM="
set "MODE=hardware"
set "ROOT="

:parse
if "%~1"=="" goto check
if /i "%~1"=="--year"    (set "YEAR=%~2"    & shift & shift & goto parse)
if /i "%~1"=="--problem" (set "PROBLEM=%~2" & shift & shift & goto parse)
if /i "%~1"=="--mode"    (set "MODE=%~2"    & shift & shift & goto parse)
if /i "%~1"=="--root"    (set "ROOT=%~2"    & shift & shift & goto parse)
shift
goto parse

:check
if "%YEAR%"==""    (echo [ERROR] --year missing & exit /b 1)
if "%PROBLEM%"=="" (echo [ERROR] --problem missing & exit /b 1)

where py >nul 2>nul
if %ERRORLEVEL%==0 (
    set "PY=py -3"
) else (
    set "PY=python"
)

set "ARGS=--year %YEAR% --problem %PROBLEM% --mode %MODE% --templates "%~dp0_templates""
if not "%ROOT%"=="" set "ARGS=%ARGS% --root "%ROOT%""

%PY% "%~dp0_render.py" %ARGS%

endlocal
