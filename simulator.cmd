@echo off
setlocal
cd /d "%~dp0"
set "PYTHONUTF8=1"
if exist ".pio-core312\penv\Scripts\python.exe" (
  set "SIM_PYTHON=.pio-core312\penv\Scripts\python.exe"
) else if exist "%USERPROFILE%\.platformio\penv\Scripts\python.exe" (
  set "SIM_PYTHON=%USERPROFILE%\.platformio\penv\Scripts\python.exe"
) else (
  echo PlatformIO Python was not found. Install PlatformIO first.
  exit /b 1
)
"%SIM_PYTHON%" scripts\windows_simulator.py %*
exit /b %errorlevel%
