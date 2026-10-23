@echo off
rem ===========================================================================
rem  Evan - Qt 5.12 / MinGW (GCC 12.1) build script
rem  Toolchain: C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3
rem  Qt       : C:\Qts\5.12\5.12.10\mingw73_64
rem ===========================================================================
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "MINGW=C:\Mingws\winlibs-x86_64-posix-seh-gcc-12.1.0-llvm-14.0.6-mingw-w64msvcrt-10.0.0-r3"
set "QTDIR=C:\Qts\5.12\5.12.10\mingw73_64"
set "BUILD=%ROOT%build-qt5"

if not exist "%MINGW%\bin\g++.exe" (
    echo [ERROR] MinGW toolchain not found at: %MINGW%
    exit /b 1
)
if not exist "%QTDIR%\bin\qmake.exe" (
    echo [ERROR] Qt 5.12 not found at: %QTDIR%
    exit /b 1
)

set "PATH=%MINGW%\bin;%QTDIR%\bin;%PATH%"

rem CMake requires forward slashes in compiler paths (backslashes become escape chars)
set "MINGW_FWD=%MINGW:\=/%"
set "QTDIR_FWD=%QTDIR:\=/%"

echo [1/3] Configuring CMake project ...
cmake -S "%ROOT%." -B "%BUILD%" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DCMAKE_CXX_COMPILER="%MINGW_FWD%/bin/g++.exe" ^
    -DCMAKE_RC_COMPILER="%MINGW_FWD%/bin/windres.exe" ^
    -DQt5_DIR="%QTDIR_FWD%/lib/cmake/Qt5"
if errorlevel 1 (
    echo [ERROR] CMake configure failed.
    exit /b 1
)

echo [2/3] Building ...
cmake --build "%BUILD%" --parallel
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [3/3] Deploying Qt runtime ...
call "%ROOT%scripts\deploy_qt5.bat" "%BUILD%\evan.exe"
if errorlevel 1 (
    echo [WARN] Deployment step reported an error, check the output above.
)

echo.
echo Done. Executable: %BUILD%\evan.exe
endlocal
