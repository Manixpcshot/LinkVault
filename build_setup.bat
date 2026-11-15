@echo off
setlocal
title LinkVault - Build Setup
cd /d "%~dp0"

echo.
echo  ==========================================
echo    LinkVault - building installer (Setup)
echo  ==========================================
echo.

rem ---------- 1) build the exe + DLLs into dist ----------
set "LV_NOPAUSE=1"
call "%~dp0build.bat"
if errorlevel 1 exit /b 1
if not exist "%~dp0dist\LinkVault.exe" goto :fail

rem ---------- 2) find Inno Setup compiler ----------
:find_iscc
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not defined ISCC (
    where iscc >nul 2>&1 && set "ISCC=iscc"
)
if defined ISCC goto :have_iscc

echo  [X] Inno Setup 6 is not installed.
if defined TRIED_INNO goto :no_inno
where winget >nul 2>&1
if errorlevel 1 goto :no_inno
choice /c YN /m "  Install Inno Setup automatically with winget (~5 MB)"
if errorlevel 2 goto :no_inno
set "TRIED_INNO=1"
winget install --id JRSoftware.InnoSetup -e --accept-source-agreements --accept-package-agreements
goto :find_iscc

:have_iscc
echo.
echo  [OK] Inno Setup: %ISCC%
echo  Packing installer...
"%ISCC%" /Q "%~dp0installer\LinkVault.iss"
if errorlevel 1 goto :fail

echo.
echo  ==========================================
echo    DONE!  Installer is in the "Setup" folder
echo  ==========================================
start "" explorer "%~dp0Setup"
pause
exit /b 0

:no_inno
echo.
echo  Download Inno Setup 6 from https://jrsoftware.org/isdl.php , install it,
echo  then run this file again.
start "" https://jrsoftware.org/isdl.php
pause
exit /b 1

:fail
echo.
echo  [X] Building the installer failed. Send the error above so it can be fixed.
pause
exit /b 1
