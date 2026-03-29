@echo off
rem One-shot: ps2sdk-ports (cmakelibs) + gsKit + FCEUmm ELF (nopack).
rem Prereq: ps2toolchain + ps2sdk (clone ps2sdk-src, make && make release into %%PS2SDK%%).
rem First run is very long; after that use build-emu-only-wsl.bat for quick rebuilds.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

wsl.exe -e true 2>nul
if errorlevel 1 (
  echo WSL not available. Install with: wsl --install
  exit /b 1
)

for /f "usebackq delims=" %%i in (`wsl wslpath -a "%CD%"`) do set "WSLDIR=%%i"
rem Editor/Windows often saves *.sh as CRLF; bash then errors on "set -o pipefail"
for %%f in (*.sh) do wsl sed -i "s/\r$//" "!WSLDIR!/%%~nxf"
wsl bash -- "!WSLDIR!/wsl-build-deps-and-emu.sh"
set "ERR=!errorlevel!"
if not "!ERR!"=="0" exit /b !ERR!

echo.
echo ELF: %CD%\fceu.elf
exit /b 0
