@echo off
setlocal
cd /d "%~dp0"

set "CF=%~dp0..\bin\cloudflared.exe"
if not exist "%CF%" set "CF=cloudflared"

if not exist "%USERPROFILE%\.cloudflared\config.yml" (
  echo.
  echo No tunnel configured yet. Run setup-tunnel.cmd once.
  echo.
  timeout /t 8 >nul
  exit /b 1
)

title ESP32 Tunnel
echo.
echo  Cloudflare tunnel: chat.blueflutex.in -^> localhost:8787
echo  Keep this window open while using the ESP32.
echo.

:loop
"%CF%" tunnel --config "%USERPROFILE%\.cloudflared\config.yml" run esp32-chat
echo.
echo Tunnel stopped; restarting in 5 seconds. Press Ctrl+C to stop.
timeout /t 5 >nul
goto loop
