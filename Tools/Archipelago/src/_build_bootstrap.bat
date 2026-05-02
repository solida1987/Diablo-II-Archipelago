@echo off
REM Build D2Arch_Launcher.exe from d2arch_bootstrap.c
REM Produces: Tools\Archipelago\src\D2Arch_Launcher.exe
REM Deploy: copy /Y D2Arch_Launcher.exe ..\..\..\Game\D2Arch_Launcher.exe
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl.exe /nologo /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS d2arch_bootstrap.c /Fe:D2Arch_Launcher.exe /link user32.lib kernel32.lib advapi32.lib
echo EXIT_CODE=%ERRORLEVEL%
