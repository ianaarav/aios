# Changelog

## 0.7.0 — DOS emulator
- `DOS` command: boots MS-DOS / FreeDOS from a disk image file or a raw disk (`DOS #N`)
- 286 real-mode CPU core validated against 8088 hardware test vectors
- Software BIOS (INT 10h/11h/12h/13h/14h/15h/16h/17h/19h/1Ah), XMS memory manager, A20 line, HMA
- Keyboard translation incl. Alt, Ctrl, Shift+F-keys; CP437 box drawing on the UEFI console

## 0.6.x — Desktop
- `GUI`: windows, taskbar, start menu, clock, desktop icons, gradient title bars, maximize/restore
- Apps: Terminal (real shell, scrollback), Files, Notepad, System Info
- PS/2 mouse driver talking to the keyboard controller directly; keyboard-only mode (`F2`)
- Shortcuts: `F1` menu, `F3` next window, `F4` close window, `F10` exit

## 0.5.0 — Smarter shell
- "Did you mean?" for commands and file names, Tab completion
- Drive letters, `VOL`, `DISKS`, `REN`, `RUN`, cross-drive `COPY`

## 0.4.0 — Shell additions
- `PWD`, `TOUCH`, `INFO`, `RM`

## 0.3.0 — Real shell
- Line editor with history, file commands (`DIR`, `CD`, `TYPE`, `MKDIR`, `DEL`, `COPY`), colors, `MEM`, `REBOOT`

## 0.2.0 — First boot
- UEFI shell application: `HELP`, `VER`, `ABOUT`, `ECHO`, `TIME`, `DATE`, `EFI`, `EXIT`

## kernel/ — BIOS experiment
- Bare-metal x86_64 kernel booted by Limine on legacy BIOS: framebuffer text console, PS/2 keyboard,
  RTC, RAM disk, shell
