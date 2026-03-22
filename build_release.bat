@echo off
title Diablo II Archipelago - Build Release Package

echo ============================================
echo   Building Release Package
echo ============================================
echo.

set SRC=%~dp0
set REL=%SRC%Release\Diablo II Archipelago

:: Clean previous release
if exist "%SRC%Release" (
    echo Cleaning previous release...
    rmdir /S /Q "%SRC%Release"
)

:: Create folder structure
echo Creating folder structure...
mkdir "%REL%"
mkdir "%REL%\Archipelago"
mkdir "%REL%\Archipelago\build"
mkdir "%REL%\Archipelago\data"
mkdir "%REL%\Archipelago\files"
mkdir "%REL%\Archipelago\files\1.10"
mkdir "%REL%\Archipelago\files\plugy"
mkdir "%REL%\Archipelago\files\plugy\PlugY"
mkdir "%REL%\Archipelago\files\modfiles"
mkdir "%REL%\Archipelago\files\graphics"
mkdir "%REL%\Archipelago\files\singling"
mkdir "%REL%\Archipelago\files\mpqfixer"
mkdir "%REL%\Archipelago\src"
mkdir "%REL%\Archipelago\tools"

:: ============================================
:: Copy D2ArchSetup.exe (the GUI installer)
:: ============================================
echo Copying installer...
copy /Y "%SRC%Archipelago\build\D2ArchSetup.exe" "%REL%\" >nul

:: ============================================
:: Copy build artifacts
:: ============================================
echo Copying build artifacts...
copy /Y "%SRC%Archipelago\build\D2Archipelago.dll" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\d2skillreset.exe" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\D2MpqGen.exe" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\SFMPQ.dll" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\extract_icons.exe" "%REL%\Archipelago\build\" >nul
if exist "%SRC%Archipelago\build\icon_offsets.dat" copy /Y "%SRC%Archipelago\build\icon_offsets.dat" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\D2Launcher.exe" "%REL%\Archipelago\build\" >nul
copy /Y "%SRC%Archipelago\build\D2Launcher.exe" "%REL%\Play Archipelago.exe" >nul

:: ============================================
:: Copy data files (icons, vanilla txt)
:: ============================================
echo Copying Archipelago data files...
xcopy "%SRC%Archipelago\data\*" "%REL%\Archipelago\data\" /E /I /Y /Q >nul 2>nul

:: ============================================
:: Copy GENERATED game data from working dir
:: These files are pre-generated and must be included.
:: ============================================
echo Copying generated game data (bin files, icons)...
mkdir "%REL%\data" 2>nul
mkdir "%REL%\data\global" 2>nul
mkdir "%REL%\data\global\excel" 2>nul
mkdir "%REL%\data\global\ui" 2>nul
mkdir "%REL%\data\global\ui\spells" 2>nul
xcopy "%SRC%data\global\excel\*" "%REL%\data\global\excel\" /Y /Q >nul 2>nul
xcopy "%SRC%data\global\ui\spells\*" "%REL%\data\global\ui\spells\" /Y /Q >nul 2>nul

:: ============================================
:: Copy patch files
:: ============================================
echo Copying 1.10f patch files...
copy /Y "%SRC%Archipelago\files\1.10\*" "%REL%\Archipelago\files\1.10\" >nul

echo Copying PlugY files...
copy /Y "%SRC%Archipelago\files\plugy\PlugY.dll" "%REL%\Archipelago\files\plugy\" >nul
copy /Y "%SRC%Archipelago\files\plugy\PlugY.ini" "%REL%\Archipelago\files\plugy\" >nul
copy /Y "%SRC%Archipelago\files\plugy\D2Mod.dll" "%REL%\Archipelago\files\plugy\" >nul
copy /Y "%SRC%Archipelago\files\plugy\D2Mod.ini" "%REL%\Archipelago\files\plugy\" >nul
if exist "%SRC%Archipelago\files\plugy\PlugY" (
    xcopy "%SRC%Archipelago\files\plugy\PlugY\*" "%REL%\Archipelago\files\plugy\PlugY\" /E /I /Y /Q >nul 2>nul
)

echo Copying mod files...
copy /Y "%SRC%Archipelago\files\modfiles\*" "%REL%\Archipelago\files\modfiles\" >nul

echo Copying graphics files...
copy /Y "%SRC%Archipelago\files\graphics\ddraw.dll" "%REL%\Archipelago\files\graphics\" >nul
copy /Y "%SRC%Archipelago\files\graphics\ddraw_cnc.dll" "%REL%\Archipelago\files\graphics\" >nul
copy /Y "%SRC%Archipelago\files\graphics\ddraw.ini" "%REL%\Archipelago\files\graphics\" >nul

echo Copying singling patches...
copy /Y "%SRC%Archipelago\files\singling\*" "%REL%\Archipelago\files\singling\" >nul

echo Copying MPQ fixer...
copy /Y "%SRC%Archipelago\files\mpqfixer\WinMPQ.exe" "%REL%\Archipelago\files\mpqfixer\" >nul
copy /Y "%SRC%Archipelago\files\mpqfixer\MSCOMCTL.OCX" "%REL%\Archipelago\files\mpqfixer\" >nul
copy /Y "%SRC%Archipelago\files\mpqfixer\VB40032.DLL" "%REL%\Archipelago\files\mpqfixer\" >nul
copy /Y "%SRC%Archipelago\files\mpqfixer\SFMPQ.dll" "%REL%\Archipelago\files\mpqfixer\" >nul
copy /Y "%SRC%Archipelago\files\mpqfixer\FIX_MPQS_RUN_AS_ADMIN.bat" "%REL%\Archipelago\files\mpqfixer\" >nul
if exist "%SRC%Archipelago\files\mpqfixer\mpqfix.exe" copy /Y "%SRC%Archipelago\files\mpqfixer\mpqfix.exe" "%REL%\Archipelago\files\mpqfixer\" >nul
if exist "%SRC%Archipelago\files\mpqfixer\mpq_extract.exe" copy /Y "%SRC%Archipelago\files\mpqfixer\mpq_extract.exe" "%REL%\Archipelago\files\mpqfixer\" >nul

:: ============================================
:: Copy source code
:: ============================================
echo Copying source code...
copy /Y "%SRC%Archipelago\src\d2arch.c" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\d2skillreset.c" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\installer.c" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\launcher.c" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\build.bat" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\build_launcher.bat" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\ap_bridge.py" "%REL%\Archipelago\src\" >nul
copy /Y "%SRC%Archipelago\src\ap_bridge_gui.py" "%REL%\Archipelago\src\" >nul
if exist "%SRC%Archipelago\src\app.ico" copy /Y "%SRC%Archipelago\src\app.ico" "%REL%\Archipelago\src\" >nul
if exist "%SRC%Archipelago\src\launcher.rc" copy /Y "%SRC%Archipelago\src\launcher.rc" "%REL%\Archipelago\src\" >nul
if exist "%SRC%Archipelago\src\installer.rc" copy /Y "%SRC%Archipelago\src\installer.rc" "%REL%\Archipelago\src\" >nul
if exist "%SRC%Archipelago\src\installer.manifest" copy /Y "%SRC%Archipelago\src\installer.manifest" "%REL%\Archipelago\src\" >nul

:: ============================================
:: Copy state/config .dat files
:: ============================================
echo Copying state files...
if exist "%SRC%Archipelago\icon_offsets.dat" copy /Y "%SRC%Archipelago\icon_offsets.dat" "%REL%\Archipelago\" >nul
if exist "%SRC%Archipelago\skill_icon_map.dat" copy /Y "%SRC%Archipelago\skill_icon_map.dat" "%REL%\Archipelago\" >nul
if exist "%SRC%Archipelago\launcher.cfg" copy /Y "%SRC%Archipelago\launcher.cfg" "%REL%\Archipelago\" >nul
:: Per-character state/slots/checks files are NOT included — auto-generated

:: ============================================
:: Copy character templates (acc Temp)
:: ============================================
echo Copying character templates...
if exist "%SRC%Archipelago\acc Temp" (
    mkdir "%REL%\Archipelago\acc Temp" 2>nul
    xcopy "%SRC%Archipelago\acc Temp\*" "%REL%\Archipelago\acc Temp\" /E /I /Y /Q >nul 2>nul
)

:: ============================================
:: Copy apworld package
:: ============================================
echo Copying apworld...
if exist "%SRC%apworld" (
    mkdir "%REL%\apworld" 2>nul
    copy /Y "%SRC%apworld\*.apworld" "%REL%\apworld\" >nul 2>nul
)

:: ============================================
:: Copy lib (reference headers for development)
:: ============================================
echo Copying lib references...
xcopy "%SRC%Archipelago\lib\*" "%REL%\Archipelago\lib\" /E /I /Y /Q >nul 2>nul

:: ============================================
:: Copy tools source
:: ============================================
echo Copying tools...
copy /Y "%SRC%Archipelago\tools\extract_icons.c" "%REL%\Archipelago\tools\" >nul 2>nul
copy /Y "%SRC%Archipelago\tools\merge_icons.py" "%REL%\Archipelago\tools\" >nul 2>nul

:: ============================================
:: Verify
:: ============================================
echo.
echo Verifying release package...

set OK=1
if not exist "%REL%\Play Archipelago.exe" (echo   MISSING: Play Archipelago.exe & set OK=0)
if not exist "%REL%\D2ArchSetup.exe" (echo   MISSING: D2ArchSetup.exe & set OK=0)
if not exist "%REL%\Archipelago\build\D2Archipelago.dll" (echo   MISSING: D2Archipelago.dll & set OK=0)
if not exist "%REL%\Archipelago\build\D2Launcher.exe" (echo   MISSING: D2Launcher.exe & set OK=0)
if not exist "%REL%\Archipelago\build\d2skillreset.exe" (echo   MISSING: d2skillreset.exe & set OK=0)
if not exist "%REL%\Archipelago\build\D2MpqGen.exe" (echo   MISSING: D2MpqGen.exe & set OK=0)
if not exist "%REL%\Archipelago\build\SFMPQ.dll" (echo   MISSING: SFMPQ.dll & set OK=0)
if not exist "%REL%\Archipelago\build\extract_icons.exe" (echo   MISSING: extract_icons.exe & set OK=0)
if not exist "%REL%\Archipelago\files\1.10\Game.exe" (echo   MISSING: 1.10/Game.exe & set OK=0)
if not exist "%REL%\Archipelago\files\plugy\PlugY.dll" (echo   MISSING: plugy/PlugY.dll & set OK=0)
if not exist "%REL%\Archipelago\files\plugy\D2Mod.dll" (echo   MISSING: plugy/D2Mod.dll & set OK=0)
if not exist "%REL%\Archipelago\files\modfiles\CustomTbl.dll" (echo   MISSING: modfiles/CustomTbl.dll & set OK=0)
if not exist "%REL%\Archipelago\files\graphics\ddraw.dll" (echo   MISSING: graphics/ddraw.dll & set OK=0)
if not exist "%REL%\Archipelago\files\singling\D2Client.dll" (echo   MISSING: singling/D2Client.dll & set OK=0)
if not exist "%REL%\Archipelago\src\d2arch.c" (echo   MISSING: src/d2arch.c & set OK=0)
if not exist "%REL%\Archipelago\src\launcher.c" (echo   MISSING: src/launcher.c & set OK=0)
if not exist "%REL%\Archipelago\src\ap_bridge.py" (echo   MISSING: src/ap_bridge.py & set OK=0)
if not exist "%REL%\Archipelago\src\ap_bridge_gui.py" (echo   MISSING: src/ap_bridge_gui.py & set OK=0)
if not exist "%REL%\data\global\excel\Skills.txt" (echo   MISSING: data/global/excel/Skills.txt & set OK=0)
if not exist "%REL%\data\global\ui\spells\AsSkillicon.DC6" (echo   MISSING: data/global/ui/spells/AsSkillicon.DC6 & set OK=0)
if not exist "%REL%\Archipelago\skill_icon_map.dat" (echo   MISSING: Archipelago/skill_icon_map.dat & set OK=0)

if "%OK%"=="1" (
    echo   All checks passed.
) else (
    echo.
    echo   Some files missing!
)

echo.
echo ============================================
echo   Release package ready!
echo ============================================
echo   Location: %REL%
echo.
echo   Contents:
echo     D2ArchSetup.exe    - GUI installer
echo     Archipelago\       - All mod files
echo.
echo   To distribute: zip the folder
echo   "Release\Diablo II Archipelago" and upload.
echo ============================================
echo.
pause
