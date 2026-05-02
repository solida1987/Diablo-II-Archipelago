@echo off
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl.exe /nologo /MT /O2 /W3 /LD /TP /D_CRT_SECURE_NO_WARNINGS d2arch.c /Fe:D2Archipelago.dll /link user32.lib gdi32.lib kernel32.lib advapi32.lib
if %ERRORLEVEL% EQU 0 (
    echo.
    echo === BUILD SUCCESS ===
    copy /Y D2Archipelago.dll "..\..\D2Archipelago.dll" >nul
    echo DLL copied to Game directory
) else (
    echo.
    echo === BUILD FAILED ===
)
pause
