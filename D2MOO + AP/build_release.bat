@echo off
title Diablo II Archipelago beta-1.0.0 - Build Release Package
echo ============================================
echo   Building Release Package (D2MOO + AP)
echo ============================================
echo.

set SRC=%~dp0
set REL=%SRC%Release\Diablo II Archipelago

:: Clean previous release
if exist "%SRC%Release" (
    echo Cleaning previous release...
    rmdir /S /Q "%SRC%Release"
)

:: ============================================
:: Create folder structure
:: Root: ONLY D2ArchSetup.exe
:: Everything else goes into files\
:: ============================================
echo Creating folder structure...
mkdir "%REL%"
mkdir "%REL%\files"
mkdir "%REL%\files\game"
mkdir "%REL%\files\patch"
mkdir "%REL%\files\framework"
mkdir "%REL%\files\data"
mkdir "%REL%\files\data\global"
mkdir "%REL%\files\data\global\excel"
mkdir "%REL%\files\data\global\ui"
mkdir "%REL%\files\data\global\ui\SPELLS"
mkdir "%REL%\files\Archipelago"
:: NOTE: src/ and research/ are NOT included in release — internal development only

:: ============================================
:: ROOT: Only the installer EXE
:: ============================================
echo Copying installer to root...
if exist "%SRC%Archipelago\src\D2ArchSetup.exe" (
    copy /Y "%SRC%Archipelago\src\D2ArchSetup.exe" "%REL%\" >nul
) else (
    echo   ERROR: D2ArchSetup.exe not found! Build it first.
    echo   cd Archipelago\src
    echo   rc /nologo installer.rc
    echo   cl /nologo /MT /O2 /W3 installer.c installer.res /Fe:D2ArchSetup.exe /link user32.lib gdi32.lib shell32.lib ole32.lib uuid.lib comctl32.lib advapi32.lib
    pause
    exit /b 1
)

:: ============================================
:: files\game: 1.10f Game DLLs + EXEs
:: ============================================
echo Copying 1.10f game files to files\game\...
for %%f in (Bnclient.dll D2CMP.dll D2Client.dll D2Common.dll D2DDraw.dll D2Direct3D.dll D2Game.dll D2Gdi.dll D2Glide.dll D2Lang.dll D2Launch.dll D2MCPClient.dll D2Multi.dll D2Net.dll D2Win.dll D2gfx.dll D2sound.dll Fog.dll Storm.dll SmackW32.dll binkw32.dll ijl11.dll dsound.dll winmm.dll dsoal-aldrv.dll D2Archipelago.dll) do (
    if exist "%SRC%%%f" copy /Y "%SRC%%%f" "%REL%\files\game\" >nul
)
if exist "%SRC%Game.exe" copy /Y "%SRC%Game.exe" "%REL%\files\game\" >nul
if exist "%SRC%Diablo II.exe" copy /Y "%SRC%Diablo II.exe" "%REL%\files\game\" >nul

:: ============================================
:: files\patch: D2MOO replacement DLLs + our mod
:: ============================================
echo Copying patch DLLs to files\patch\...
for %%f in (D2Archipelago.dll D2Common.dll D2Game.dll Fog.dll D2Debugger.dll) do (
    if exist "%SRC%patch\%%f" copy /Y "%SRC%patch\%%f" "%REL%\files\patch\" >nul
)

:: ============================================
:: files\framework: D2.Detours, launcher, ddraw
:: ============================================
echo Copying framework to files\framework\...
copy /Y "%SRC%D2.Detours.dll" "%REL%\files\framework\" >nul
copy /Y "%SRC%D2.DetoursLauncher.exe" "%REL%\files\framework\" >nul
:: Launcher ships as "Play Archipelago.exe" only — no duplicate D2ArchLauncher.exe
copy /Y "%SRC%Archipelago\src\D2ArchLauncher.exe" "%REL%\files\framework\Play Archipelago.exe" >nul
copy /Y "%SRC%ddraw.dll" "%REL%\files\framework\" >nul
copy /Y "%SRC%ddraw.ini" "%REL%\files\framework\" >nul
:: AP Bridge executable (PyInstaller-built)
if exist "%SRC%Archipelago\src\dist\ap_bridge.exe" (
    copy /Y "%SRC%Archipelago\src\dist\ap_bridge.exe" "%REL%\files\framework\" >nul
    echo   AP Bridge: included
) else if exist "%SRC%ap_bridge.exe" (
    copy /Y "%SRC%ap_bridge.exe" "%REL%\files\framework\" >nul
    echo   AP Bridge: included (from root)
) else (
    echo   WARNING: ap_bridge.exe not found! AP connectivity disabled.
)

:: ============================================
:: files\data: TXT files + skill icons
:: ============================================
echo Copying game data to files\data\...
xcopy "%SRC%data\global\excel\*" "%REL%\files\data\global\excel\" /Y /Q >nul 2>nul
xcopy "%SRC%data\global\ui\SPELLS\*" "%REL%\files\data\global\ui\SPELLS\" /Y /Q >nul 2>nul

:: ============================================
:: files\Archipelago: config, icon map, source, research
:: ============================================
echo Copying Archipelago data to files\Archipelago\...
copy /Y "%SRC%Archipelago\d2arch.ini" "%REL%\files\Archipelago\" >nul
copy /Y "%SRC%Archipelago\skill_icon_map.dat" "%REL%\files\Archipelago\" >nul
:: .apworld for Archipelago server
if exist "%SRC%diablo2_archipelago.apworld" (
    copy /Y "%SRC%diablo2_archipelago.apworld" "%REL%\files\Archipelago\" >nul
    echo   .apworld: included
)
:: Source code and research docs are NOT shipped — internal development only

:: ============================================
:: MpqFixer (fixes 1.14b MPQs for 1.10f)
:: ============================================
echo Copying MpqFixer...
mkdir "%REL%\files\MpqFixer" 2>nul
:: Only SFMPQ.dll needed — no WinMPQ, no MSCOMCTL, no admin rights
if exist "%SRC%MpqFixer\SFMPQ.dll" copy /Y "%SRC%MpqFixer\SFMPQ.dll" "%REL%\files\MpqFixer\" >nul

:: ============================================
:: ALSO copy Play Archipelago.exe directly to root
:: (backup in case installer CopyFiles misses it)
:: ============================================
echo Copying Play Archipelago.exe to root...
copy /Y "%SRC%Archipelago\src\D2ArchLauncher.exe" "%REL%\Play Archipelago.exe" >nul

:: ============================================
:: Verify
:: ============================================
echo.
echo Verifying release package...
echo.

set OK=1
echo   Checking root...
if not exist "%REL%\D2ArchSetup.exe" (echo     MISSING: D2ArchSetup.exe & set OK=0)
echo   Checking files\game\...
if not exist "%REL%\files\game\Game.exe" (echo     MISSING: files\game\Game.exe & set OK=0)
if not exist "%REL%\files\game\D2Client.dll" (echo     MISSING: files\game\D2Client.dll & set OK=0)
if not exist "%REL%\files\game\Storm.dll" (echo     MISSING: files\game\Storm.dll & set OK=0)
echo   Checking files\patch\...
if not exist "%REL%\files\patch\D2Archipelago.dll" (echo     MISSING: files\patch\D2Archipelago.dll & set OK=0)
if not exist "%REL%\files\patch\D2Common.dll" (echo     MISSING: files\patch\D2Common.dll & set OK=0)
if not exist "%REL%\files\patch\D2Game.dll" (echo     MISSING: files\patch\D2Game.dll & set OK=0)
if not exist "%REL%\files\patch\Fog.dll" (echo     MISSING: files\patch\Fog.dll & set OK=0)
if not exist "%REL%\files\patch\D2Debugger.dll" (echo     MISSING: files\patch\D2Debugger.dll & set OK=0)
echo   Checking files\framework\...
if not exist "%REL%\files\framework\D2.Detours.dll" (echo     MISSING: files\framework\D2.Detours.dll & set OK=0)
if not exist "%REL%\files\framework\Play Archipelago.exe" (echo     MISSING: files\framework\Play Archipelago.exe & set OK=0)
if not exist "%REL%\files\framework\ddraw.dll" (echo     MISSING: files\framework\ddraw.dll & set OK=0)
if not exist "%REL%\files\framework\ddraw.ini" (echo     MISSING: files\framework\ddraw.ini & set OK=0)
if not exist "%REL%\files\framework\ap_bridge.exe" (echo     WARNING: files\framework\ap_bridge.exe missing - AP disabled)
if not exist "%REL%\files\framework\D2.DetoursLauncher.exe" (echo     MISSING: files\framework\D2.DetoursLauncher.exe & set OK=0)
echo   Checking files\Archipelago\...
if not exist "%REL%\files\Archipelago\diablo2_archipelago.apworld" (echo     WARNING: .apworld missing - AP world not included)
echo   Checking files\data\...
if not exist "%REL%\files\data\global\excel\Skills.txt" (echo     MISSING: files\data\global\excel\Skills.txt & set OK=0)
if not exist "%REL%\files\data\global\ui\SPELLS\AmSkillicon.DC6" (echo     MISSING: files\data\global\ui\SPELLS\AmSkillicon.DC6 & set OK=0)
echo   Checking files\Archipelago\...
if not exist "%REL%\files\Archipelago\d2arch.ini" (echo     MISSING: files\Archipelago\d2arch.ini & set OK=0)
if not exist "%REL%\files\Archipelago\skill_icon_map.dat" (echo     MISSING: files\Archipelago\skill_icon_map.dat & set OK=0)

if "%OK%"=="1" (
    echo   All checks passed!
) else (
    echo.
    echo   WARNING: Some files missing!
)

echo.
echo ============================================
echo   Release package ready!
echo ============================================
echo.
echo   Location: %REL%
echo.
echo   What user sees in root:
echo     D2ArchSetup.exe  (the only visible file)
echo     files\           (hidden install data)
echo.
echo   To distribute: zip "Release\Diablo II Archipelago"
echo ============================================
echo.
pause
