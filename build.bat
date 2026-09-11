@echo off
REM ===================================================================
REM InitraOS build script for native Windows
REM
REM   build.bat          build build\disk.img
REM   build.bat run      build, then boot in QEMU
REM   build.bat clean    delete the build directory
REM
REM Requires nasm.exe and python on PATH. If NASM is not on PATH, set it:
REM   set NASM=C:\path\to\nasm.exe
REM ===================================================================

setlocal enabledelayedexpansion

if "%NASM%"=="" set NASM=nasm
if "%PYTHON%"=="" set PYTHON=python
if "%QEMU%"=="" set QEMU=qemu-system-i386

if /i "%1"=="clean" (
    if exist build rmdir /s /q build
    echo Cleaned.
    goto :eof
)

if not exist build mkdir build

echo [1/4] Assembling stage2.asm
%NASM% -f bin %NASMFLAGS% stage2.asm -o build\stage2.bin
if errorlevel 1 goto :fail

echo [2/4] Assembling kernel.asm
%NASM% -f bin %NASMFLAGS% kernel.asm -o build\kernel.bin
if errorlevel 1 goto :fail

echo [3/4] Computing bootloader sector count
for /f %%i in ('%PYTHON% tools\mkimage.py --sectors-only') do set SECTORS=%%i
if errorlevel 1 goto :fail
echo       bootloader will load !SECTORS! sectors

%NASM% -f bin boot.asm -o build\boot.bin -DLOAD_SECTORS=!SECTORS!
if errorlevel 1 goto :fail

echo [4/4] Building disk image
%PYTHON% tools\mkimage.py
if errorlevel 1 goto :fail

if /i "%1"=="run" (
    echo Booting in QEMU...
    %QEMU% -drive file=build\disk.img,format=raw,if=floppy -serial stdio -no-reboot
)

goto :eof

:fail
echo.
echo BUILD FAILED
exit /b 1
