# DOS emulator guide

AIOS contains a software PC (286-class CPU, BIOS, XMS memory) that boots real DOS disk images.

## Run a DOS disk

```
DISKS                 list physical disks (note the number)
DOS #1                boot physical disk 1
DOS #1 /RO            same, read-only (nothing is written)
DOS floppy.img        run a small image straight from a file
DOS floppy.img #1 /A  floppy as A:, disk 1 as C:, boot the floppy
```

- Floppy-sized images become `A:`/`B:`, larger ones `C:`/`D:`.
- `F12` leaves the emulator. When you leave, "Emulator notes" are printed if DOS used anything the
  emulator does not support yet.

## Getting a disk image from VMware

1. Shut the VM down completely and work on a **copy** of the `.vmdk`.
2. Convert it to a raw image:
   ```
   qemu-img convert -O raw "C:\path\MSDOS.vmdk" C:\AIOS\dos.img
   ```
3. `scripts\aios.bat` attaches `C:\AIOS\dos.img` as a second disk; in AIOS use `DISKS` then `DOS #1`.

## CONFIG.SYS advice

AIOS provides the XMS driver and A20 line itself, and the CPU is a 286, so in `CONFIG.SYS`:

- remove (REM out) `DEVICE=...HIMEM.SYS` and `DEVICE=...EMM386.EXE`
- change `DOS=HIGH,UMB` to `DOS=HIGH`
- change `DEVICEHIGH=` to `DEVICE=`

## Supported / not supported

| Supported | Not yet |
|---|---|
| 8086/186/286 real-mode instructions | 386+ instructions, protected mode |
| 80x25 text mode, cursor, scrolling | graphics modes (CGA/EGA/VGA) |
| Keyboard (Alt, Ctrl, Shift, F-keys) | mouse (INT 33h) |
| Hard disks (CHS) and floppies | INT 13h extensions, big-disk LBA |
| Timer, RTC date/time | sound, printer, serial |
| XMS memory (8 MB), HMA | EMS, UMBs |

## Legal note

Do not publish MS-DOS, Windows or Microsoft Word images. FreeDOS (GPL) is a good, legal test system.
