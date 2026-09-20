@echo off
setlocal

set "PUBLISH_ROOT=%~dp0publish"
set "CURRENT_DLL=%PUBLISH_ROOT%\tsf-x64\CodeSnippetInputTsf.dll"
set "OLD_DLL=%PUBLISH_ROOT%\tsf-x64-literal\CodeSnippetInputTsf.dll"

if not exist "%CURRENT_DLL%" (
    echo ERROR: The current input-method DLL was not found:
    echo "%CURRENT_DLL%"
    pause
    exit /b 2
)

reg.exe query "HKCR\CLSID\{20B13E38-A7B1-4ECF-A263-956894F41828}\InprocServer32" /ve 2>nul | findstr.exe /i /l /c:"%CURRENT_DLL%" >nul
if errorlevel 1 (
    echo.
    echo The current input method has not been registered yet.
    echo First run install-input-method.bat and approve the administrator prompt.
    echo Then restart Windows and run this cleanup file again.
    pause
    exit /b 3
)

for %%D in ("manager-live" "tsf-x64-live" "tsf-x64-v2" "tsf-x64-context" "tsf-x64-state") do (
    if exist "%PUBLISH_ROOT%\%%~D" rmdir /s /q "%PUBLISH_ROOT%\%%~D" >nul 2>&1
)

for %%F in ("CodeSnippetInput.deps.json" "CodeSnippetInput.dll" "CodeSnippetInput.exe" "CodeSnippetInput.pdb" "CodeSnippetInput.runtimeconfig.json") do (
    if exist "%PUBLISH_ROOT%\%%~F" del /f /q "%PUBLISH_ROOT%\%%~F"
)

if exist "%OLD_DLL%" (
    echo.
    echo The old input-method DLL is still in use.
    echo Restart Windows completely, then run this cleanup file again.
    echo Closing this command window is not enough.
    pause
    exit /b 1
)

echo.
echo Old published versions have been removed.
pause
exit /b 0
