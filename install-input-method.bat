@echo off
setlocal

set "PROJECT_ROOT=%~dp0"
set "INSTALL_SCRIPT=%PROJECT_ROOT%CodeSnippetInputTsf\install-input-method.ps1"
set "TSF_DLL=%PROJECT_ROOT%publish\tsf-x64-literal\CodeSnippetInputTsf.dll"

if not exist "%INSTALL_SCRIPT%" (
    echo ERROR: Installer script was not found:
    echo "%INSTALL_SCRIPT%"
    pause
    exit /b 2
)

if not exist "%TSF_DLL%" (
    echo ERROR: TSF DLL was not found:
    echo "%TSF_DLL%"
    pause
    exit /b 3
)

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%INSTALL_SCRIPT%" -DllPath "%TSF_DLL%"
set "INSTALL_RESULT=%ERRORLEVEL%"

if not "%INSTALL_RESULT%"=="0" (
    echo.
    echo Installation failed with exit code %INSTALL_RESULT%.
) else (
    echo.
    echo Installation completed successfully.
)

pause
exit /b %INSTALL_RESULT%
