@echo off
setlocal
if not exist "%~dp0build\IoTControl_PC.exe" (
    echo Build the PC project before running this mockup.
    pause
    exit /b 1
)
start "" "%~dp0build\IoTControl_PC.exe" --login
