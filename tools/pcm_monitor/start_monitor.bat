@echo off

setlocal EnableExtensions

cd /d "%~dp0"



set "SCRIPT=%~dp0pcm_serial_bridge.py"

set "LIST=%~dp0list_ports.py"

set "URLFILE=%~dp0monitor.url"



echo ESP PCM Serial Monitor

echo Folder: %~dp0

echo.



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



echo Using Python: %PYRUN%

%PYRUN% -c "import serial" >nul 2>&1

if errorlevel 1 (

  echo Installing pyserial...

  %PYRUN% -m pip install pyserial

)



echo.

echo === COM port scan ===

%PYRUN% "%LIST%"

echo.



if exist "%URLFILE%" del /f /q "%URLFILE%" >nul 2>&1



echo Starting bridge in new window...

start "PCM Monitor" /D "%~dp0" cmd /k %PYRUN% "%SCRIPT%" --no-browser



echo Waiting for bridge URL...

set "BRURL="

for /L %%I in (1,1,20) do (

  if exist "%URLFILE%" (

    set /p BRURL=<"%URLFILE%"

    goto OpenBrowser

  )

  ping -n 2 127.0.0.1 >nul

)



echo WARNING: monitor.url not found. Check the PCM Monitor window for errors.

echo Try opening: http://127.0.0.1:8765/pcm_serial_monitor.html

pause

exit /b 1



:OpenBrowser

echo.

echo Bridge URL: %BRURL%

echo Opening browser...

start "" "%BRURL%"

echo.

echo If COM list is empty, click Refresh or type COM23 in Manual COM.

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

if not errorlevel 1 (

  set "PYEXE=py"

  goto FindPythonDone

)

if exist "%USERPROFILE%\.espressif\python_env" (

  for /f "delims=" %%D in ('dir /b /ad /o-n "%USERPROFILE%\.espressif\python_env\idf*_py*_env" 2^>nul') do (

    if exist "%USERPROFILE%\.espressif\python_env\%%D\Scripts\python.exe" (

      set "PYEXE=%USERPROFILE%\.espressif\python_env\%%D\Scripts\python.exe"

      goto FindPythonDone

    )

  )

)

:FindPythonDone

exit /b 0

