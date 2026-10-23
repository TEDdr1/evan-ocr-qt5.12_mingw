@echo off
rem ===========================================================================
rem  Deploy Qt 5.12 runtime (DLLs + platform plugins) next to evan.exe
rem  Usage: deploy_qt5.bat <path-to-evan.exe>
rem ===========================================================================
setlocal

set "QT_BIN=C:\Qts\5.12\5.12.10\mingw73_64\bin"
set "EXE=%~1"

if "%EXE%"=="" (
    echo Usage: deploy_qt5.bat path\to\evan.exe
    exit /b 1
)
if not exist "%EXE%" (
    echo [ERROR] Executable not found: %EXE%
    exit /b 1
)

rem --- Qt runtime DLLs and plugin folders -----------------------------------
"%QT_BIN%\windeployqt.exe" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%EXE%"
if errorlevel 1 (
    echo [ERROR] windeployqt failed.
    exit /b 1
)

rem --- OpenSSL 1.1 x64 (required by Qt 5.12 for HTTPS) ----------------------
if exist "%~dp0..\third_party\openssl\libssl-1_1-x64.dll" (
    copy /Y "%~dp0..\third_party\openssl\*.dll" "%~dp1" >nul
    echo OpenSSL 1.1 x64 copied for HTTPS support.
) else (
    echo [WARN] third_party\openssl not found - HTTPS features will be unavailable.
)

echo Deployment finished.
endlocal
