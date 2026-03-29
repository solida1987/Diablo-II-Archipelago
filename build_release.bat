@echo off
title Diablo II Archipelago beta-1.5.0 - Build Release Package
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
pushd "%SRC%Archipelago\src"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
rc /nologo launcher.rc >nul 2>&1
cl /nologo /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS launcher.c launcher.res /Fe:"Play Archipelago.exe" /link user32.lib gdi32.lib kernel32.lib advapi32.lib shell32.lib comdlg32.lib comctl32.lib >nul 2>&1
if exist "Play Archipelago.exe" (
    copy /Y "Play Archipelago.exe" "%SRC%Play Archipelago.exe" >nul
    echo   Launcher compiled.
) else (
    echo   WARNING: Launcher compile failed!
)
popd
echo.

:: ============================================
:: Step 0b: Rebuild AP Bridge
:: ============================================
echo Rebuilding AP Bridge...
if exist "%SRC%Archipelago\src\ap_bridge.py" (
    pushd "%SRC%Archipelago\src"
    python -m PyInstaller --onefile --name ap_bridge ap_bridge.py >nul 2>&1
    if exist "dist\ap_bridge.exe" (
        copy /Y "dist\ap_bridge.exe" "%SRC%ap_bridge.exe" >nul
        echo   AP Bridge rebuilt.
    ) else (
        echo   WARNING: AP Bridge build failed.
    )
    popd
)
echo.

:: ============================================
:: Step 0c: Rebuild Monster Shuffle (C version — no PyInstaller/AV issues)
:: ============================================
echo Rebuilding Monster Shuffle (C)...
if exist "%SRC%Archipelago\src\monster_shuffle.c" (
    pushd "%SRC%Archipelago\src"
    call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86 >nul 2>&1
    cl /nologo /MT /O2 /W3 /D_CRT_SECURE_NO_WARNINGS monster_shuffle.c /Fe:monster_shuffle.exe /link kernel32.lib >nul 2>&1
    if exist "monster_shuffle.exe" (
        copy /Y "monster_shuffle.exe" "%SRC%Archipelago\monster_shuffle.exe" >nul
        echo   Monster Shuffle C version rebuilt.
    ) else (
        echo   WARNING: Monster Shuffle C build failed.
    )
    popd
)
echo.

:: ============================================
:: Step 0d: Rebuild apworld
:: ============================================
echo Rebuilding apworld...
if exist "%SRC%apworld\diablo2_archipelago" (
    pushd "%SRC%apworld\diablo2_archipelago"
    powershell -NoProfile -Command "Compress-Archive -Path * -DestinationPath ..\diablo2_archipelago.zip -Force" >nul 2>&1
    popd
    if exist "%SRC%apworld\diablo2_archipelago.zip" (
        move /Y "%SRC%apworld\diablo2_archipelago.zip" "%SRC%apworld\diablo2_archipelago.apworld" >nul
        echo   apworld rebuilt.
    )
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

:: Monster Shuffle (prefer C-compiled version over PyInstaller)
mkdir "%REL%\files\Archipelago" >nul 2>nul
if exist "%SRC%Archipelago\monster_shuffle.exe" (
    copy /Y "%SRC%Archipelago\monster_shuffle.exe" "%REL%\files\Archipelago\" >nul
    echo   Monster Shuffle: included (C version)
) else if exist "%SRC%Archipelago\src\dist\monster_shuffle.exe" (
    copy /Y "%SRC%Archipelago\src\dist\monster_shuffle.exe" "%REL%\files\Archipelago\" >nul
    echo   Monster Shuffle: included (PyInstaller fallback)
)
:: Also copy .py as fallback
mkdir "%REL%\files\Archipelago\src" >nul 2>nul
if exist "%SRC%Archipelago\src\monster_shuffle.py" (
    copy /Y "%SRC%Archipelago\src\monster_shuffle.py" "%REL%\files\Archipelago\src\" >nul
    echo   Monster Shuffle: .py fallback included
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

:: d2tbl tool (for custom name generation)
if exist "%SRC%d2tbl\d2tbl.exe" (
    mkdir "%REL%\files\d2tbl" 2>nul
    copy /Y "%SRC%d2tbl\d2tbl.exe" "%REL%\files\d2tbl\" >nul
    copy /Y "%SRC%d2tbl\patchstring.tbl" "%REL%\files\d2tbl\" >nul
    copy /Y "%SRC%d2tbl\cow_names.txt" "%REL%\files\d2tbl\" >nul
    copy /Y "%SRC%d2tbl\import_cows.bat" "%REL%\files\d2tbl\" >nul
    echo   d2tbl tool: included
)

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
if not exist "%REL%\files\framework\ap_bridge.exe" (echo     WARNING: files\framework\ap_bridge.exe missing - AP disabled)
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
