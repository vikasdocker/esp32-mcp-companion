@echo off
setlocal
cd /d "%~dp0esp32_opencode_chat"

set FQBN=esp32:esp32:esp32:PartitionScheme=huge_app
set PORT=COM5

rem --- locate arduino-cli (PATH first, then the default installer location) ---
set "ARDUINO_CLI="
where arduino-cli >nul 2>&1 && set "ARDUINO_CLI=arduino-cli"
if not defined ARDUINO_CLI if exist "%ProgramFiles%\Arduino CLI\arduino-cli.exe" set "ARDUINO_CLI=%ProgramFiles%\Arduino CLI\arduino-cli.exe"
if not defined ARDUINO_CLI if exist "%LOCALAPPDATA%\Programs\Arduino CLI\arduino-cli.exe" set "ARDUINO_CLI=%LOCALAPPDATA%\Programs\Arduino CLI\arduino-cli.exe"
if not defined ARDUINO_CLI (
  echo Arduino CLI not found.
  echo Install it from https://arduino.github.io/arduino-cli/ or add it to PATH.
  pause
  exit /b 1
)

echo.
echo Compiling ESP32 OpenCode Chat...
echo.
"%ARDUINO_CLI%" compile --fqbn %FQBN% .
if errorlevel 1 goto :fail

echo.
echo Uploading to %PORT%...
echo.
"%ARDUINO_CLI%" upload -p %PORT% --fqbn %FQBN% .
if errorlevel 1 goto :fail

echo.
echo Done. Open the serial monitor with:
echo   "%ARDUINO_CLI%" monitor -p %PORT% -c baudrate=115200
echo.
pause
exit /b 0

:fail
echo.
echo Build or upload failed. Check the messages above.
echo.
pause
exit /b 1
