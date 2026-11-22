# AIOS

**A modern, DOS-inspired operating system for UEFI** — a command shell, a mouse-driven desktop, and a built-in
emulator that runs real MS-DOS / FreeDOS disk images. Written in C with [EDK II](https://github.com/tianocore/edk2).

```
====================================
           Welcome to AIOS
====================================
AIOS shell version 0.7.0
Type HELP to see available commands. Press Tab to complete.

AIOS C:\> _
```

> **Status:** hobby project, actively developed. It runs on UEFI firmware (tested in QEMU/OVMF).
> Not a replacement for a real OS — see [Limitations](#limitations).

## Features

### Shell
- DOS-style commands: `DIR`, `CD`, `TYPE`, `COPY`, `REN`, `DEL`, `MKDIR`, `TOUCH`, `INFO`, `COLOR`, `MEM` ...
- **"Did you mean?"** suggestions for mistyped commands *and* file names
- **Tab completion** for commands and files (press Tab again to cycle)
- Command history, full line editing (arrows, Home/End, Delete, Esc)
- **Drive letters** (`C:`, `D:`, ...) for every disk UEFI can see, cross-drive `COPY`, `VOL`, `DISKS`
- Run any `.EFI` program by typing its name (`RUN` or just `hello`)

### Desktop (`GUI`)
- Windowed desktop with taskbar, start menu, clock, desktop icons, gradient title bars
- **Terminal** window running the real shell (with scrollback), **Files** browser, **Notepad**, **System Info**
- Mouse support (PS/2, read directly from the hardware) and a keyboard-only mode (`F2`)
- Move, minimize, maximize, close; double-click to open

### DOS emulator (`DOS`)
- A software **286-class PC**: CPU, BIOS (video, keyboard, disk, clock, timer), XMS memory, A20 line
- Boots real **MS-DOS 6.22 / FreeDOS** from a disk image file or a raw disk
- CPU core validated against tens of thousands of real **8088 hardware test vectors**
- Aimed at running classic DOS programs such as Word for DOS 6

## Quick start (prebuilt)

1. Download `AIOS.efi` from the [Releases](../../releases) page.
2. Make a folder with this layout: `USB\EFI\BOOT\BOOTX64.EFI` (rename `AIOS.efi` to `BOOTX64.EFI`).
3. Install [QEMU](https://www.qemu.org/download/#windows) and run (adjust paths):

```bat
"C:\Program Files\qemu\qemu-system-x86_64.exe" ^
  -machine q35 -m 512M ^
  -drive if=pflash,format=raw,readonly=on,file="C:\Program Files\qemu\share\edk2-x86_64-code.fd" ^
  -drive if=pflash,format=raw,file=OVMF_VARS.fd ^
  -drive format=raw,file=fat:rw:USB ^
  -net none
```

(`OVMF_VARS.fd` is a copy of `edk2-i386-vars.fd` from the QEMU `share` folder.)
It also boots on real UEFI machines from a FAT32 USB stick with the same `EFI\BOOT\BOOTX64.EFI` layout.

## Building from source (Windows)

You need: **Visual Studio Build Tools** (C++ workload), **Git**, **Python 3**, **NASM**, and a checkout of **EDK II**.
See [docs/BUILDING.md](docs/BUILDING.md) for the full walkthrough. In short:

```bat
git clone https://github.com/tianocore/edk2.git C:\AIOS\edk2
git clone https://github.com/ianaarav/AIOS.git C:\AIOS\src-repo
rem copy / link src\AIOSPkg into C:\AIOS\src\AIOSPkg, then:
scripts\aios.bat
```

`scripts/aios.bat` builds with EDK II, stages the result, and boots it in QEMU.

## Commands

| Command | What it does |
|---|---|
| `HELP` | List all commands |
| `DIR` / `LS`, `CD`, `PWD` | Browse folders |
| `TYPE` / `CAT` | Show a text file |
| `COPY` / `CP`, `REN`, `DEL` / `RM`, `MKDIR` / `MD`, `TOUCH` | File management (drives allowed: `COPY C:\a.txt D:\a.txt`) |
| `INFO <path>` | File details |
| `VOL`, `DISKS` | List drives / physical disks |
| `RUN <prog>` | Start an `.EFI` program |
| `GUI [1\|2] [nops2]` | Start the desktop (`F1` menu, `F2` key-mouse, `F3` next window, `F4` close, `F10` exit) |
| `DOS <image or #N> [/RO] [/A]` | Run MS-DOS/FreeDOS from a disk image or physical disk (`F12` leaves) |
| `COLOR`, `MEM`, `TIME`, `DATE`, `HISTORY`, `EFI`, `VER`, `ABOUT` | Utilities |
| `REBOOT`, `SHUTDOWN`, `EXIT` | Power / leave |

## Running DOS

```
DISKS                 (find the number of your DOS disk, e.g. Disk 1)
DOS #1                (boot it; answer y to the write warning)
DOS #1 /RO            (same, but never writes to the disk)
DOS floppy.img        (run a small image straight from a file)
```

Details, CONFIG.SYS advice and troubleshooting: [docs/DOS-EMULATOR.md](docs/DOS-EMULATOR.md).

> **Do not commit or share MS-DOS, Windows or Microsoft Word disk images** — they are copyrighted.
> FreeDOS is free software and works great for testing.

## Project layout

```
src/AIOSPkg/                EDK II package
  AIOSPkg.dsc               platform description
  Application/
    main.c                  shell, file system, drive letters, commands
    gui.inc                 desktop (windows, mouse, Files/Notepad/Terminal/System Info)
    dos.inc                 DOS command + UEFI host side of the emulator
    x86cpu.inc              286 real-mode CPU core
    x86bios.inc             software BIOS, XMS memory manager, boot logic
    AIOS.inf  Hello.c/.inf  module descriptions + a sample program
kernel/                     experimental bare-metal x86_64 kernel (Limine, legacy BIOS boot)
scripts/aios.bat            build + run helper
docs/                       build guide, DOS emulator guide
```

## Limitations

- UEFI application, not a stand-alone kernel (the `kernel/` folder is a separate BIOS experiment)
- DOS emulator: text mode only (no VGA/CGA graphics yet), no mouse inside DOS, no sound, no printer, no EMS,
  no 386-only programs (Windows 3.x, EMM386)
- Mouse needs a PS/2 mouse (QEMU, VMware, laptop touchpads); otherwise use the `F2` keyboard mode
- Tested mainly in QEMU/OVMF; real hardware testing is limited

## Roadmap

- [ ] Mouse + graphics modes inside the DOS emulator
- [ ] Run DOS inside a desktop window
- [ ] `INSTALL` command (copy AIOS to a disk and add a boot entry)
- [ ] Battery charge / health reporting
- [ ] More desktop apps
- [ ] Batch scripts, pipes and redirection in the shell

## Credits

- [EDK II](https://github.com/tianocore/edk2) (TianoCore) — build system and UEFI libraries (BSD-2-Clause-Patent)
- 8x8 bitmap font: public domain
- [SingleStepTests/8088](https://github.com/SingleStepTests/8088) — hardware test vectors used to validate the CPU core
- [FreeDOS](https://www.freedos.org/) — used for testing the DOS emulator
- [Limine](https://github.com/limine-bootloader/limine) — bootloader for the experimental BIOS kernel

## License

[MIT](LICENSE)
