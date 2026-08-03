@echo off
setlocal EnableExtensions
title PartSplice 3D - Windows Build

echo =============================================
echo  PartSplice 3D - Windows Build
echo =============================================
echo.

set "PROJECT_DIR=%~dp0"
set "PROJECT_DIR=%PROJECT_DIR:~0,-1%"
if not defined PARTSPLICE_BUILD_DIR set "PARTSPLICE_BUILD_DIR=%PROJECT_DIR%\build"
set "BUILD_DIR=%PARTSPLICE_BUILD_DIR%"

set "CMAKE_EXE="
for /f "delims=" %%I in ('where cmake.exe 2^>nul') do if not defined CMAKE_EXE set "CMAKE_EXE=%%I"
if not defined CMAKE_EXE if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE_EXE=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE_EXE goto :missing_cmake

set "GIT_EXE="
for /f "delims=" %%I in ('where git.exe 2^>nul') do if not defined GIT_EXE set "GIT_EXE=%%I"
if not defined GIT_EXE if exist "C:\Program Files\Git\cmd\git.exe" set "GIT_EXE=C:\Program Files\Git\cmd\git.exe"
if not defined GIT_EXE goto :missing_git
for %%I in ("%GIT_EXE%") do set "GIT_BIN_DIR=%%~dpI"
set "PATH=%GIT_BIN_DIR%;%PATH%"

set "VCPKG_TOOLCHAIN="
if defined VCPKG_ROOT if exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" set "VCPKG_TOOLCHAIN=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_TOOLCHAIN if exist "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_TOOLCHAIN=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_TOOLCHAIN if exist "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_TOOLCHAIN if exist "C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Professional\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_TOOLCHAIN if exist "C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg\scripts\buildsystems\vcpkg.cmake" set "VCPKG_TOOLCHAIN=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\vcpkg\scripts\buildsystems\vcpkg.cmake"
if not defined VCPKG_TOOLCHAIN goto :missing_vcpkg

echo CMake: %CMAKE_EXE%
echo Git:   %GIT_EXE%
echo vcpkg: %VCPKG_TOOLCHAIN%
echo Build: %BUILD_DIR%
echo.

"%CMAKE_EXE%" -S "%PROJECT_DIR%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 "-DCMAKE_TOOLCHAIN_FILE=%VCPKG_TOOLCHAIN%"
if errorlevel 1 goto :configure_failed

"%CMAKE_EXE%" --build "%BUILD_DIR%" --config Release --target PartSplice3D --parallel
if errorlevel 1 goto :build_failed

if defined PARTSPLICE_SIGN_CERT_SHA1 (
    echo.
    echo Signiere Release mit dem konfigurierten Codesignaturzertifikat...
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\sign_windows.ps1" -File "%BUILD_DIR%\Release\PartSplice3D.exe" -Thumbprint "%PARTSPLICE_SIGN_CERT_SHA1%"
    if errorlevel 1 goto :sign_failed
    powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PROJECT_DIR%\sign_windows.ps1" -File "%BUILD_DIR%\Release\PartSpliceWorker.exe" -Thumbprint "%PARTSPLICE_SIGN_CERT_SHA1%"
    if errorlevel 1 goto :sign_failed
)

echo.
echo Build erfolgreich.
echo EXE: %BUILD_DIR%\Release\PartSplice3D.exe
echo Worker: %BUILD_DIR%\Release\PartSpliceWorker.exe
goto :success

:missing_cmake
echo FEHLER: CMake wurde nicht gefunden.
echo Installiere Visual Studio 2022 mit der Workload "Desktopentwicklung mit C++".
goto :failed

:missing_git
echo FEHLER: Git wurde nicht gefunden.
echo Installiere Git fuer Windows und starte das Skript erneut.
goto :failed

:missing_vcpkg
echo FEHLER: Die vcpkg-Toolchain wurde nicht gefunden.
echo Setze VCPKG_ROOT oder installiere vcpkg ueber Visual Studio 2022.
goto :failed

:configure_failed
echo.
echo FEHLER: Die CMake-Konfiguration ist fehlgeschlagen.
goto :failed

:build_failed
echo.
echo FEHLER: Der Release-Build ist fehlgeschlagen.
goto :failed

:sign_failed
echo.
echo FEHLER: Der Build war erfolgreich, aber Signieren oder Signaturpruefung ist fehlgeschlagen.
goto :failed

:success
if not defined PARTSPLICE_NO_PAUSE pause
exit /b 0

:failed
if not defined PARTSPLICE_NO_PAUSE pause
exit /b 1
