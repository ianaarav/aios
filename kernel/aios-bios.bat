@echo off
rem ====================================================================
rem  AIOS BIOS kernel - build + boot script (Windows only, no Linux)
rem
rem  Run from an ADMINISTRATOR command prompt (diskpart needs it):
rem      aios-bios            build the image and boot it in QEMU
rem      aios-bios run        skip the build, just boot the last image
rem      aios-bios nobuild    build the image but do not start QEMU
rem
rem  Output:  build\aios.img   <- write this to a USB stick for the laptop
rem ====================================================================
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
cd /d "%ROOT%"

set "BUILD=%ROOT%\build"
set "LIMINE=%ROOT%\limine"
set "VHD=%BUILD%\aios.vhd"
set "IMG=%BUILD%\aios.img"
set "MNT=%BUILD%\mnt"
set "QEMU=C:\Program Files\qemu\qemu-system-x86_64.exe"

if /I "%1"=="run" goto :boot

rem ---------- prerequisites ----------
fltmc >nul 2>&1
if errorlevel 1 (
    echo [AIOS] Please run this from an ADMINISTRATOR command prompt.
    pause & exit /b 1
)

if exist "C:\Program Files\LLVM\bin\clang.exe" set "PATH=%PATH%;C:\Program Files\LLVM\bin"

where clang >nul 2>&1
if errorlevel 1 (
    echo [AIOS] clang not found. Install it with:   winget install LLVM.LLVM
    echo        then open a NEW administrator prompt and run this again.
    pause & exit /b 1
)
where ld.lld >nul 2>&1
if errorlevel 1 (
    echo [AIOS] ld.lld not found. Reinstall LLVM:   winget install LLVM.LLVM
    pause & exit /b 1
)
where git >nul 2>&1
if errorlevel 1 (
    echo [AIOS] git not found. Install it with:   winget install Git.Git
    pause & exit /b 1
)

if not exist "%BUILD%" mkdir "%BUILD%"

rem ---------- Limine bootloader (pinned to v7 so it matches kernel.c) ----------
if not exist "%LIMINE%\limine-bios.sys" (
    echo [AIOS] Downloading Limine v7 bootloader...
    git clone https://github.com/limine-bootloader/limine.git --branch=v7.x-binary --depth=1 "%LIMINE%"
    if errorlevel 1 (
        echo [AIOS] Could not download Limine.
        pause & exit /b 1
    )
)
if not exist "%LIMINE%\limine.exe" (
    echo [AIOS] Building limine.exe...
    clang -O2 -D_CRT_SECURE_NO_WARNINGS "%LIMINE%\limine.c" -o "%LIMINE%\limine.exe"
    if errorlevel 1 goto :fail
)

rem ---------- compile + link the kernel ----------
echo [AIOS] Compiling kernel...
clang --target=x86_64-unknown-none-elf -std=gnu11 -ffreestanding -fno-stack-protector ^
  -fno-pic -fno-pie -mcmodel=kernel -mno-red-zone -mgeneral-regs-only -O2 -Wall -Wextra ^
  -I"%LIMINE%" -c kernel.c -o "%BUILD%\kernel.o"
if errorlevel 1 goto :fail

ld.lld -nostdlib -static -z max-page-size=0x1000 -T linker.ld -o "%BUILD%\aios" "%BUILD%\kernel.o"
if errorlevel 1 goto :fail

rem ---------- RAM disk (files inside the "ramdisk" folder show up as A:\) ----------
if not exist "%ROOT%\ramdisk" mkdir "%ROOT%\ramdisk"
echo [AIOS] Packing RAM disk...
tar --format ustar -cf "%BUILD%\ramdisk.tar" -C "%ROOT%\ramdisk" .
if errorlevel 1 goto :fail

rem ---------- create a 64 MB FAT32 disk image ----------
echo [AIOS] Creating disk image...
> "%BUILD%\dp_detach.txt" echo select vdisk file="%VHD%"
>> "%BUILD%\dp_detach.txt" echo detach vdisk
if exist "%VHD%" diskpart /s "%BUILD%\dp_detach.txt" >nul 2>&1
if exist "%VHD%" del /f /q "%VHD%"
if exist "%IMG%" del /f /q "%IMG%"
if exist "%MNT%" rmdir "%MNT%" >nul 2>&1
mkdir "%MNT%"

> "%BUILD%\dp_create.txt" echo create vdisk file="%VHD%" maximum=64 type=fixed
>> "%BUILD%\dp_create.txt" echo attach vdisk
>> "%BUILD%\dp_create.txt" echo convert mbr
>> "%BUILD%\dp_create.txt" echo create partition primary
>> "%BUILD%\dp_create.txt" echo active
>> "%BUILD%\dp_create.txt" echo format fs=fat32 quick label=AIOS
>> "%BUILD%\dp_create.txt" echo assign mount="%MNT%"
diskpart /s "%BUILD%\dp_create.txt" >nul
if errorlevel 1 (
    echo [AIOS] diskpart failed. Is this an Administrator prompt?
    goto :fail
)

mkdir "%MNT%\boot\limine"
copy /Y "%BUILD%\aios"            "%MNT%\boot\aios"                  >nul
copy /Y "%BUILD%\ramdisk.tar"     "%MNT%\boot\ramdisk.tar"           >nul
copy /Y "%ROOT%\limine.cfg"       "%MNT%\boot\limine\limine.cfg"     >nul
copy /Y "%LIMINE%\limine-bios.sys" "%MNT%\boot\limine\limine-bios.sys" >nul
if not exist "%MNT%\boot\limine\limine-bios.sys" (
    echo [AIOS] Copy into the disk image failed.
    diskpart /s "%BUILD%\dp_detach.txt" >nul 2>&1
    goto :fail
)

diskpart /s "%BUILD%\dp_detach.txt" >nul
rmdir "%MNT%" >nul 2>&1

echo [AIOS] Installing Limine boot sector...
"%LIMINE%\limine.exe" bios-install "%VHD%"
if errorlevel 1 goto :fail

copy /Y "%VHD%" "%IMG%" >nul
echo [AIOS] Built: %IMG%

if /I "%1"=="nobuild" goto :eof

:boot
if not exist "%VHD%" (
    echo [AIOS] No image yet. Run aios-bios without arguments first.
    pause & exit /b 1
)
echo [AIOS] Booting in QEMU...
"%QEMU%" -m 512M -vga std -drive file="%VHD%",format=raw
goto :eof

:fail
echo.
echo [AIOS] BUILD FAILED - scroll up for the error.
pause
exit /b 1
