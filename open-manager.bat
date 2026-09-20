@echo off
setlocal

set "MANAGER=%~dp0publish\manager-context\CodeSnippetInput.exe"
if not exist "%MANAGER%" (
    echo ERROR: Manager executable was not found:
    echo "%MANAGER%"
    pause
    exit /b 2
)

start "" "%MANAGER%"
exit /b 0
