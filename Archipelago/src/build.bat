@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86

set "SRC=%~dp0"
set "OUT=%~dp0..\build"

if not exist "%OUT%" mkdir "%OUT%"

echo Building D2Archipelago.dll v2...
cl /nologo /MT /O2 /LD /TP "%SRC%d2arch.c" /Fe:"%OUT%\D2Archipelago.dll" /link /SUBSYSTEM:WINDOWS kernel32.lib user32.lib
if errorlevel 1 (
    echo [ERROR] Build failed!
    pause
    exit /b 1
)
echo [OK] D2Archipelago.dll built.

del "%OUT%\*.obj" "%OUT%\*.lib" "%OUT%\*.exp" 2>nul

echo.
echo Output: %OUT%\D2Archipelago.dll
pause
