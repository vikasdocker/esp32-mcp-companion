@echo off
setlocal
cd /d "%~dp0"
title ESP32 Tunnel Setup

echo.
echo  Cloudflare tunnel setup
echo  -----------------------
echo.
echo This creates the public URL https://chat.blueflutex.in for the bridge.
echo Run it once. The first run opens a browser to pick the blueflutex.in zone.
echo.

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0setup-tunnel.ps1"

echo.
pause
