@echo off
setlocal
echo Building D2Archipelago.dll (Diablo II Lod ny)...

:: Find Visual Studio (x86 toolchain ? D2 is 32-bit)
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call "%VCVARS%" x86 >nul 2>&1

:: Unity build: d2arch.c #includes the d2arch_*.c modules; d2detours_hook.cpp is a
:: separate TU; d2arch.def exports the DLL entry points. Compiled as C++ (/TP) for
:: __try/__except.
cl /nologo /MT /O2 /W3 /LD /TP /D_CRT_SECURE_NO_WARNINGS ^
    d2arch.c ^
    d2detours_hook.cpp ^
    /Fe:D2Archipelago.dll ^
    /link /DEF:d2arch.def ^
    /MAP:D2Archipelago.map ^
    user32.lib gdi32.lib kernel32.lib advapi32.lib

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo BUILD FAILED ^(exit %ERRORLEVEL%^)
    exit /b %ERRORLEVEL%
)

echo.
echo BUILD SUCCESS: D2Archipelago.dll
:: Deploy to ..\Game\ and ..\Game\patch\ (patch\ is the authoritative load path;
:: Game\ kept in sync as a convenience copy ? same as the old layout).
copy /Y D2Archipelago.dll "..\Game\D2Archipelago.dll" >nul
copy /Y D2Archipelago.dll "..\Game\patch\D2Archipelago.dll" >nul
echo Deployed to ..\Game\ and ..\Game\patch\
exit /b 0
