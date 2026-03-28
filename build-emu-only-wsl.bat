@echo off
rem Quick rebuild after deps are already built (edit code, double-click this).
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

wsl.exe -e true 2>nul
if errorlevel 1 (
  echo WSL not available.
  exit /b 1
)

for /f "usebackq delims=" %%i in (`wsl wslpath -a "%CD%"`) do set "WSLDIR=%%i"
wsl bash -- "!WSLDIR!/build-wsl-inner.sh" nopack
exit /b %errorlevel%
