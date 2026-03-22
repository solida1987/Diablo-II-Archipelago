@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
rc /nologo launcher.rc
cl /nologo /MT /O2 launcher.c launcher.res /Fe:..\build\D2Launcher.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comctl32.lib shell32.lib ole32.lib uuid.lib comdlg32.lib advapi32.lib > build_result.txt 2>&1
type build_result.txt
if errorlevel 1 (
    echo [ERROR] Build failed!
    exit /b 1
)
echo [OK] D2Launcher.exe built.
del *.obj 2>nul
copy /Y "..\build\D2Launcher.exe" "..\..\Play Archipelago.exe" >nul
echo [OK] Copied to "Play Archipelago.exe"
