@echo off
rem ============================================================
rem  AIOS one-click build + boot  (save as C:\AIOS\aios.bat)
rem  Usage:  aios         build, stage and boot in QEMU
rem          aios run     skip the build, just boot
rem
rem  If C:\AIOS\dos.img exists it is attached as a second disk, so
rem  the AIOS command  DOS #1  can run it.
rem ============================================================

set AIOS_ROOT=C:\AIOS
set VCVARS="C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set QEMU_DIR=C:\Program Files\qemu
set EFI_OUT=%AIOS_ROOT%\edk2\Build\AIOSPkg\DEBUG_VS2026\X64\AIOS.efi
set HELLO_OUT=%AIOS_ROOT%\edk2\Build\AIOSPkg\DEBUG_VS2026\X64\HELLO.efi
set BOOT_DIR=%AIOS_ROOT%\USB\EFI\BOOT

if /I "%1"=="run" goto :boot

rem ---------- Build ----------
call %VCVARS% >nul
if errorlevel 1 (
    echo [AIOS] Could not initialise the Visual Studio environment.
    pause & exit /b 1
)

cd /d %AIOS_ROOT%\edk2
call edksetup.bat >nul 2>&1
set PACKAGES_PATH=%AIOS_ROOT%\src

echo [AIOS] Building...
call build -p AIOSPkg\AIOSPkg.dsc -a X64 -t VS2026 -b DEBUG
if errorlevel 1 (
    echo.
    echo [AIOS] BUILD FAILED - scroll up for the error.
    pause & exit /b 1
)

rem ---------- Stage ----------
if not exist "%BOOT_DIR%" mkdir "%BOOT_DIR%"
copy /Y "%EFI_OUT%" "%BOOT_DIR%\BOOTX64.EFI" >nul
if errorlevel 1 (
    echo [AIOS] Could not copy %EFI_OUT%
    pause & exit /b 1
)
if exist "%HELLO_OUT%" copy /Y "%HELLO_OUT%" "%AIOS_ROOT%\USB\HELLO.EFI" >nul
echo [AIOS] Staged BOOTX64.EFI

:boot
rem ---------- Boot ----------
if not exist "%AIOS_ROOT%\OVMF_VARS.fd" copy "%QEMU_DIR%\share\edk2-i386-vars.fd" "%AIOS_ROOT%\OVMF_VARS.fd" >nul

set "DOSDISK="
if exist "%AIOS_ROOT%\dos.img" set "DOSDISK=-drive format=raw,file=%AIOS_ROOT%\dos.img"

echo [AIOS] Booting in QEMU...
"%QEMU_DIR%\qemu-system-x86_64.exe" ^
  -machine q35 -m 512M ^
  -drive if=pflash,format=raw,readonly=on,file="%QEMU_DIR%\share\edk2-x86_64-code.fd" ^
  -drive if=pflash,format=raw,file="%AIOS_ROOT%\OVMF_VARS.fd" ^
  -drive format=raw,file=fat:rw:"%AIOS_ROOT%\USB" ^
  %DOSDISK% ^
  -net none
