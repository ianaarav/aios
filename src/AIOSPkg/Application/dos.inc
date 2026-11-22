//
// =====================================================================
//  AIOS DOS emulator - UEFI host side (the DOS command).  This file is
//  #included from main.c.
//
//  DOS <image-or-#disk> [second] [/RO] [/A]
//    - an image file, or #N = physical disk N exactly as DISKS numbers it
//    - images of floppy size become A: (then B:), anything larger is a
//      hard disk (C:, then D:).  Boots C: if there is one, else A:.
//    - /RO opens the images read-only (nothing is ever written to them)
//    - /A  boots from the floppy even if a hard disk is present
//    - F12 leaves the emulator
// =====================================================================
//

#include <Protocol/SimpleTextInEx.h>

#if defined (_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4244 4245 4389 4018 4100 4127 4146 4189 4267 4305 4310 4505 4701 4702 4703 4706 4054 4055 4057)
#endif

#include "x86cpu.inc"
#include "x86bios.inc"

STATIC CONST UINT16  gCp437[256] = {
  0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x002E,
  0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020, 0x0020,
  0x25BA, 0x25C4, 0x2195, 0x0021, 0x0020, 0x0020, 0x002D, 0x0020,
  0x2191, 0x2193, 0x2192, 0x2190, 0x0020, 0x2194, 0x25B2, 0x25BC,
  0x0020, 0x0021, 0x0022, 0x0023, 0x0024, 0x0025, 0x0026, 0x0027,
  0x0028, 0x0029, 0x002A, 0x002B, 0x002C, 0x002D, 0x002E, 0x002F,
  0x0030, 0x0031, 0x0032, 0x0033, 0x0034, 0x0035, 0x0036, 0x0037,
  0x0038, 0x0039, 0x003A, 0x003B, 0x003C, 0x003D, 0x003E, 0x003F,
  0x0040, 0x0041, 0x0042, 0x0043, 0x0044, 0x0045, 0x0046, 0x0047,
  0x0048, 0x0049, 0x004A, 0x004B, 0x004C, 0x004D, 0x004E, 0x004F,
  0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0055, 0x0056, 0x0057,
  0x0058, 0x0059, 0x005A, 0x005B, 0x005C, 0x005D, 0x005E, 0x005F,
  0x0060, 0x0061, 0x0062, 0x0063, 0x0064, 0x0065, 0x0066, 0x0067,
  0x0068, 0x0069, 0x006A, 0x006B, 0x006C, 0x006D, 0x006E, 0x006F,
  0x0070, 0x0071, 0x0072, 0x0073, 0x0074, 0x0075, 0x0076, 0x0077,
  0x0078, 0x0079, 0x007A, 0x007B, 0x007C, 0x007D, 0x007E, 0x0020,
  0x00C7, 0x00FC, 0x00E9, 0x00E2, 0x00E4, 0x00E0, 0x00E5, 0x00E7,
  0x00EA, 0x00EB, 0x00E8, 0x00EF, 0x00EE, 0x00EC, 0x00C4, 0x00C5,
  0x00C9, 0x00E6, 0x00C6, 0x00F4, 0x00F6, 0x00F2, 0x00FB, 0x00F9,
  0x00FF, 0x00D6, 0x00DC, 0x00A2, 0x00A3, 0x00A5, 0x20A7, 0x0192,
  0x00E1, 0x00ED, 0x00F3, 0x00FA, 0x00F1, 0x00D1, 0x00AA, 0x00BA,
  0x00BF, 0x2310, 0x00AC, 0x00BD, 0x00BC, 0x00A1, 0x00AB, 0x00BB,
  0x2591, 0x2592, 0x2593, 0x2502, 0x2524, 0x2561, 0x2562, 0x2556,
  0x2555, 0x2563, 0x2551, 0x2557, 0x255D, 0x255C, 0x255B, 0x2510,
  0x2514, 0x2534, 0x252C, 0x251C, 0x2500, 0x253C, 0x255E, 0x255F,
  0x255A, 0x2554, 0x2569, 0x2566, 0x2560, 0x2550, 0x256C, 0x2567,
  0x2568, 0x2564, 0x2565, 0x2559, 0x2558, 0x2552, 0x2553, 0x256B,
  0x256A, 0x2518, 0x250C, 0x2588, 0x2584, 0x258C, 0x2590, 0x2580,
  0x03B1, 0x00DF, 0x0393, 0x03C0, 0x03A3, 0x03C3, 0x00B5, 0x03C4,
  0x03A6, 0x0398, 0x03A9, 0x03B4, 0x221E, 0x03C6, 0x03B5, 0x2229,
  0x2261, 0x00B1, 0x2265, 0x2264, 0x2320, 0x2321, 0x00F7, 0x2248,
  0x00B0, 0x2219, 0x00B7, 0x221A, 0x207F, 0x00B2, 0x25A0, 0x00A0
};

STATIC CONST UINT8  gScanOf[128] = {
  0x00, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x0E, 0x0F, 0x24, 0x25, 0x26, 0x1C, 0x31, 0x18,
  0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x01, 0x00, 0x00, 0x00, 0x00,
  0x39, 0x02, 0x28, 0x04, 0x05, 0x06, 0x08, 0x28, 0x0A, 0x0B, 0x09, 0x0D, 0x33, 0x0C, 0x34, 0x35,
  0x0B, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x27, 0x27, 0x33, 0x0D, 0x34, 0x35,
  0x03, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18,
  0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x1A, 0x2B, 0x1B, 0x07, 0x0C,
  0x29, 0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26, 0x32, 0x31, 0x18,
  0x19, 0x10, 0x13, 0x1F, 0x14, 0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C, 0x1A, 0x2B, 0x1B, 0x29, 0x00
};


#define DOS_XMS_BYTES  (8u * 1024u * 1024u)

STATIC X86                                  gDos;
STATIC EFI_FILE_PROTOCOL                    *gDosImg[4];
STATIC EFI_BLOCK_IO_PROTOCOL                *gDosBio[4];
STATIC volatile UINTN                       gDosTicks = 0;
STATIC CHAR8                                gDosLog[16][72];
STATIC UINT32                               gDosLogA[16];
STATIC UINT32                               gDosLogB[16];
STATIC UINTN                                gDosLogN = 0;
STATIC UINT16                               gDosShadow[25][80];
STATIC EFI_SIMPLE_TEXT_INPUT_EX_PROTOCOL    *gDosKeyEx = NULL;

//
// ---- host hooks required by the emulator core ----
//
//
// Reads or writes sectors of a physical disk through the block I/O
// protocol, whatever the disk's native sector size is.
//
STATIC
BOOLEAN
BioXfer(EFI_BLOCK_IO_PROTOCOL *Bio, UINT64 Lba, UINTN Count, UINT8 *Buf, BOOLEAN Write)
{
  UINT32      Bs = Bio->Media->BlockSize;
  UINT64      ByteOff = Lba * 512;
  UINTN       Bytes = Count * 512;
  UINT64      FirstBlk = ByteOff / Bs;
  UINTN       Pre = (UINTN)(ByteOff % Bs);
  UINTN       Total = ((Pre + Bytes + Bs - 1) / Bs) * Bs;
  UINT8       *Tmp = NULL;
  EFI_STATUS  St;
  UINTN       I;

  if (Bs == 512) {
    St = Write ? Bio->WriteBlocks(Bio, Bio->Media->MediaId, Lba, Bytes, Buf)
               : Bio->ReadBlocks(Bio, Bio->Media->MediaId, Lba, Bytes, Buf);
    return (BOOLEAN)!EFI_ERROR(St);
  }

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Total, (VOID **)&Tmp))) {
    return FALSE;
  }

  if (!Write || Pre != 0 || (Bytes % Bs) != 0) {
    St = Bio->ReadBlocks(Bio, Bio->Media->MediaId, FirstBlk, Total, Tmp);
    if (EFI_ERROR(St)) {
      gBS->FreePool(Tmp);
      return FALSE;
    }
  }

  if (Write) {
    for (I = 0; I < Bytes; I++) {
      Tmp[Pre + I] = Buf[I];
    }
    St = Bio->WriteBlocks(Bio, Bio->Media->MediaId, FirstBlk, Total, Tmp);
  } else {
    for (I = 0; I < Bytes; I++) {
      Buf[I] = Tmp[Pre + I];
    }
    St = EFI_SUCCESS;
  }

  gBS->FreePool(Tmp);
  return (BOOLEAN)!EFI_ERROR(St);
}

STATIC
BOOLEAN
HostDiskRead(UINTN Drive, UINT64 Lba, UINTN Count, UINT8 *Buf)
{
  UINTN  Size = Count * 512;

  if (Drive >= 4) {
    return FALSE;
  }
  if (gDosBio[Drive] != NULL) {
    return BioXfer(gDosBio[Drive], Lba, Count, Buf, FALSE);
  }
  if (gDosImg[Drive] == NULL) {
    return FALSE;
  }
  if (EFI_ERROR(gDosImg[Drive]->SetPosition(gDosImg[Drive], Lba * 512))) {
    return FALSE;
  }
  if (EFI_ERROR(gDosImg[Drive]->Read(gDosImg[Drive], &Size, Buf))) {
    return FALSE;
  }
  return (BOOLEAN)(Size == Count * 512);
}

STATIC
BOOLEAN
HostDiskWrite(UINTN Drive, UINT64 Lba, UINTN Count, CONST UINT8 *Buf)
{
  UINTN  Size = Count * 512;

  if (Drive >= 4) {
    return FALSE;
  }
  if (gDosBio[Drive] != NULL) {
    return BioXfer(gDosBio[Drive], Lba, Count, (UINT8 *)Buf, TRUE);
  }
  if (gDosImg[Drive] == NULL) {
    return FALSE;
  }
  if (EFI_ERROR(gDosImg[Drive]->SetPosition(gDosImg[Drive], Lba * 512))) {
    return FALSE;
  }
  if (EFI_ERROR(gDosImg[Drive]->Write(gDosImg[Drive], &Size, (VOID *)Buf))) {
    return FALSE;
  }
  return (BOOLEAN)(Size == Count * 512);
}

STATIC
VOID
HostLog(CONST CHAR8 *Msg, UINT32 A, UINT32 B)
{
  UINTN  I = gDosLogN % 16;
  UINTN  P;
  UINTN  K;

  if (gDosLogN > 0) {
    P = (gDosLogN - 1) % 16;
    if (gDosLogA[P] == A && gDosLogB[P] == B && AsciiStrCmp(gDosLog[P], Msg) == 0) {
      return;
    }
  }
  for (K = 0; K < 71 && Msg[K] != 0; K++) {
    gDosLog[I][K] = Msg[K];
  }
  gDosLog[I][K] = 0;
  gDosLogA[I] = A;
  gDosLogB[I] = B;
  gDosLogN++;
}

STATIC
VOID
HostTime(UINT16 *Year, UINT8 *Month, UINT8 *Day, UINT8 *Hour, UINT8 *Min, UINT8 *Sec)
{
  EFI_TIME  T;

  if (EFI_ERROR(gRT->GetTime(&T, NULL))) {
    *Year = 1994;
    *Month = 1;
    *Day = 1;
    *Hour = 0;
    *Min = 0;
    *Sec = 0;
    return;
  }
  *Year = T.Year;
  *Month = T.Month;
  *Day = T.Day;
  *Hour = T.Hour;
  *Min = T.Minute;
  *Sec = T.Second;
}

//
// ---- screen ----
//
STATIC
VOID
DosDraw(X86 *C, BOOLEAN Force)
{
  UINTN    Row;
  UINTN    Col;
  UINTN    Start;
  UINTN    N;
  UINT32   A;
  UINT16   Cell;
  UINT8    Attr;
  CHAR16   Line[84];
  UINT8    CurRow = C->Mem[BDA(0x51)];
  UINT8    CurCol = C->Mem[BDA(0x50)];
  UINT16   Shape = M16(C, BDA(0x60));

  for (Row = 0; Row < 25; Row++) {
    Col = 0;
    while (Col < 80) {
      if (Row == 24 && Col == 79) {
        break;                                   // writing the last cell would scroll the console
      }
      A = VRAM_TEXT + (UINT32)(Row * 80 + Col) * 2;
      Cell = (UINT16)(C->Mem[A] | ((UINT16)C->Mem[A + 1] << 8));
      if (!Force && Cell == gDosShadow[Row][Col]) {
        Col++;
        continue;
      }

      Attr = C->Mem[A + 1];
      Start = Col;
      N = 0;
      while (Col < 80 && !(Row == 24 && Col == 79)) {
        A = VRAM_TEXT + (UINT32)(Row * 80 + Col) * 2;
        Cell = (UINT16)(C->Mem[A] | ((UINT16)C->Mem[A + 1] << 8));
        if (C->Mem[A + 1] != Attr || (!Force && Cell == gDosShadow[Row][Col])) {
          break;
        }
        Line[N++] = gCp437[C->Mem[A]];
        gDosShadow[Row][Col] = Cell;
        Col++;
      }
      Line[N] = L'\0';

      gST->ConOut->SetCursorPosition(gST->ConOut, Start, Row);
      gST->ConOut->SetAttribute(gST->ConOut, (UINTN)((Attr & 0x0F) | (((UINTN)Attr >> 4) & 0x07) << 4));
      gST->ConOut->OutputString(gST->ConOut, Line);
    }
  }

  if (CurRow < 25 && CurCol < 80) {
    gST->ConOut->SetCursorPosition(gST->ConOut, CurCol, CurRow);
  }
  gST->ConOut->EnableCursor(gST->ConOut, (BOOLEAN)((Shape & 0x2000) == 0));
}

//
// ---- keyboard ----
//
STATIC
BOOLEAN
DosReadKey(EFI_INPUT_KEY *K, UINT32 *Shift)
{
  EFI_KEY_DATA  Kd;

  *Shift = 0;

  if (gDosKeyEx != NULL) {
    if (gBS->CheckEvent(gDosKeyEx->WaitForKeyEx) != EFI_SUCCESS) {
      return FALSE;
    }
    if (EFI_ERROR(gDosKeyEx->ReadKeyStrokeEx(gDosKeyEx, &Kd))) {
      return FALSE;
    }
    *K = Kd.Key;
    if ((Kd.KeyState.KeyShiftState & EFI_SHIFT_STATE_VALID) != 0) {
      *Shift = Kd.KeyState.KeyShiftState;
    }
    return TRUE;
  }

  if (gBS->CheckEvent(gST->ConIn->WaitForKey) != EFI_SUCCESS) {
    return FALSE;
  }
  if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, K))) {
    return FALSE;
  }
  return TRUE;
}

STATIC
BOOLEAN
DosTranslate(EFI_INPUT_KEY *K, UINT32 Shift, UINT8 *Ascii, UINT8 *Scan)
{
  BOOLEAN  Alt = (BOOLEAN)((Shift & (EFI_RIGHT_ALT_PRESSED | EFI_LEFT_ALT_PRESSED)) != 0);
  BOOLEAN  Ctl = (BOOLEAN)((Shift & (EFI_RIGHT_CONTROL_PRESSED | EFI_LEFT_CONTROL_PRESSED)) != 0);
  BOOLEAN  Sh = (BOOLEAN)((Shift & (EFI_RIGHT_SHIFT_PRESSED | EFI_LEFT_SHIFT_PRESSED)) != 0);
  CHAR16   U = K->UnicodeChar;
  UINT8    Base;

  *Ascii = 0;
  *Scan = 0;

  if (U == 0) {
    switch (K->ScanCode) {
      case SCAN_UP:         *Scan = 0x48; break;
      case SCAN_DOWN:       *Scan = 0x50; break;
      case SCAN_RIGHT:      *Scan = 0x4D; break;
      case SCAN_LEFT:       *Scan = 0x4B; break;
      case SCAN_HOME:       *Scan = 0x47; break;
      case SCAN_END:        *Scan = 0x4F; break;
      case SCAN_INSERT:     *Scan = 0x52; break;
      case SCAN_DELETE:     *Scan = 0x53; break;
      case SCAN_PAGE_UP:    *Scan = 0x49; break;
      case SCAN_PAGE_DOWN:  *Scan = 0x51; break;
      case SCAN_ESC:
        *Ascii = 0x1B;
        *Scan = 0x01;
        break;
      default:
        if (K->ScanCode >= SCAN_F1 && K->ScanCode <= SCAN_F10) {
          Base = Alt ? 0x68 : (Ctl ? 0x5E : (Sh ? 0x54 : 0x3B));
          *Scan = (UINT8)(Base + (K->ScanCode - SCAN_F1));
        } else if (K->ScanCode == SCAN_F11) {
          *Scan = Alt ? 0x8B : (Ctl ? 0x89 : (Sh ? 0x87 : 0x85));
        } else {
          return FALSE;
        }
        break;
    }
    return TRUE;
  }

  if (U > 127) {
    return FALSE;
  }

  *Ascii = (UINT8)U;
  *Scan = gScanOf[U];

  if (Alt && *Scan != 0) {
    *Ascii = 0;
    if (U >= L'1' && U <= L'9') {
      *Scan = (UINT8)(0x78 + (U - L'1'));
    } else if (U == L'0') {
      *Scan = 0x81;
    }
  }
  return TRUE;
}

STATIC
VOID
EFIAPI
DosTickNotify(IN EFI_EVENT Event, IN VOID *Context)
{
  UNREFERENCED_PARAMETER(Event);
  UNREFERENCED_PARAMETER(Context);
  gDosTicks++;
}

//
// Runs the emulated PC until F12, INT 19h, or a fatal condition.
//
STATIC
VOID
DosRun(VOID)
{
  EFI_INPUT_KEY  Key;
  UINT32         Shift;
  UINT8          Ascii;
  UINT8          Scan;
  UINT8          Flags;
  UINTN          LastTicks = gDosTicks;
  UINTN          Now;
  UINTN          Due;
  UINT32         N;
  BOOLEAN        Dirty;

  for (;;) {
    N = X86Run(&gDos, 30000);
    Dirty = FALSE;

    Now = gDosTicks;
    Due = Now - LastTicks;
    LastTicks = Now;
    if (Due > 3) {
      Due = 3;
    }
    if (Due > 0) {
      Dirty = TRUE;
    }
    while (Due > 0) {
      X86Irq0(&gDos);
      Due--;
    }

    while (DosReadKey(&Key, &Shift)) {
      if (Key.UnicodeChar == 0 && Key.ScanCode == SCAN_F12) {
        return;
      }
      if (Shift != 0 || gDosKeyEx != NULL) {
        Flags = 0;
        if (Shift & EFI_RIGHT_SHIFT_PRESSED)   Flags |= 0x01;
        if (Shift & EFI_LEFT_SHIFT_PRESSED)    Flags |= 0x02;
        if (Shift & (EFI_RIGHT_CONTROL_PRESSED | EFI_LEFT_CONTROL_PRESSED)) Flags |= 0x04;
        if (Shift & (EFI_RIGHT_ALT_PRESSED | EFI_LEFT_ALT_PRESSED))         Flags |= 0x08;
        gDos.Mem[BDA(0x17)] = Flags;
      }
      if (DosTranslate(&Key, Shift, &Ascii, &Scan)) {
        X86KeyPush(&gDos, Ascii, Scan);
        Dirty = TRUE;
      }
    }

    if (Dirty) {
      DosDraw(&gDos, FALSE);
    }

    if (gDos.Reboot || gDos.Dead) {
      DosDraw(&gDos, FALSE);
      return;
    }
    if (N < 30000) {
      gBS->Stall(1000);                          // waiting for a key or an interrupt
    }
  }
}

//
// Finds physical disk number N, numbered exactly like the DISKS command.
//
STATIC
EFI_BLOCK_IO_PROTOCOL *
FindPhysicalDisk(UINTN N)
{
  EFI_HANDLE             *Handles = NULL;
  UINTN                  Count = 0;
  UINTN                  I;
  UINTN                  Disk = 0;
  EFI_BLOCK_IO_PROTOCOL  *Bio;
  EFI_BLOCK_IO_PROTOCOL  *Result = NULL;

  if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    return NULL;
  }

  for (I = 0; I < Count; I++) {
    if (EFI_ERROR(gBS->HandleProtocol(Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Bio))) {
      continue;
    }
    if (Bio->Media == NULL || !Bio->Media->MediaPresent || Bio->Media->BlockSize == 0 || Bio->Media->LogicalPartition) {
      continue;
    }
    if (Disk == N) {
      Result = Bio;
      break;
    }
    Disk++;
  }

  gBS->FreePool(Handles);
  return Result;
}

STATIC
BOOLEAN
IsFloppySize(UINT64 Bytes)
{
  return (BOOLEAN)(Bytes == 163840 || Bytes == 184320 || Bytes == 327680 || Bytes == 368640 ||
                   Bytes == 737280 || Bytes == 1228800 || Bytes == 1474560 || Bytes == 2949120);
}

STATIC
VOID
CmdDos(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *F;
  EFI_FILE_INFO      *Info;
  EFI_STATUS         Status;
  BOOLEAN            Ro = FALSE;
  BOOLEAN            BootFloppy = FALSE;
  UINTN              Floppies = 0;
  UINTN              Hdds = 0;
  UINTN              I;
  UINTN              Slot;
  UINT8              Boot;
  UINTN              OrigMode;
  UINTN              Mode;
  UINTN              Cols;
  UINTN              Rows;
  UINTN              Pick;
  BOOLEAN            Found;
  EFI_EVENT          Timer = NULL;
  UINT64             Sizes[4];
  UINTN              Any = 0;
  BOOLEAN            Physical = FALSE;

  if (gIdleHook != NULL) {
    Print(L"Leave the desktop first (F10), then run DOS from the text shell.\r\n");
    return;
  }

  for (I = 0; I < 4; I++) {
    gDosImg[I] = NULL;
    gDosBio[I] = NULL;
    Sizes[I] = 0;
  }
  gDos.Mem = NULL;
  gDos.Xms = NULL;

  if (!NeedVolume()) {
    return;
  }

  while (NextToken(&C, Arg, MAX_PATH)) {
    if (SameText(Arg, L"/RO")) {
      Ro = TRUE;
      continue;
    }
    if (SameText(Arg, L"/A")) {
      BootFloppy = TRUE;
      continue;
    }

    if (Arg[0] == L'#') {
      UINTN                  Num = 0;
      UINTN                  K;
      EFI_BLOCK_IO_PROTOCOL  *Bio;
      UINT64                 Bytes;

      for (K = 1; Arg[K] >= L'0' && Arg[K] <= L'9'; K++) {
        Num = Num * 10 + (UINTN)(Arg[K] - L'0');
      }
      Bio = FindPhysicalDisk(Num);
      if (Bio == NULL || Arg[1] == L'\0' || Arg[K] != L'\0') {
        Print(L"Physical disk %s not found. Use DISKS to see the numbers.\r\n", Arg);
        goto Cleanup;
      }
      Bytes = (Bio->Media->LastBlock + 1) * (UINT64)Bio->Media->BlockSize;
      if (IsFloppySize(Bytes)) {
        Slot = Floppies;
        Floppies++;
      } else {
        Slot = 2 + Hdds;
        Hdds++;
      }
      if (Floppies > 2 || Hdds > 2) {
        Print(L"Too many images (up to 2 floppies and 2 hard disks).\r\n");
        goto Cleanup;
      }
      gDosBio[Slot] = Bio;
      Sizes[Slot] = Bytes;
      if (Bio->Media->ReadOnly) {
        Ro = TRUE;
      }
      Physical = TRUE;
      Any++;
      continue;
    }

    Status = OpenPath(Arg, Ro ? EFI_FILE_MODE_READ : (EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE), 0, &F);
    if (EFI_ERROR(Status) && !Ro) {
      Status = OpenPath(Arg, EFI_FILE_MODE_READ, 0, &F);       // read-only media
      if (!EFI_ERROR(Status)) {
        Print(L"Note: %s opened read-only.\r\n", Arg);
        Ro = TRUE;
      }
    }
    if (EFI_ERROR(Status)) {
      NotFound(Arg);
      goto Cleanup;
    }

    Info = GetFileInfo(F);
    if (Info == NULL || (Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
      Print(L"%s is not a disk image file.\r\n", Arg);
      if (Info != NULL) {
        gBS->FreePool(Info);
      }
      F->Close(F);
      goto Cleanup;
    }

    if (IsFloppySize(Info->FileSize)) {
      Slot = Floppies;
      Floppies++;
    } else {
      Slot = 2 + Hdds;
      Hdds++;
    }
    if (Floppies > 2 || Hdds > 2) {
      Print(L"Too many images (up to 2 floppies and 2 hard disks).\r\n");
      gBS->FreePool(Info);
      F->Close(F);
      goto Cleanup;
    }

    gDosImg[Slot] = F;
    Sizes[Slot] = Info->FileSize;
    gBS->FreePool(Info);
    Any++;
  }

  if (Any == 0) {
    Print(L"\r\nUsage: DOS <disk-image or #N> [second] [/RO] [/A]\r\n");
    Print(L"  #N = physical disk N as listed by DISKS (best for big images / real disks)\r\n");
    Print(L"  Runs a real MS-DOS / FreeDOS disk image inside AIOS (a 286-class PC).\r\n");
    Print(L"  Floppy-sized images become A:/B:, larger images become hard disk C:/D:.\r\n");
    Print(L"  /RO = never write to the images   /A = boot the floppy   F12 = leave\r\n");
    Print(L"  Make a RAW image from a VMware disk with:  qemu-img convert -O raw disk.vmdk disk.img\r\n\r\n");
    return;
  }

  // writing straight to a physical disk deserves a confirmation
  if (Physical && !Ro) {
    EFI_INPUT_KEY  Key;

    SetColor(FgAttr(EFI_YELLOW));
    Print(L"\r\nWARNING: DOS will be able to WRITE directly to a physical disk.\r\n");
    Print(L"Use /RO to protect it, or make a backup first. Continue? [y/N] ");
    SetColor(gAttr);
    if (EFI_ERROR(NextKey(&Key)) || (Key.UnicodeChar != L'y' && Key.UnicodeChar != L'Y')) {
      Print(L"N\r\n");
      goto Cleanup;
    }
    Print(L"Y\r\n");
  }

  // memory
  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, X86_MEM_SIZE, (VOID **)&gDos.Mem)) ||
      EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, DOS_XMS_BYTES, (VOID **)&gDos.Xms))) {
    Print(L"Not enough memory to start the emulator.\r\n");
    goto Cleanup;
  }
  SetMem(gDos.Mem, X86_MEM_SIZE, 0);
  SetMem(gDos.Xms, DOS_XMS_BYTES, 0);
  gDos.XmsSize = DOS_XMS_BYTES;
  gDos.Insn = 0;
  for (I = 0; I < 4; I++) {
    gDos.Disk[I].Present = FALSE;
    gDos.Disk[I].ReadOnly = Ro;
  }

  X86Tables();
  for (I = 0; I < 4; I++) {
    if (gDosImg[I] != NULL || gDosBio[I] != NULL) {
      DiskSetup(&gDos, I, Sizes[I] / 512);
      gDos.Disk[I].ReadOnly = Ro;
    }
  }
  BiosInit(&gDos, TRUE);

  Boot = (UINT8)((Hdds > 0 && !BootFloppy) ? 0x80 : 0x00);
  if (!BiosBoot(&gDos, Boot)) {
    Print(L"Cannot boot: the first sector of the %s is not a valid boot sector.\r\n",
          Boot == 0x80 ? L"hard disk image" : L"floppy image");
    goto Cleanup;
  }

  Print(L"\r\nStarting DOS from %s  (F12 = leave the emulator)\r\n",
        Boot == 0x80 ? L"hard disk C:" : L"floppy A:");
  gBS->Stall(900000);

  // keyboard with shift state, if the firmware has it
  gDosKeyEx = NULL;
  if (EFI_ERROR(gBS->HandleProtocol(gST->ConsoleInHandle, &gEfiSimpleTextInputExProtocolGuid, (VOID **)&gDosKeyEx))) {
    gDosKeyEx = NULL;
  }

  // 80x25 console
  OrigMode = (UINTN)gST->ConOut->Mode->Mode;
  Pick = OrigMode;
  Found = FALSE;
  for (Mode = 0; Mode < (UINTN)gST->ConOut->Mode->MaxMode; Mode++) {
    if (!EFI_ERROR(gST->ConOut->QueryMode(gST->ConOut, Mode, &Cols, &Rows)) && Cols == 80 && Rows == 25) {
      Pick = Mode;
      Found = TRUE;
      break;
    }
  }
  if (!Found) {
    for (Mode = 0; Mode < (UINTN)gST->ConOut->Mode->MaxMode; Mode++) {
      if (!EFI_ERROR(gST->ConOut->QueryMode(gST->ConOut, Mode, &Cols, &Rows)) && Cols >= 80 && Rows >= 25) {
        Pick = Mode;
        break;
      }
    }
  }
  if (Pick != OrigMode) {
    gST->ConOut->SetMode(gST->ConOut, Pick);
  }
  gST->ConOut->SetAttribute(gST->ConOut, EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK));
  gST->ConOut->ClearScreen(gST->ConOut);
  SetMem(gDosShadow, sizeof(gDosShadow), 0xFF);

  gDosLogN = 0;
  gDosTicks = 0;
  if (!EFI_ERROR(gBS->CreateEvent(EVT_TIMER | EVT_NOTIFY_SIGNAL, TPL_CALLBACK, DosTickNotify, NULL, &Timer))) {
    gBS->SetTimer(Timer, TimerPeriodic, 549451);        // 18.2 times a second
  }

  DosDraw(&gDos, TRUE);
  DosRun();

  if (Timer != NULL) {
    gBS->SetTimer(Timer, TimerCancel, 0);
    gBS->CloseEvent(Timer);
  }

  gST->ConOut->EnableCursor(gST->ConOut, TRUE);
  if (Pick != OrigMode) {
    gST->ConOut->SetMode(gST->ConOut, OrigMode);
  }
  gST->ConOut->SetAttribute(gST->ConOut, gAttr);
  gST->ConOut->ClearScreen(gST->ConOut);

  if (gDos.Reboot) {
    Print(L"DOS requested a reboot (INT 19h). Emulator stopped.\r\n");
  } else if (gDos.Dead) {
    Print(L"The emulated PC stopped.\r\n");
  } else {
    Print(L"Left the DOS emulator.\r\n");
  }

  if (gDosLogN > 0) {
    UINTN  First = (gDosLogN > 16) ? gDosLogN - 16 : 0;

    Print(L"\r\nEmulator notes (these help me fix compatibility problems):\r\n");
    for (I = First; I < gDosLogN; I++) {
      Print(L"  %a  [%X %X]\r\n", gDosLog[I % 16], gDosLogA[I % 16], gDosLogB[I % 16]);
    }
  }

Cleanup:
  for (I = 0; I < 4; I++) {
    if (gDosImg[I] != NULL) {
      gDosImg[I]->Close(gDosImg[I]);
      gDosImg[I] = NULL;
    }
  }
  if (gDos.Mem != NULL) {
    gBS->FreePool(gDos.Mem);
    gDos.Mem = NULL;
  }
  if (gDos.Xms != NULL) {
    gBS->FreePool(gDos.Xms);
    gDos.Xms = NULL;
  }
}

#if defined (_MSC_VER)
#pragma warning(pop)
#endif
