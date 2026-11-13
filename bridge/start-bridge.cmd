@echo off
setlocal
cd /d "%~dp0"
title ESP32 OpenCode Bridge

echo.
echo  ESP32 OpenCode Bridge
echo  ---------------------
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0start-hotspot.ps1"

echo.
echo Starting the public tunnel (chat.blueflutex.in)...
start "ESP32 Tunnel" /min "%~dp0start-tunnel.cmd"

echo.
echo Starting bridge on port 8787...
echo Keep this window open while using the ESP32.
echo.
node server.js

echo.
echo Bridge stopped. Press any key to close.
pause >nul
