@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
cl.exe /nologo /MT /O2 /W3 /LD /TP /D_CRT_SECURE_NO_WARNINGS d2arch.c /Fe:D2Archipelago.dll /link user32.lib gdi32.lib kernel32.lib advapi32.lib
if %ERRORLEVEL% EQU 0 (
    rem 1.9.0 - deploy to BOTH Game\ and Game\patch\.
    rem The game loads D2Archipelago.dll from %CD%\patch (set as
    rem DIABLO2_PATCH in START.bat) so Game\patch\ is the authoritative
    rem load path. Game\ kept in sync as a convenience copy.
    copy /Y D2Archipelago.dll "..\..\..\Game\D2Archipelago.dll" >nul
    copy /Y D2Archipelago.dll "..\..\..\Game\patch\D2Archipelago.dll" >nul
)
echo EXIT_CODE=%ERRORLEVEL%
