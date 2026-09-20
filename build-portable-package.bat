@echo off
setlocal

set "PACKAGE_SCRIPT=%~dp0build-portable-package.ps1"

if not exist "%PACKAGE_SCRIPT%" (
    echo ERROR: Packaging script was not found:
    echo "%PACKAGE_SCRIPT%"
    pause
    exit /b 2
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%PACKAGE_SCRIPT%"
set "PACKAGE_RESULT=%ERRORLEVEL%"

echo.
if not "%PACKAGE_RESULT%"=="0" (
    echo Packaging failed with exit code %PACKAGE_RESULT%.
) else (
    echo Packaging completed successfully.
)

pause
exit /b %PACKAGE_RESULT%
