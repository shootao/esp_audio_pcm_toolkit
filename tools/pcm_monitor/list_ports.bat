@echo off
setlocal EnableExtensions
cd /d "%~dp0"

call :FindPython
if not defined PYEXE (
  echo ERROR: Python not found.
  pause
  exit /b 1
)

if /I "%PYEXE%"=="py" (
  set "PYRUN=py -3"
) else (
  set "PYRUN=%PYEXE%"
)

%PYRUN% "%~dp0list_ports.py"
echo.
pause
exit /b 0

:FindPython
set "PYEXE="
where python >nul 2>&1
if not errorlevel 1 (
  for /f "delims=" %%P in ('where python 2^>nul') do (
    set "PYEXE=%%P"
    goto FindPythonDone
  )
)
where py >nul 2>&1
if not errorlevel 1 set "PYEXE=py"
:FindPythonDone
exit /b 0
