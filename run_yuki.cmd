@echo off
chcp 65001 >nul
if not exist "%~dp0build\zig\er_calc.exe" (
    echo Build the calculator first: powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    pause
    exit /b 2
)
"%~dp0build\zig\er_calc.exe" --interactive --catalog "%~dp0data\catalog.tsv"
if errorlevel 1 pause
