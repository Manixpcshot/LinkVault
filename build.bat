@echo off
setlocal EnableDelayedExpansion
title LinkVault - Build EXE
cd /d "%~dp0"

rem ============================================================
rem  LinkVault build script (Qt 6 + MinGW)
rem  Manual override (optional), e.g.:
rem  set "QT_DIR=C:\Qt\6.8.3\mingw_64"
rem ============================================================
set "QT_DIR="
set "QT_VER=6.8.3"

echo.
echo  ==========================================
echo    LinkVault - building release EXE
echo  ==========================================
echo.

:find
set "QT_ROOT="
if defined QT_DIR (
    for %%A in ("%QT_DIR%\..\..") do set "QT_ROOT=%%~fA"
    goto :check_qt
)
rem ---------- search common Qt locations ----------
for %%R in ("C:\Qt" "D:\Qt" "E:\Qt" "F:\Qt" "%USERPROFILE%\Qt" "%LOCALAPPDATA%\Qt" "C:\Program Files\Qt") do (
    if exist "%%~R" (
        for /d %%D in ("%%~R\6.*") do (
            for /d %%M in ("%%D\mingw*_64") do (
                if exist "%%M\bin\windeployqt.exe" (
                    set "QT_DIR=%%M"
                    set "QT_ROOT=%%~R"
                )
            )
        )
    )
)

:check_qt
if not defined QT_DIR goto :no_qt
if not exist "%QT_DIR%\bin\windeployqt.exe" goto :no_qt
echo  [OK] Qt:     %QT_DIR%

rem ---------- MinGW compiler ----------
set "MINGW_DIR="
for /d %%T in ("%QT_ROOT%\Tools\mingw*_64") do set "MINGW_DIR=%%T"
if not defined MINGW_DIR (
    where g++ >nul 2>&1 || goto :no_mingw
    echo  [OK] MinGW:  from PATH
) else (
    echo  [OK] MinGW:  %MINGW_DIR%
)

set "PATH=%QT_DIR%\bin;%MINGW_DIR%\bin;%QT_ROOT%\Tools\CMake_64\bin;%QT_ROOT%\Tools\Ninja;%PATH%"

where cmake >nul 2>&1
if errorlevel 1 goto :no_cmake
echo  [OK] CMake found

set "GEN=MinGW Makefiles"
where ninja >nul 2>&1
if not errorlevel 1 set "GEN=Ninja"
echo  [OK] Generator: %GEN%
echo.

rem ---------- stage sources in a path without spaces / parentheses ----------
rem  (MinGW windres and batch blocks break on paths like "LinkVault (1)")
set "SRC=%~dp0"
set "SRC=%SRC:~0,-1%"
set "WORK=%PUBLIC%\LinkVaultBuild"
if not defined PUBLIC set "WORK=C:\LinkVaultBuild"
echo  [0/3] Copying sources to %WORK% ...
if exist "%WORK%" rmdir /s /q "%WORK%"
robocopy "%SRC%" "%WORK%" /E /XD build dist .git /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 goto :fail

echo  [1/3] Configuring...
cmake -S "%WORK%" -B "%WORK%\build" -G "%GEN%" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 goto :fail

echo.
echo  [2/3] Compiling...
cmake --build "%WORK%\build" --config Release --parallel
if errorlevel 1 goto :fail
if not exist "%WORK%\build\LinkVault.exe" goto :fail

echo.
echo  [3/3] Packaging DLLs...
if exist "%SRC%\dist" rmdir /s /q "%SRC%\dist"
mkdir "%SRC%\dist"
copy /y "%WORK%\build\LinkVault.exe" "%SRC%\dist\" >nul
windeployqt --release --no-translations --compiler-runtime "%SRC%\dist\LinkVault.exe"
if errorlevel 1 goto :fail

echo.
echo  ==========================================
echo    DONE!  Output: "%SRC%\dist\LinkVault.exe"
echo    Copy the whole "dist" folder anywhere.
echo  ==========================================
rem build_setup.bat sets LV_NOPAUSE so it can continue straight to the installer
if defined LV_NOPAUSE exit /b 0
start "" explorer "%SRC%\dist"
pause
exit /b 0

rem ============================================================
:no_qt
echo  [X] Qt 6 (MinGW) is not installed on this PC.
echo.
if defined TRIED_INSTALL goto :install_failed
choice /c YN /m "  Install Qt %QT_VER% + MinGW + CMake automatically into C:\Qt (~1.5 GB, no account needed)"
if errorlevel 2 goto :manual_help
goto :auto_install

:no_mingw
echo  [X] MinGW compiler not found in %QT_ROOT%\Tools
if defined TRIED_INSTALL goto :install_failed
choice /c YN /m "  Install MinGW + CMake + Ninja automatically"
if errorlevel 2 goto :manual_help
goto :auto_install

:no_cmake
echo  [X] CMake not found.
if defined TRIED_INSTALL goto :install_failed
choice /c YN /m "  Install CMake + Ninja automatically"
if errorlevel 2 goto :manual_help
goto :auto_install

:auto_install
set "TRIED_INSTALL=1"
set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY ( where python >nul 2>&1 && set "PY=python" )
if not defined PY (
    echo.
    echo  [X] Python is needed for auto-install and was not found.
    echo      Install Python from https://www.python.org/downloads/
    echo      ^(tick "Add python.exe to PATH"^) then run this file again.
    start "" https://www.python.org/downloads/
    goto :end_err
)
echo.
echo  Installing aqtinstall...
%PY% -m pip install --upgrade aqtinstall
if errorlevel 1 goto :install_failed
echo.
echo  Downloading Qt %QT_VER% (MinGW 64-bit)... this takes a while
%PY% -m aqt install-qt windows desktop %QT_VER% win64_mingw --outputdir C:\Qt
if errorlevel 1 goto :install_failed
echo.
echo  Downloading MinGW 13.1 compiler...
%PY% -m aqt install-tool windows desktop tools_mingw1310 qt.tools.win64_mingw1310 --outputdir C:\Qt
if errorlevel 1 goto :install_failed
echo.
echo  Downloading CMake + Ninja...
%PY% -m aqt install-tool windows desktop tools_cmake qt.tools.cmake --outputdir C:\Qt
%PY% -m aqt install-tool windows desktop tools_ninja qt.tools.ninja --outputdir C:\Qt
echo.
echo  [OK] Install finished. Building...
echo.
set "QT_DIR="
goto :find

:install_failed
echo.
echo  [X] Automatic install failed (check internet / VPN and try again).
:manual_help
echo.
echo  Manual install:
echo    1. Download "Qt Online Installer" from https://www.qt.io/download-qt-installer-oss
echo    2. Select: Qt 6.x ^> MinGW 64-bit
echo       and:    Build Tools ^> MinGW 64-bit, CMake, Ninja
echo    3. Run this file again.
start "" https://www.qt.io/download-qt-installer-oss
goto :end_err

:fail
echo.
echo  [X] Build failed. Send the error above so it can be fixed.

:end_err
echo.
pause
exit /b 1
