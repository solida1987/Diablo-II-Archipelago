@echo off
cd /d "%~dp0"
title Diablo II Archipelago - Beta 1.8.3
color 0A
echo ============================================
echo   Diablo II Archipelago - Beta 1.8.3
echo ============================================
echo.

del d2arch_log.txt 2>nul
set DIABLO2_PATCH=%CD%\patch

echo Starting game...
echo.
start "" D2Arch_Launcher.exe -3dfx -direct -txt -log

echo Waiting for log file...
:waitlog
if not exist d2arch_log.txt (
    timeout /t 1 /nobreak >nul
    goto waitlog
)

echo Log file found. Streaming live output:
echo ============================================
echo.
powershell -NoProfile -Command "Get-Content d2arch_log.txt -Wait -Tail 100"
