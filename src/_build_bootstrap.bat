@echo off
setlocal
REM Build D2Arch_Launcher.exe from d2arch_bootstrap.c (Diablo II Lod ny).
REM Deploys to ..\Game\ so START.bat works and the C# launcher can fall back to it.
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
if not exist "%VCVARS%" set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat"
call "%VCVARS%" x86 >nul 2>&1
cl.exe /nologo /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS d2arch_bootstrap.c /Fe:D2Arch_Launcher.exe /link user32.lib kernel32.lib advapi32.lib
if %ERRORLEVEL% NEQ 0 (
    echo BUILD FAILED ^(exit %ERRORLEVEL%^)
    exit /b %ERRORLEVEL%
)
copy /Y D2Arch_Launcher.exe "..\Game\D2Arch_Launcher.exe" >nul
echo BUILD SUCCESS: D2Arch_Launcher.exe deployed to ..\Game\
exit /b 0
