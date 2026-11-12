@echo off
setlocal
cd /d "%~dp0"
if not exist ".venv\Scripts\python.exe" (
    echo [1/3] Creating Python environment...
    py -3 -m venv .venv
    if errorlevel 1 goto failed
)
echo [2/3] Checking dependencies...
".venv\Scripts\python.exe" -c "import serial, tkinter" >nul 2>&1
if errorlevel 1 (
    ".venv\Scripts\python.exe" -m pip install -r requirements.txt
    if errorlevel 1 goto failed
)
echo [3/3] Opening STM32 Light Console...
echo Keep this window open while using the application. You may minimize it.
".venv\Scripts\python.exe" -u "%~dp0app.py"
if errorlevel 1 goto failed
exit /b 0
:failed
echo.
echo Startup failed. Please copy the error text above or send a screenshot.
pause
exit /b 1
