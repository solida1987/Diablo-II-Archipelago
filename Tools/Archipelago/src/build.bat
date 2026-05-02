@echo off
echo Building D2Archipelago.dll...

:: Find Visual Studio
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"

call "%VCVARS%" x86 >nul 2>&1

:: Build - compile d2arch.c as C++ (for __try/__except) and link with hook
cl /nologo /MT /O2 /W3 /LD /TP ^
    d2arch.c ^
    d2detours_hook.cpp ^
    /Fe:D2Archipelago.dll ^
    /link /DEF:d2arch.def ^
    user32.lib gdi32.lib kernel32.lib advapi32.lib

if %ERRORLEVEL% EQU 0 (
    echo.
    echo BUILD SUCCESS: D2Archipelago.dll
    echo.
    :: Copy to patch folder
    copy /Y D2Archipelago.dll "..\..\patch\D2Archipelago.dll" >nul 2>&1
    if %ERRORLEVEL% EQU 0 (
        echo Deployed to patch\D2Archipelago.dll
    ) else (
        echo NOTE: Could not copy to patch folder. Copy manually.
    )
) else (
    echo.
    echo BUILD FAILED!
)

pause
