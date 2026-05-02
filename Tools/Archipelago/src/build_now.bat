@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cd /d "%~dp0"
cl /nologo /MT /O2 /W3 /LD /TP /D_CRT_SECURE_NO_WARNINGS d2arch.c /Fe:D2Archipelago.dll /link user32.lib gdi32.lib kernel32.lib advapi32.lib
if %ERRORLEVEL% EQU 0 (
    copy /Y D2Archipelago.dll ..\..\patch\D2Archipelago.dll
    echo BUILD OK - deployed to patch\
) else (
    echo BUILD FAILED
)
