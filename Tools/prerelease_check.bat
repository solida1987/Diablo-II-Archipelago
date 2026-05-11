@echo off
setlocal enabledelayedexpansion
REM Pre-release verification - run before create_release.bat
REM Exits 0 on success, 1 on any failure.

set REPO_ROOT=%~dp0..
set PYEXE=
for /d %%D in ("%REPO_ROOT%\Diablo II Archipelago Beta *") do set PYEXE=%%D\python_embed\python.exe
if not defined PYEXE set PYEXE=%REPO_ROOT%\Game\python_embed\python.exe

echo Checking release readiness...

REM --------------------------------------------------------------
REM EULA check: scan game_package.zip for Blizzard original files.
REM Only runs if the ZIP already exists (i.e. pack step has run).
REM --------------------------------------------------------------
if exist "%REPO_ROOT%\game_package.zip" (
    if not exist "%PYEXE%" (
        echo WARNING: Python not found, skipping EULA check.
    ) else (
        "%PYEXE%" -c "import zipfile, sys; blacklist={'D2.LNG','SmackW32.dll','binkw32.dll','d2exp.mpq','d2music.mpq','d2speech.mpq','d2video.mpq','d2xmusic.mpq','d2xtalk.mpq','d2xvideo.mpq','ijl11.dll','d2char.mpq','d2data.mpq','d2sfx.mpq','Game.exe'}; z=zipfile.ZipFile(r'%REPO_ROOT%\game_package.zip'); hits=[n for n in z.namelist() for b in blacklist if n.split('/')[-1].lower()==b.lower()]; sys.exit(1 if hits else 0)"
        if errorlevel 1 (
            echo EULA VIOLATION: Blizzard original file found in game_package.zip
            exit /b 1
        )
        echo [OK] EULA check passed (game_package.zip contains no Blizzard originals^)
    )
) else (
    echo [SKIP] game_package.zip not yet built - EULA check deferred
)

REM --------------------------------------------------------------
REM AI reference check in recent commits.
REM --------------------------------------------------------------
pushd "%REPO_ROOT%" >nul
git log --since="2026-04-13" --pretty=format:%%B 2>nul | findstr /i "claude anthropic co-authored" >nul
if not errorlevel 1 (
    echo AI REFERENCE found in recent commits - investigate before release
    popd >nul
    exit /b 1
)
popd >nul
echo [OK] No AI references in recent commits

REM --------------------------------------------------------------
REM Version-file sanity: launcher_version.txt must have:
REM   line 1 = non-empty version string
REM   line 2 = 64-char lowercase hex SHA-256 (or comment line starting with #)
REM If launcher_package.zip exists, also verify its SHA matches line 2.
REM --------------------------------------------------------------
if not exist "%REPO_ROOT%\launcher_version.txt" (
    echo ERROR: launcher_version.txt missing at repo root
    exit /b 1
)

REM Delegate to Python helper for robust parsing + optional SHA verification.
if not exist "%PYEXE%" (
    echo WARNING: Python not found, skipping full launcher_version.txt validation
    exit /b 0
)
"%PYEXE%" "%~dp0_validate_launcher_version.py" "%REPO_ROOT%"
if errorlevel 1 exit /b 1

echo [OK] Release pre-flight passed
endlocal
exit /b 0
