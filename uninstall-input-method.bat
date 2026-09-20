@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "UNINSTALL_SCRIPT=%PROJECT_ROOT%CodeSnippetInputTsf\uninstall-input-method.ps1"
set "TSF_DLL=%PROJECT_ROOT%publish\tsf-x64-literal\CodeSnippetInputTsf.dll"

if not exist "%UNINSTALL_SCRIPT%" (
    echo ERROR: Uninstaller script was not found:
    echo "%UNINSTALL_SCRIPT%"
    pause
    exit /b 2
)

if not exist "%TSF_DLL%" (
    echo ERROR: TSF DLL was not found:
    echo "%TSF_DLL%"
    pause
    exit /b 3
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%UNINSTALL_SCRIPT%" -DllPath "%TSF_DLL%"
set "UNINSTALL_RESULT=%ERRORLEVEL%"

if not "%UNINSTALL_RESULT%"=="0" (
    echo.
    echo Uninstallation failed with exit code %UNINSTALL_RESULT%.
) else (
    echo.
    echo Uninstallation completed successfully.
)

pause
exit /b %UNINSTALL_RESULT%
