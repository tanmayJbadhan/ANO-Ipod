@echo off
rem Double-click to build and flash the console. Extra options are passed
rem through to flash.ps1, e.g.  flash.bat -Port COM12 -Monitor
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0flash.ps1" %*
echo.
pause
