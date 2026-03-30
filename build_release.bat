@echo off
title Diablo II Archipelago beta-1.5.1 - Build Release Package
echo ============================================
echo   Building Release Package (D2MOO + AP)
echo ============================================
echo.

set SRC=%~dp0
set REL=%SRC%Release\Diablo II Archipelago

:: ============================================
:: Step 0: Compile D2Archipelago.dll
:: ============================================
echo Compiling D2Archipelago.dll...
call "%SRC%build_d2arch.bat"
if not exist "%SRC%patch\D2Archipelago.dll" (
    echo ERROR: DLL compile failed!
    exit /b 1
)
echo.

:: ============================================
:: Step 0a: Compile Play Archipelago.exe (launcher)
:: ============================================
echo Compiling Play Archipelago.exe...
call "%SRC%Archipelago\src\build_launcher.bat"
if exist "%SRC%Play Archipelago.exe" (
    echo   Launcher compiled.
) else (
    echo   WARNING: Launcher compile failed!
)
echo.

:: ============================================
:: Step 0b: AP Bridge — use pre-built standalone folder from ap_bridge_dist\
:: To rebuild: cd Archipelago\src && python -m nuitka --standalone --output-dir=bridge_build ap_bridge.py
:: Then copy bridge_build\ap_bridge.dist\* to %SRC%ap_bridge_dist\
:: ============================================
echo Checking AP Bridge...
if exist "%SRC%ap_bridge_dist\ap_bridge.exe" (
    echo   AP Bridge: using pre-built standalone folder.
) else if exist "%SRC%ap_bridge.exe" (
    echo   AP Bridge: using single exe ^(legacy, may trigger AV^).
    mkdir "%SRC%ap_bridge_dist" >nul 2>&1
    copy /Y "%SRC%ap_bridge.exe" "%SRC%ap_bridge_dist\ap_bridge.exe" >nul
) else (
    echo   WARNING: ap_bridge not found! AP connectivity disabled.
    echo   Build it: cd Archipelago\src ^&^& python -m nuitka --standalone --output-dir=bridge_build ap_bridge.py
)
echo.

:: ============================================
:: Step 0c: Rebuild Monster Shuffle (C version — no PyInstaller/AV issues)
:: ============================================
echo Rebuilding Monster Shuffle (C)...
call "%SRC%Archipelago\src\build_monster_shuffle.bat"
if exist "%SRC%Archipelago\monster_shuffle.exe" (
    echo   Monster Shuffle rebuilt.
) else (
    echo   WARNING: Monster Shuffle build failed.
)
echo.

:: ============================================
:: Step 0c2: Rebuild D2ArchSetup.exe (installer)
:: ============================================
echo Rebuilding D2ArchSetup.exe...
call "%SRC%Archipelago\src\build_installer.bat"
if exist "%SRC%Archipelago\src\D2ArchSetup.exe" (
    echo   Installer rebuilt.
) else (
    echo   WARNING: Installer compile failed!
)
echo.

:: ============================================
:: Step 0d: Rebuild apworld
:: ============================================
echo Rebuilding apworld...
if exist "%SRC%apworld\diablo2_archipelago" (
    pushd "%SRC%apworld"
    powershell -NoProfile -Command "Compress-Archive -Path 'diablo2_archipelago' -DestinationPath 'diablo2_archipelago.zip' -Force" >nul 2>&1
    if exist "diablo2_archipelago.zip" (
        del "diablo2_archipelago.apworld" 2>nul
        move /Y "diablo2_archipelago.zip" "diablo2_archipelago.apworld" >nul
        echo   apworld rebuilt.
    )
    popd
)
echo.

:: Clean previous release
if exist "%SRC%Release" (
    echo Cleaning previous release...
    rmdir /S /Q "%SRC%Release"
)

:: ============================================
:: Create folder structure
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
mkdir "%REL%\files\data\global\ui\panel"
mkdir "%REL%\files\Archipelago"
:: NOTE: src/, research/, apworld/ source, .claude/, memory/ are NOT included — internal development only

:: ============================================
:: ROOT: Only the installer EXE
:: ============================================
echo Copying installer to root...
if exist "%SRC%Archipelago\src\D2ArchSetup.exe" (
    copy /Y "%SRC%Archipelago\src\D2ArchSetup.exe" "%REL%\" >nul
) else (
    echo   ERROR: D2ArchSetup.exe not found! Build it first.
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
if exist "%SRC%Play Archipelago.exe" (
    copy /Y "%SRC%Play Archipelago.exe" "%REL%\files\framework\Play Archipelago.exe" >nul
) else (
    copy /Y "%SRC%Archipelago\src\D2ArchLauncher.exe" "%REL%\files\framework\Play Archipelago.exe" >nul
)
copy /Y "%SRC%ddraw.dll" "%REL%\files\framework\" >nul
copy /Y "%SRC%ddraw.ini" "%REL%\files\framework\" >nul
:: AP Bridge (standalone folder — low AV flags)
if exist "%SRC%ap_bridge_dist\ap_bridge.exe" (
    mkdir "%REL%\files\framework\ap_bridge" >nul 2>&1
    xcopy "%SRC%ap_bridge_dist" "%REL%\files\framework\ap_bridge\" /Y /Q /S /E >nul
    echo   AP Bridge: included - standalone folder
) else if exist "%SRC%ap_bridge.exe" (
    copy /Y "%SRC%ap_bridge.exe" "%REL%\files\framework\" >nul
    echo   AP Bridge: included - single exe ^(legacy^)
) else (
    echo   WARNING: ap_bridge not found! AP connectivity disabled.
)

:: Monster Shuffle (C-compiled only — no PyInstaller, no AV flags)
mkdir "%REL%\files\Archipelago" >nul 2>nul
if exist "%SRC%Archipelago\monster_shuffle.exe" (
    copy /Y "%SRC%Archipelago\monster_shuffle.exe" "%REL%\files\Archipelago\" >nul
    echo   Monster Shuffle: included - C native
) else (
    echo   WARNING: monster_shuffle.exe not found!
)
:: Also copy .py as fallback for systems without the C version
mkdir "%REL%\files\Archipelago\src" >nul 2>nul
if exist "%SRC%Archipelago\src\monster_shuffle.py" (
    copy /Y "%SRC%Archipelago\src\monster_shuffle.py" "%REL%\files\Archipelago\src\" >nul
)

:: ============================================
:: files\data: TXT files + skill icons + panel graphics (inventory/stash/cube)
:: ============================================
echo Copying game data to files\data\...
xcopy "%SRC%data\global\excel\*.txt" "%REL%\files\data\global\excel\" /Y /Q >nul 2>nul
:: Do NOT copy .bin files — game regenerates them from .txt with -direct -txt
xcopy "%SRC%data\global\ui\SPELLS\*" "%REL%\files\data\global\ui\SPELLS\" /Y /Q >nul 2>nul
:: Panel graphics: expanded inventory, stash, and cube panels
xcopy "%SRC%data\global\ui\panel\*" "%REL%\files\data\global\ui\panel\" /Y /Q >nul 2>nul
:: Regenerate patchstring.tbl using d2tbl.exe (imports custom names into original TBL with correct CRC)
echo Regenerating patchstring.tbl...
if exist "%SRC%d2tbl\d2tbl.exe" (
    pushd "%SRC%d2tbl"
    .\d2tbl.exe -import cow_names.txt -source patchstring.tbl -ansi -always-insert >nul 2>&1
    if exist "d2tbl_ouput.tbl" (
        copy /Y "d2tbl_ouput.tbl" "%SRC%data\local\LNG\ENG\patchstring.tbl" >nul
        echo   patchstring.tbl: regenerated with custom names
    )
    popd
)
:: String table: custom monster/item names (patchstring.tbl)
if exist "%SRC%data\local\LNG\ENG\patchstring.tbl" (
    mkdir "%REL%\files\data\local\LNG\ENG" 2>nul
    copy /Y "%SRC%data\local\LNG\ENG\patchstring.tbl" "%REL%\files\data\local\LNG\ENG\" >nul
    echo   patchstring.tbl: included (custom names)
    :: Also copy to root for direct testing
    mkdir "%REL%\data\local\LNG\ENG" 2>nul
    copy /Y "%SRC%data\local\LNG\ENG\patchstring.tbl" "%REL%\data\local\LNG\ENG\" >nul
)

:: DS1 tile files: custom SuperUnique placements (Treasure Cows)
:: Copy to BOTH files\data\ (for installer) AND root data\ (for direct testing)
echo Copying DS1 tile files...
for %%D in (Act1 Act2 Act3 Act4) do (
    if exist "%SRC%data\global\tiles\%%D" (
        xcopy "%SRC%data\global\tiles\%%D" "%REL%\files\data\global\tiles\%%D\" /Y /Q /S /E >nul 2>nul
        xcopy "%SRC%data\global\tiles\%%D" "%REL%\data\global\tiles\%%D\" /Y /Q /S /E >nul 2>nul
    )
)
if exist "%SRC%data\global\tiles\expansion" (
    xcopy "%SRC%data\global\tiles\expansion" "%REL%\files\data\global\tiles\expansion\" /Y /Q /S /E >nul 2>nul
    xcopy "%SRC%data\global\tiles\expansion" "%REL%\data\global\tiles\expansion\" /Y /Q /S /E >nul 2>nul
)
echo   DS1 tiles: included (files + root)

:: d2tbl tool: NOT shipped to users (UPX packed = AV risk)
:: Only used during build to regenerate patchstring.tbl
:: The generated patchstring.tbl IS shipped (see above)

:: ============================================
:: files\Archipelago: config, icon map, apworld
:: ============================================
echo Copying Archipelago data to files\Archipelago\...
copy /Y "%SRC%Archipelago\d2arch.ini" "%REL%\files\Archipelago\" >nul
copy /Y "%SRC%Archipelago\skill_icon_map.dat" "%REL%\files\Archipelago\" >nul
:: .apworld for Archipelago server
if exist "%SRC%apworld\diablo2_archipelago.apworld" (
    copy /Y "%SRC%apworld\diablo2_archipelago.apworld" "%REL%\files\Archipelago\" >nul
    echo   .apworld: included (from apworld/)
) else if exist "%SRC%diablo2_archipelago.apworld" (
    copy /Y "%SRC%diablo2_archipelago.apworld" "%REL%\files\Archipelago\" >nul
    echo   .apworld: included (from root)
) else (
    echo   WARNING: .apworld not found!
)
:: NOTE: Source code (src/), research docs, apworld/ python source, .claude/ memory are NEVER shipped

:: ============================================
:: MpqFixer (fixes 1.14b MPQs for 1.10f)
:: ============================================
echo Copying MpqFixer...
mkdir "%REL%\files\MpqFixer" 2>nul
if exist "%SRC%MpqFixer\SFMPQ.dll" copy /Y "%SRC%MpqFixer\SFMPQ.dll" "%REL%\files\MpqFixer\" >nul

:: ============================================
:: Play Archipelago.exe to root (backup)
:: ============================================
:: ============================================
:: LICENSE
:: ============================================
echo Copying LICENSE...
if exist "%SRC%LICENSE" copy /Y "%SRC%LICENSE" "%REL%\" >nul

echo Copying Play Archipelago.exe to root...
if exist "%SRC%Play Archipelago.exe" (
    copy /Y "%SRC%Play Archipelago.exe" "%REL%\Play Archipelago.exe" >nul
) else (
    copy /Y "%SRC%Archipelago\src\D2ArchLauncher.exe" "%REL%\Play Archipelago.exe" >nul
)

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
if not exist "%REL%\files\framework\ap_bridge\ap_bridge.exe" (
    if not exist "%REL%\files\framework\ap_bridge.exe" (echo     WARNING: ap_bridge missing - AP disabled)
)
if not exist "%REL%\files\framework\D2.DetoursLauncher.exe" (echo     MISSING: files\framework\D2.DetoursLauncher.exe & set OK=0)
echo   Checking files\Archipelago\...
if not exist "%REL%\files\Archipelago\diablo2_archipelago.apworld" (echo     WARNING: .apworld missing - AP world not included)
echo   Checking files\data\...
if not exist "%REL%\files\data\global\excel\Skills.txt" (echo     MISSING: files\data\global\excel\Skills.txt & set OK=0)
if not exist "%REL%\files\data\global\excel\ItemTypes.txt" (echo     MISSING: files\data\global\excel\ItemTypes.txt & set OK=0)
if not exist "%REL%\files\data\global\excel\inventory.txt" (echo     MISSING: files\data\global\excel\inventory.txt & set OK=0)
if not exist "%REL%\files\data\global\excel\weapons.txt" (echo     MISSING: files\data\global\excel\weapons.txt & set OK=0)
if not exist "%REL%\files\data\global\excel\charstats.txt" (echo     MISSING: files\data\global\excel\charstats.txt & set OK=0)
if not exist "%REL%\files\data\global\excel\Armor.txt" (echo     MISSING: files\data\global\excel\Armor.txt & set OK=0)
if not exist "%REL%\files\data\global\ui\SPELLS\AmSkillicon.DC6" (echo     MISSING: files\data\global\ui\SPELLS\AmSkillicon.DC6 & set OK=0)
if not exist "%REL%\files\data\global\ui\panel\invchar6.dc6" (echo     MISSING: files\data\global\ui\panel\invchar6.dc6 & set OK=0)
if not exist "%REL%\files\data\global\ui\panel\tradestash.dc6" (echo     MISSING: files\data\global\ui\panel\tradestash.dc6 & set OK=0)
if not exist "%REL%\files\data\global\ui\panel\supertransmogrifier.dc6" (echo     MISSING: files\data\global\ui\panel\supertransmogrifier.dc6 & set OK=0)
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
