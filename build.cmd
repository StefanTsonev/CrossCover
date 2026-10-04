@echo off
setlocal

cd /d "%~dp0"
set "PIO=%USERPROFILE%\.platformio\penv\Scripts\pio.exe"

if not exist "%PIO%" (
    echo PlatformIO's Python 3.11 environment was not found:
    echo   %PIO%
    echo Install PlatformIO or run this script from a configured PlatformIO setup.
    set "BUILD_EXIT_CODE=1"
    goto finish
)

"%PIO%" run -e default %*
set "BUILD_EXIT_CODE=%ERRORLEVEL%"

:finish
echo.
echo Build finished with exit code %BUILD_EXIT_CODE%.
pause
exit /b %BUILD_EXIT_CODE%
