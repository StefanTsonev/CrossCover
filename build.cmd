@echo off
setlocal

cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO%" (
    echo PlatformIO's Python 3.11 environment was not found:
    echo   %PIO%
    echo Install PlatformIO or run this script from a configured PlatformIO setup.
    exit /b 1
)

"%PIO%" run -e default %*
exit /b %ERRORLEVEL%
