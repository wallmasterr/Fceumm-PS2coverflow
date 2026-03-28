@echo off
rem Build FCEUmm-PS2 using WSL (PS2SDK + gsKit must be installed inside Linux).
rem Usage:
rem   build-wsl.bat         — make -j(nproc)
rem   build-wsl.bat nopack  — skip ps2-packer (NOT_PACKED=1)
rem   build-wsl.bat clean   — make clean
rem
rem If you see /samples/Makefile.eeglobal missing: PS2SDK is empty in WSL.
rem Fix: copy wsl-ps2env.sh.example to wsl-ps2env.sh and set paths, or export PS2SDK/GSKIT in ~/.bashrc.
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

wsl.exe -e true 2>nul
if errorlevel 1 (
  echo WSL is not available. Install it from an elevated prompt:
  echo   wsl --install
  echo Then install ps2toolchain inside Ubuntu/WSL: https://github.com/ps2dev/ps2toolchain
  exit /b 1
)

set "WSLDIR="
for /f "usebackq delims=" %%i in (`wsl wslpath -a "%CD%"`) do set "WSLDIR=%%i"
if "!WSLDIR!"=="" (
  echo Could not map this folder to a WSL path ^(wslpath failed^).
  exit /b 1
)

echo WSL project path: !WSLDIR!
echo.

if /i "%~1"=="clean" (
  wsl -e bash "!WSLDIR!/build-wsl-inner.sh" clean
  set "ERR=!errorlevel!"
  goto done
)

if /i "%~1"=="nopack" (
  wsl -e bash "!WSLDIR!/build-wsl-inner.sh" nopack
  set "ERR=!errorlevel!"
  goto done
)

if not "%~1"=="" (
  echo Unknown option: %~1
  echo Usage: %~nx0 [nopack ^| clean]
  set "ERR=1"
  goto done
)

wsl -e bash "!WSLDIR!/build-wsl-inner.sh" build
set "ERR=!errorlevel!"

:done
if not "!ERR!"=="0" exit /b !ERR!

echo.
echo Done. ELFs are in this Windows folder:
echo   %CD%\fceu.elf
if exist "fceu-packed.elf" echo   %CD%\fceu-packed.elf
exit /b 0
