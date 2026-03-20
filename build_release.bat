@echo off
setlocal enabledelayedexpansion

set VERSION=0.1.0-alpha
set RELEASE_NAME=DiabloII-Archipelago-v%VERSION%
set BUILD_DIR=build\%RELEASE_NAME%
set ZIP_FILE=build\%RELEASE_NAME%.zip

echo ==========================================
echo  Diablo II Archipelago - Build Release
echo  Version: %VERSION%
echo ==========================================
echo.

:: Clean previous build
if exist build rmdir /s /q build
mkdir "%BUILD_DIR%"

echo [1/5] Copying launcher files...

:: Core launcher
xcopy /s /i /q "launcher\__init__.py" "%BUILD_DIR%\launcher\"
xcopy /s /i /q "launcher\main.py" "%BUILD_DIR%\launcher\"
xcopy /s /i /q "launcher\core\*.py" "%BUILD_DIR%\launcher\core\"
xcopy /s /i /q "launcher\gui\*.py" "%BUILD_DIR%\launcher\gui\"

:: Data files
xcopy /s /i /q "launcher\data\vanilla_txt\*" "%BUILD_DIR%\launcher\data\vanilla_txt\"
xcopy /s /i /q "launcher\data\icons_dc6\*" "%BUILD_DIR%\launcher\data\icons_dc6\"
copy /y "launcher\data\units_palette.dat" "%BUILD_DIR%\launcher\data\" >nul

:: DLL
xcopy /s /i /q "launcher\x64\*" "%BUILD_DIR%\launcher\x64\"

echo [2/5] Copying support files...

:: Requirements and launcher
copy /y "requirements.txt" "%BUILD_DIR%\" >nul

:: Create Launch.bat
(
echo @echo off
echo cd /d "%%~dp0"
echo.
echo :: Check Python
echo python --version ^>nul 2^>^&1
echo if errorlevel 1 ^(
echo     echo ERROR: Python is not installed or not in PATH.
echo     echo Please install Python 3.10+ from https://python.org
echo     echo Make sure to check "Add Python to PATH" during installation.
echo     pause
echo     exit /b 1
echo ^)
echo.
echo :: Install dependencies on first run
echo if not exist ".deps_installed" ^(
echo     echo Installing dependencies...
echo     pip install -r requirements.txt
echo     echo. ^> .deps_installed
echo ^)
echo.
echo :: Launch
echo python -m launcher.main
echo if errorlevel 1 pause
) > "%BUILD_DIR%\Launch.bat"

echo [3/5] Creating README...

(
echo # Diablo II Archipelago Randomizer v%VERSION%
echo.
echo ## Setup
echo.
echo 1. Install Python 3.10 or newer from https://python.org
echo    - IMPORTANT: Check "Add Python to PATH" during installation
echo 2. Extract this folder anywhere on your computer
echo 3. Copy your Diablo II Lord of Destruction installation files into this folder:
echo    - Game.exe, Diablo II.exe
echo    - All .mpq files ^(d2data.mpq, d2exp.mpq, etc.^)
echo    - All .dll files ^(binkw32.dll, ijl11.dll, SmackW32.dll^)
echo    - D2.LNG
echo 4. Double-click Launch.bat
echo 5. In the launcher, verify the game path points to this folder
echo 6. Create a character and click Play!
echo.
echo ## Features
echo.
echo - Randomized skill trees: 30 random skills from all 7 classes
echo - Skill tree editor: swap skills between your equipped and pool
echo - Area kill tracking: clear areas by killing monsters
echo - Real-time area detection: shows your current location and monsters
echo - Character profiles: multiple characters with isolated saves
echo - Map reset: regenerate maps without losing progress
echo.
echo ## Requirements
echo.
echo - Windows 10/11
echo - Python 3.10+
echo - Diablo II: Lord of Destruction ^(version 1.14b^)
echo.
echo ## Troubleshooting
echo.
echo - If the launcher doesn't start, make sure Python is in your PATH
echo - If the game doesn't launch, verify Game.exe is in the same folder
echo - The game runs in -direct -txt mode, no MPQ modifications needed
echo.
) > "%BUILD_DIR%\README.txt"

echo [4/5] Cleaning up...

:: Remove __pycache__ directories
for /d /r "%BUILD_DIR%" %%d in (__pycache__) do (
    if exist "%%d" rmdir /s /q "%%d"
)

echo [5/5] Creating ZIP archive...

:: Use PowerShell to create ZIP
powershell -Command "Compress-Archive -Path '%BUILD_DIR%\*' -DestinationPath '%ZIP_FILE%' -Force"

if exist "%ZIP_FILE%" (
    echo.
    echo ==========================================
    echo  Build complete!
    echo  Output: %ZIP_FILE%
    for %%A in ("%ZIP_FILE%") do echo  Size: %%~zA bytes
    echo ==========================================
) else (
    echo.
    echo ERROR: Failed to create ZIP file.
)

echo.
pause
