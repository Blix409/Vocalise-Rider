@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0make_installer.ps1" %*
pause
