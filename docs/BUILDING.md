# Building AIOS on Windows

## 1. Install the tools

| Tool | Notes |
|---|---|
| Visual Studio Build Tools | "Desktop development with C++" workload |
| Git | https://git-scm.com |
| Python 3 | needed by EDK II (`py -3`) |
| NASM | add to PATH, or set `NASM_PREFIX` |
| QEMU | to run the result (https://www.qemu.org) |

## 2. Folder layout

```
C:\AIOS\
  edk2\                  EDK II checkout          (not part of this repo)
  src\AIOSPkg\           this repo's src\AIOSPkg  (the package)
  USB\                   staging folder that QEMU boots from
  aios.bat               scripts\aios.bat from this repo
```

```bat
cd C:\AIOS
git clone https://github.com/tianocore/edk2.git
cd edk2
git submodule update --init
```

## 3. Package files

`src\AIOSPkg\AIOSPkg.dsc` must list the applications under `[Components]`:

```
[Components]
  AIOSPkg/Application/AIOS.inf
  AIOSPkg/Application/Hello.inf
```

and map the libraries the code uses (`UefiLib`, `BaseLib`, `BaseMemoryLib`, `PrintLib`, `DevicePathLib`,
`UefiBootServicesTableLib`, `UefiRuntimeServicesTableLib`, `MemoryAllocationLib`, ...).

## 4. Build

Open a *Developer Command Prompt for VS* and run:

```bat
cd C:\AIOS\edk2
edksetup.bat
set PACKAGES_PATH=C:\AIOS\src
build -p AIOSPkg\AIOSPkg.dsc -a X64 -t VS2026 -b DEBUG
```

(use your own toolchain tag: `VS2022`, `VS2026`, ...). The result is
`Build\AIOSPkg\DEBUG_VS2026\X64\AIOS.efi`.

## 5. Run

`scripts\aios.bat` does steps 4–5 for you: it builds, copies `AIOS.efi` to `USB\EFI\BOOT\BOOTX64.EFI`
and starts QEMU with OVMF. If `C:\AIOS\dos.img` exists it is attached as a second disk for the DOS emulator.

## Troubleshooting

- **`unresolved external symbol memset`** — the compiler turned a loop into a library call; use `SetMem`/`CopyMem`
  instead of hand-written fill/copy loops.
- **`C2268 ... compiler predefined library helper`** — same cause (see above).
- **Build stops instantly with no error** — `PACKAGES_PATH` does not point at the folder containing `AIOSPkg`.
