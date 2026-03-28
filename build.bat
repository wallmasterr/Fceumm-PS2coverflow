@echo off
rem FCEUmm-PS2 — requires PS2SDK, gsKit, GNU make, and ps2sdk-ports (libpng, libjpeg, zlib).
rem Usage:
rem   build.bat           — build fceu.elf and fceu-packed.elf (needs ps2-packer in PATH)
rem   build.bat nopack    — build fceu.elf only (skip ps2-packer)
rem   build.bat clean     — remove objects and ELFs
rem Set PS2SDK and GSKIT before running (see messages if missing).
setlocal EnableExtensions
cd /d "%~dp0"

if /i "%~1"=="clean" (
  call :require_env silent
  if errorlevel 1 exit /b 1
  where make >nul 2>&1
  if errorlevel 1 (
    echo GNU make not found in PATH.
    exit /b 1
  )
  make clean
  exit /b %errorlevel%
)

call :require_env
if errorlevel 1 exit /b 1

where make >nul 2>&1
if errorlevel 1 (
  echo.
  echo GNU make was not found in PATH.
  echo Options:
  echo   1^) Install MSYS2 from https://www.msys2.org/ then: pacman -S make
  echo      Add the MSYS2 usr\bin folder to PATH, or open "MSYS2 MSYS" and run: make
  echo   2^) Use WSL: run build-wsl.bat in this folder ^(install ps2toolchain inside WSL first^)
  echo.
  exit /b 1
)

set "JOBS=%NUMBER_OF_PROCESSORS%"
if "%JOBS%"=="" set "JOBS=4"

echo Building with make -j%JOBS% ...
if /i "%~1"=="nopack" (
  make -j%JOBS% NOT_PACKED=1
) else (
  make -j%JOBS% %*
)
set "ERR=%errorlevel%"
if not "%ERR%"=="0" exit /b %ERR%

echo.
if exist "fceu.elf" echo ELF:  %cd%\fceu.elf
if exist "fceu-packed.elf" echo Packed: %cd%\fceu-packed.elf
exit /b 0

:require_env
if "%PS2SDK%"=="" goto need_ps2sdk
if "%GSKIT%"=="" goto need_gskit
if /i not "%~1"=="silent" (
  echo PS2SDK=%PS2SDK%
  echo GSKIT=%GSKIT%
)
exit /b 0

:need_ps2sdk
if /i "%~1"=="silent" exit /b 1
echo.
echo PS2SDK is not set. This project cross-compiles with the PlayStation 2 SDK.
echo.
echo 1. Install the toolchain and SDK ^(Linux/WSL/MSYS2^):
echo    https://github.com/ps2dev/ps2toolchain
echo 2. Build/install gsKit and point GSKIT at it:
echo    https://github.com/ps2dev/gsKit
echo 3. Build ps2sdk-ports ^(libpng, libjpeg, zlib^) into your PS2SDK ports prefix.
echo.
echo Then set environment variables ^(example paths^):
echo   set PS2SDK=C:\ps2dev\ps2sdk
echo   set GSKIT=C:\ps2dev\gsKit
echo.
echo Re-run this script from the same Command Prompt, or set them in System Environment.
echo.
exit /b 1

:need_gskit
if /i "%~1"=="silent" exit /b 1
echo.
echo GSKIT is not set. Set it to your gsKit source/install root, e.g.:
echo   set GSKIT=C:\ps2dev\gsKit
echo.
exit /b 1
