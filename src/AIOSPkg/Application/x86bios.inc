//
// =====================================================================
//  AIOS DOS emulator - software BIOS, XMS memory manager, boot logic
//  (host-agnostic; include after x86cpu.inc)
//
//  BIOS services are "high-level emulated": the interrupt vectors point
//  at tiny stubs in the F000 segment, and when the CPU reaches a stub the
//  emulator runs the service in C and then returns (IRET).
// =====================================================================
//

#define BIOS_SEG      0xF000
#define STUB_BASE     0xE000                  // vector n stub at F000:E000 + n*4
#define STUB_SPAN     0x0400
#define XMS_ENTRY     0xE500                  // XMS driver far-call entry
#define TICK_END      0xE510                  // continuation after the user's INT 1Ch
#define DISK_PARAMS   0xEFC7                  // diskette parameter table

#define BDA(O)        (0x400u + (O))
#define VRAM_TEXT     0xB8000u

STATIC UINT8  gHleVec[256];

//
// ---------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------
//
STATIC
VOID
SetStackedFlags(X86 *C, UINT16 Mask, UINT16 Val)
{
  UINT16  F = Rd16(C, C->S[S_SS], (UINT16)(C->R[R_SP] + 4));

  F = (UINT16)((F & ~Mask) | (Val & Mask));
  Wr16(C, C->S[S_SS], (UINT16)(C->R[R_SP] + 4), F);
}

STATIC
VOID
RetCF(X86 *C, BOOLEAN Set)
{
  SetStackedFlags(C, F_CF, Set ? F_CF : 0);
}

STATIC
VOID
RetZF(X86 *C, BOOLEAN Set)
{
  SetStackedFlags(C, F_ZF, Set ? F_ZF : 0);
}

STATIC
UINT8
HiOf(UINT16 V)
{
  return (UINT8)(V >> 8);
}

STATIC VOID SetAH(X86 *C, UINT8 V) { *R8p(C, 4) = V; }
STATIC VOID SetAL(X86 *C, UINT8 V) { *R8p(C, 0) = V; }
STATIC UINT8 GetAH(X86 *C) { return *R8p(C, 4); }
STATIC UINT8 GetAL(X86 *C) { return *R8p(C, 0); }

STATIC
VOID
MemFill(UINT8 *P, UINT8 V, UINT32 N)
{
  UINT32  I;

  for (I = 0; I < N; I++) {
    P[I] = V;
  }
}

//
// ---------------------------------------------------------------
// Video (text modes only)
// ---------------------------------------------------------------
//
STATIC
VOID
VidCell(X86 *C, UINT32 Row, UINT32 Col, UINT8 Ch, UINT8 Attr, BOOLEAN SetAttr)
{
  UINT32  A = VRAM_TEXT + (Row * 80 + Col) * 2;

  C->Mem[A] = Ch;
  if (SetAttr) {
    C->Mem[A + 1] = Attr;
  }
}

STATIC
VOID
VidClear(X86 *C, UINT8 Attr)
{
  UINT32  I;

  for (I = 0; I < 80 * 25; I++) {
    C->Mem[VRAM_TEXT + I * 2] = ' ';
    C->Mem[VRAM_TEXT + I * 2 + 1] = Attr;
  }
}

STATIC
VOID
VidScroll(X86 *C, BOOLEAN Up, UINT32 Lines, UINT8 Attr, UINT32 Top, UINT32 Left, UINT32 Bottom, UINT32 Right)
{
  UINT32  R;
  UINT32  Col;
  UINT32  Height;

  if (Bottom > 24) Bottom = 24;
  if (Right > 79) Right = 79;
  if (Top > Bottom || Left > Right) {
    return;
  }

  Height = Bottom - Top + 1;
  if (Lines == 0 || Lines >= Height) {
    for (R = Top; R <= Bottom; R++) {
      for (Col = Left; Col <= Right; Col++) {
        VidCell(C, R, Col, ' ', Attr, TRUE);
      }
    }
    return;
  }

  if (Up) {
    for (R = Top; R + Lines <= Bottom; R++) {
      for (Col = Left; Col <= Right; Col++) {
        UINT32  S = VRAM_TEXT + ((R + Lines) * 80 + Col) * 2;
        UINT32  D = VRAM_TEXT + (R * 80 + Col) * 2;
        C->Mem[D] = C->Mem[S];
        C->Mem[D + 1] = C->Mem[S + 1];
      }
    }
    for (R = Bottom + 1 - Lines; R <= Bottom; R++) {
      for (Col = Left; Col <= Right; Col++) {
        VidCell(C, R, Col, ' ', Attr, TRUE);
      }
    }
  } else {
    for (R = Bottom; R >= Top + Lines; R--) {
      for (Col = Left; Col <= Right; Col++) {
        UINT32  S = VRAM_TEXT + ((R - Lines) * 80 + Col) * 2;
        UINT32  D = VRAM_TEXT + (R * 80 + Col) * 2;
        C->Mem[D] = C->Mem[S];
        C->Mem[D + 1] = C->Mem[S + 1];
      }
      if (R == 0) break;
    }
    for (R = Top; R < Top + Lines; R++) {
      for (Col = Left; Col <= Right; Col++) {
        VidCell(C, R, Col, ' ', Attr, TRUE);
      }
    }
  }
}

STATIC
VOID
CursorGet(X86 *C, UINT32 *Row, UINT32 *Col)
{
  *Col = C->Mem[BDA(0x50)];
  *Row = C->Mem[BDA(0x51)];
}

STATIC
VOID
CursorSet(X86 *C, UINT32 Row, UINT32 Col)
{
  C->Mem[BDA(0x50)] = (UINT8)Col;
  C->Mem[BDA(0x51)] = (UINT8)Row;
}

STATIC
VOID
VidTty(X86 *C, UINT8 Ch)
{
  UINT32  Row;
  UINT32  Col;

  CursorGet(C, &Row, &Col);

  switch (Ch) {
    case 0x07:
      break;
    case 0x08:
      if (Col > 0) Col--;
      break;
    case 0x0A:
      Row++;
      break;
    case 0x0D:
      Col = 0;
      break;
    default:
      VidCell(C, Row, Col, Ch, 0, FALSE);
      Col++;
      if (Col >= 80) {
        Col = 0;
        Row++;
      }
      break;
  }

  if (Row > 24) {
    VidScroll(C, TRUE, 1, 0x07, 0, 0, 24, 79);
    Row = 24;
  }
  CursorSet(C, Row, Col);
}

STATIC
VOID
SetVideoMode(X86 *C, UINT8 Mode)
{
  UINT8  M = (UINT8)(Mode & 0x7F);

  if (M <= 3 || M == 7) {
    C->Mem[BDA(0x49)] = (M == 7) ? 7 : M;
    if ((Mode & 0x80) == 0) {
      VidClear(C, 0x07);
    }
    CursorSet(C, 0, 0);
  } else {
    HostLog((CONST CHAR8 *)"graphics video mode requested (unsupported)", M, 0);
  }
}

STATIC
VOID
BiosVideo(X86 *C)
{
  UINT8   Ah = GetAH(C);
  UINT8   Al = GetAL(C);
  UINT32  Row;
  UINT32  Col;
  UINT32  N;
  UINT32  I;
  UINT8   Bh = *R8p(C, 7);
  UINT8   Bl = *R8p(C, 3);
  UINT8   Ch = *R8p(C, 5);
  UINT8   Cl = *R8p(C, 1);
  UINT8   Dh = *R8p(C, 6);
  UINT8   Dl = *R8p(C, 2);

  switch (Ah) {
    case 0x00:
      SetVideoMode(C, Al);
      break;
    case 0x01:
      SM16(C, BDA(0x60), C->R[R_CX]);
      break;
    case 0x02:
      CursorSet(C, Dh, Dl);
      break;
    case 0x03:
      C->R[R_DX] = (UINT16)((C->Mem[BDA(0x51)] << 8) | C->Mem[BDA(0x50)]);
      C->R[R_CX] = M16(C, BDA(0x60));
      break;
    case 0x05:
      C->Mem[BDA(0x62)] = Al;
      break;
    case 0x06:
    case 0x07:
      VidScroll(C, (BOOLEAN)(Ah == 0x06), Al, Bh, Ch, Cl, Dh, Dl);
      break;
    case 0x08:
      CursorGet(C, &Row, &Col);
      SetAL(C, C->Mem[VRAM_TEXT + (Row * 80 + Col) * 2]);
      SetAH(C, C->Mem[VRAM_TEXT + (Row * 80 + Col) * 2 + 1]);
      break;
    case 0x09:
    case 0x0A:
      CursorGet(C, &Row, &Col);
      N = C->R[R_CX];
      for (I = 0; I < N && Row < 25; I++) {
        VidCell(C, Row, Col, Al, Bl, (BOOLEAN)(Ah == 0x09));
        Col++;
        if (Col >= 80) {
          Col = 0;
          Row++;
        }
      }
      break;
    case 0x0E:
      VidTty(C, Al);
      break;
    case 0x0F:
      SetAL(C, C->Mem[BDA(0x49)]);
      SetAH(C, 80);
      *R8p(C, 7) = 0;
      break;
    case 0x10:
    case 0x11:
      break;
    case 0x12:
      if (Bl == 0x10) {
        *R8p(C, 7) = 0;                       // color
        *R8p(C, 3) = 3;                       // 256K
        C->R[R_CX] = 0x0009;
      }
      SetAL(C, 0x12);
      break;
    case 0x13:
      Row = Dh;
      Col = Dl;
      N = C->R[R_CX];
      {
        UINT16  Bp = C->R[R_BP];
        UINT32  Pos = 0;

        for (I = 0; I < N; I++) {
          UINT8  Chr = Rd8(C, C->S[S_ES], (UINT16)(Bp + Pos));
          UINT8  At = Bl;

          Pos++;
          if (Al & 2) {
            At = Rd8(C, C->S[S_ES], (UINT16)(Bp + Pos));
            Pos++;
          }
          if (Row < 25 && Col < 80) {
            VidCell(C, Row, Col, Chr, At, TRUE);
          }
          Col++;
          if (Col >= 80) {
            Col = 0;
            Row++;
          }
        }
      }
      if (Al & 1) {
        CursorSet(C, Row, Col);
      }
      break;
    case 0x1A:
      if (Al == 0) {
        SetAL(C, 0x1A);
        *R8p(C, 3) = 0x08;                    // VGA with color display
        *R8p(C, 7) = 0;
      }
      break;
    default:
      break;
  }
}

//
// ---------------------------------------------------------------
// Disks
// ---------------------------------------------------------------
//
STATIC
VOID
DiskSetup(X86 *C, UINTN Idx, UINT64 Sectors)
{
  X86DISK  *D = &C->Disk[Idx];
  UINT8    Mbr[512];
  UINT32   I;
  UINT32   Heads = 0;
  UINT32   Spt = 0;
  UINT64   Cyls;

  D->Present = TRUE;
  D->Sectors = Sectors;
  D->IsHdd = (BOOLEAN)(Idx >= 2);

  if (!D->IsHdd) {
    switch (Sectors) {
      case 320:   D->Cyls = 40; D->Heads = 1; D->Spt = 8;  break;
      case 360:   D->Cyls = 40; D->Heads = 1; D->Spt = 9;  break;
      case 640:   D->Cyls = 40; D->Heads = 2; D->Spt = 8;  break;
      case 720:   D->Cyls = 40; D->Heads = 2; D->Spt = 9;  break;
      case 1440:  D->Cyls = 80; D->Heads = 2; D->Spt = 9;  break;
      case 2400:  D->Cyls = 80; D->Heads = 2; D->Spt = 15; break;
      case 5760:  D->Cyls = 80; D->Heads = 2; D->Spt = 36; break;
      default:    D->Cyls = 80; D->Heads = 2; D->Spt = 18; break;
    }
    return;
  }

  // Hard disk: take the geometry from the partition table so that DOS
  // sees the same CHS values the partitions were created with.
  if (HostDiskRead(Idx, 0, 1, Mbr) && Mbr[510] == 0x55 && Mbr[511] == 0xAA) {
    for (I = 0; I < 4; I++) {
      UINT8  *E = &Mbr[0x1BE + I * 16];
      if (E[4] != 0) {
        UINT32  H = (UINT32)E[5] + 1;
        UINT32  S = E[6] & 0x3F;
        if (S >= 1 && S <= 63 && H >= 1 && H <= 256) {
          Heads = H;
          Spt = S;
          break;
        }
      }
    }
  }
  if (Heads == 0 || Spt == 0) {
    Heads = 16;
    Spt = 63;
  }

  Cyls = Sectors / ((UINT64)Heads * Spt);
  if (Cyls > 1024) Cyls = 1024;
  if (Cyls == 0) Cyls = 1;
  D->Heads = Heads;
  D->Spt = Spt;
  D->Cyls = (UINT32)Cyls;
}

STATIC
X86DISK *
DiskFor(X86 *C, UINT8 Dl)
{
  UINTN  Idx;

  if (Dl < 2) {
    Idx = Dl;
  } else if (Dl >= 0x80 && Dl < 0x82) {
    Idx = 2 + (Dl - 0x80);
  } else {
    return NULL;
  }
  return C->Disk[Idx].Present ? &C->Disk[Idx] : NULL;
}

STATIC
UINTN
DiskIndex(X86 *C, X86DISK *D)
{
  return (UINTN)(D - C->Disk);
}

STATIC
VOID
BiosDisk(X86 *C)
{
  UINT8     Ah = GetAH(C);
  UINT8     Dl = *R8p(C, 2);
  X86DISK   *D = DiskFor(C, Dl);
  UINT8     Status = 0;
  UINT32    Count = GetAL(C);
  UINT32    Cyl;
  UINT32    Head;
  UINT32    Sec;
  UINT64    Lba;
  UINT32    I;
  UINT16    Off;
  UINT8     Buf[512];
  UINT32    Done = 0;

  switch (Ah) {
    case 0x00:                                           // reset
    case 0x0C:                                           // seek
    case 0x0D:
    case 0x10:
    case 0x11:
    case 0x14:
    case 0x09:
    case 0x16:
    case 0x17:
    case 0x18:
      Status = (D != NULL || Ah == 0x00) ? 0 : 0x80;
      break;

    case 0x01:                                           // get status
      SetAL(C, C->DiskStatus);
      SetAH(C, C->DiskStatus);
      RetCF(C, (BOOLEAN)(C->DiskStatus != 0));
      return;

    case 0x02:                                           // read sectors
    case 0x03:                                           // write sectors
    case 0x04:                                           // verify
      if (D == NULL) {
        Status = 0x80;
        break;
      }
      Cyl = ((UINT32)(C->R[R_CX] & 0xC0) << 2) | HiOf(C->R[R_CX]);
      Sec = C->R[R_CX] & 0x3F;
      Head = *R8p(C, 6);
      if (Sec == 0 || Sec > D->Spt || Head >= D->Heads || Cyl >= D->Cyls) {
        Status = 0x04;
        break;
      }
      Lba = ((UINT64)Cyl * D->Heads + Head) * D->Spt + (Sec - 1);
      if (Lba + Count > D->Sectors) {
        Status = 0x04;
        break;
      }
      if (Ah == 0x04) {
        Done = Count;
        break;
      }
      for (I = 0; I < Count; I++) {
        Off = (UINT16)(C->R[R_BX] + I * 512);
        if (Ah == 0x02) {
          UINT32  K;

          if (!HostDiskRead(DiskIndex(C, D), Lba + I, 1, Buf)) {
            Status = 0x20;
            break;
          }
          for (K = 0; K < 512; K++) {
            Wr8(C, C->S[S_ES], (UINT16)(Off + K), Buf[K]);
          }
        } else {
          UINT32  K;

          if (D->ReadOnly) {
            Status = 0x03;
            break;
          }
          for (K = 0; K < 512; K++) {
            Buf[K] = Rd8(C, C->S[S_ES], (UINT16)(Off + K));
          }
          if (!HostDiskWrite(DiskIndex(C, D), Lba + I, 1, Buf)) {
            Status = 0x20;
            break;
          }
        }
        Done++;
      }
      break;

    case 0x05:                                           // format track
      Status = (D != NULL) ? 0 : 0x80;
      break;

    case 0x08:                                           // get drive parameters
      if (D == NULL) {
        Status = 0x07;
        break;
      }
      {
        UINT32  MaxCyl = D->Cyls - 1;
        UINT32  Floppies = (UINT32)C->Disk[0].Present + (UINT32)C->Disk[1].Present;
        UINT32  Hdds = (UINT32)C->Disk[2].Present + (UINT32)C->Disk[3].Present;

        SetAL(C, 0);
        *R8p(C, 5) = (UINT8)MaxCyl;
        *R8p(C, 1) = (UINT8)(D->Spt | ((MaxCyl >> 2) & 0xC0));
        *R8p(C, 6) = (UINT8)(D->Heads - 1);
        *R8p(C, 2) = (UINT8)(D->IsHdd ? Hdds : Floppies);
        if (!D->IsHdd) {
          UINT8  Type = 4;
          if (D->Spt == 9 && D->Cyls == 40) Type = 1;
          else if (D->Spt == 15) Type = 2;
          else if (D->Spt == 9) Type = 3;
          else if (D->Spt == 36) Type = 6;
          *R8p(C, 3) = Type;
          C->S[S_ES] = BIOS_SEG;
          C->R[R_DI] = DISK_PARAMS;
        }
      }
      Status = 0;
      break;

    case 0x15:                                           // get disk type
      if (D == NULL) {
        SetAH(C, 0);
        RetCF(C, FALSE);
        return;
      }
      if (D->IsHdd) {
        SetAH(C, 3);
        C->R[R_CX] = (UINT16)(D->Sectors >> 16);
        C->R[R_DX] = (UINT16)D->Sectors;
      } else {
        SetAH(C, 1);
      }
      RetCF(C, FALSE);
      return;

    case 0x41:                                           // INT 13h extensions: not supported
      SetAH(C, 1);
      RetCF(C, TRUE);
      return;

    default:
      Status = 0x01;
      break;
  }

  C->DiskStatus = Status;
  SetAH(C, Status);
  if (Status == 0 && (Ah == 0x02 || Ah == 0x03 || Ah == 0x04)) {
    SetAL(C, (UINT8)Done);
  }
  RetCF(C, (BOOLEAN)(Status != 0));
}

//
// ---------------------------------------------------------------
// Keyboard
// ---------------------------------------------------------------
//
STATIC
BOOLEAN
KeyAvail(X86 *C)
{
  return (BOOLEAN)(M16(C, BDA(0x1A)) != M16(C, BDA(0x1C)));
}

STATIC
VOID
X86KeyPush(X86 *C, UINT8 Ascii, UINT8 Scan)
{
  UINT16  Head = M16(C, BDA(0x1A));
  UINT16  Tail = M16(C, BDA(0x1C));
  UINT16  Next = (UINT16)(Tail + 2);

  if (Next >= 0x3E) {
    Next = 0x1E;
  }
  if (Next == Head) {
    return;                                     // buffer full: drop
  }
  C->Mem[BDA(Tail)] = Ascii;
  C->Mem[BDA(Tail) + 1] = Scan;
  SM16(C, BDA(0x1C), Next);
}

STATIC
VOID
BiosKeyboard(X86 *C)
{
  UINT8   Ah = GetAH(C);
  UINT16  Head;
  UINT16  Key;

  switch (Ah) {
    case 0x00:
    case 0x10:
      if (!KeyAvail(C)) {
        C->WaitInput = TRUE;
        C->F |= F_IF;
        return;                                  // stay on the stub until a key arrives
      }
      Head = M16(C, BDA(0x1A));
      Key = M16(C, BDA(Head));
      Head = (UINT16)(Head + 2);
      if (Head >= 0x3E) Head = 0x1E;
      SM16(C, BDA(0x1A), Head);
      C->R[R_AX] = Key;
      break;

    case 0x01:
    case 0x11:
      if (!KeyAvail(C)) {
        RetZF(C, TRUE);
        break;
      }
      C->R[R_AX] = M16(C, BDA(M16(C, BDA(0x1A))));
      RetZF(C, FALSE);
      break;

    case 0x02:
    case 0x12:
      SetAL(C, C->Mem[BDA(0x17)]);
      if (Ah == 0x12) {
        SetAH(C, C->Mem[BDA(0x18)]);
      }
      break;

    case 0x05:
      X86KeyPush(C, *R8p(C, 1), *R8p(C, 5));
      SetAL(C, 0);
      break;

    default:
      break;
  }
}

//
// ---------------------------------------------------------------
// Time
// ---------------------------------------------------------------
//
STATIC
UINT8
ToBcd(UINT32 V)
{
  return (UINT8)(((V / 10) << 4) | (V % 10));
}

STATIC
VOID
BiosTime(X86 *C)
{
  UINT8   Ah = GetAH(C);
  UINT16  Y;
  UINT8   Mo;
  UINT8   D;
  UINT8   H;
  UINT8   Mi;
  UINT8   S;

  switch (Ah) {
    case 0x00:
      C->R[R_CX] = M16(C, BDA(0x6E));
      C->R[R_DX] = M16(C, BDA(0x6C));
      SetAL(C, C->Mem[BDA(0x70)]);
      C->Mem[BDA(0x70)] = 0;
      RetCF(C, FALSE);
      break;
    case 0x01:
      SM16(C, BDA(0x6E), C->R[R_CX]);
      SM16(C, BDA(0x6C), C->R[R_DX]);
      C->Mem[BDA(0x70)] = 0;
      SetAH(C, 0);
      RetCF(C, FALSE);
      break;
    case 0x02:
      HostTime(&Y, &Mo, &D, &H, &Mi, &S);
      *R8p(C, 5) = ToBcd(H);
      *R8p(C, 1) = ToBcd(Mi);
      *R8p(C, 6) = ToBcd(S);
      *R8p(C, 2) = 0;
      RetCF(C, FALSE);
      break;
    case 0x04:
      HostTime(&Y, &Mo, &D, &H, &Mi, &S);
      *R8p(C, 5) = ToBcd(Y / 100);
      *R8p(C, 1) = ToBcd(Y % 100);
      *R8p(C, 6) = ToBcd(Mo);
      *R8p(C, 2) = ToBcd(D);
      RetCF(C, FALSE);
      break;
    case 0x03:
    case 0x05:
      RetCF(C, FALSE);
      break;
    default:
      RetCF(C, TRUE);
      break;
  }
}

//
// ---------------------------------------------------------------
// XMS memory manager (extended memory, HMA, A20)
// ---------------------------------------------------------------
//
STATIC
UINT32
XmsFreeTotal(X86 *C)
{
  UINT32  I;
  UINT32  Used = 0;

  for (I = 1; I < X86_MAX_EMB; I++) {
    if (C->Emb[I].Used) {
      Used += C->Emb[I].Size;
    }
  }
  return (C->XmsSize > Used) ? (C->XmsSize - Used) : 0;
}

//
// Finds the lowest start offset where Size bytes fit. Returns FALSE if none.
//
STATIC
BOOLEAN
XmsFit(X86 *C, UINT32 Size, UINT32 *StartOut)
{
  UINT32   Cand[X86_MAX_EMB + 1];
  UINT32   N = 0;
  UINT32   I;
  UINT32   J;
  UINT32   Start;
  BOOLEAN  Ok;

  Cand[N++] = 0;
  for (I = 1; I < X86_MAX_EMB; I++) {
    if (C->Emb[I].Used) {
      Cand[N++] = (C->Emb[I].Start + C->Emb[I].Size + 1023) & ~1023u;
    }
  }

  for (I = 0; I < N; I++) {
    Start = Cand[I];
    if ((UINT64)Start + Size > C->XmsSize) {
      continue;
    }
    Ok = TRUE;
    for (J = 1; J < X86_MAX_EMB; J++) {
      if (C->Emb[J].Used &&
          Start < C->Emb[J].Start + C->Emb[J].Size &&
          C->Emb[J].Start < Start + Size) {
        Ok = FALSE;
        break;
      }
    }
    if (Ok) {
      if (StartOut != NULL) {
        *StartOut = Start;
      }
      return TRUE;
    }
  }
  return FALSE;
}

STATIC
UINT32
XmsLargest(X86 *C)
{
  UINT32  Lo = 0;
  UINT32  Hi = C->XmsSize / 1024;
  UINT32  Mid;

  while (Lo < Hi) {
    Mid = (Lo + Hi + 1) / 2;
    if (XmsFit(C, Mid * 1024, NULL)) {
      Lo = Mid;
    } else {
      Hi = Mid - 1;
    }
  }
  return Lo;
}

STATIC
UINT8 *
XmsPtr(X86 *C, UINT16 Handle, UINT32 Off, UINT32 Len, BOOLEAN *Ok)
{
  *Ok = FALSE;
  if (Handle == 0) {
    UINT32  A = ((Off >> 16) << 4) + (Off & 0xFFFF);

    if (!C->A20) A &= 0xFFFFF;
    if ((UINT64)A + Len > X86_MEM_SIZE) {
      return NULL;
    }
    *Ok = TRUE;
    return C->Mem + A;
  }
  if (Handle >= X86_MAX_EMB || !C->Emb[Handle].Used) {
    return NULL;
  }
  if ((UINT64)Off + Len > C->Emb[Handle].Size) {
    return NULL;
  }
  *Ok = TRUE;
  return C->Xms + C->Emb[Handle].Start + Off;
}

STATIC
VOID
XmsCall(X86 *C)
{
  UINT8   Ah = GetAH(C);
  UINT32  I;
  UINT32  Start;
  UINT32  Size;

  switch (Ah) {
    case 0x00:
      C->R[R_AX] = 0x0300;
      C->R[R_BX] = 0x0301;
      C->R[R_DX] = 1;
      break;
    case 0x01:
      if (C->HmaUsed) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0x91;
      } else {
        C->HmaUsed = TRUE;
        C->R[R_AX] = 1;
        *R8p(C, 3) = 0;
      }
      break;
    case 0x02:
      C->HmaUsed = FALSE;
      C->R[R_AX] = 1;
      *R8p(C, 3) = 0;
      break;
    case 0x03:
    case 0x05:
      C->A20 = TRUE;
      C->R[R_AX] = 1;
      *R8p(C, 3) = 0;
      break;
    case 0x04:
    case 0x06:
      C->A20 = FALSE;
      C->R[R_AX] = 1;
      *R8p(C, 3) = 0;
      break;
    case 0x07:
      C->R[R_AX] = C->A20 ? 1 : 0;
      *R8p(C, 3) = 0;
      break;
    case 0x08:
      C->R[R_AX] = (UINT16)XmsLargest(C);
      C->R[R_DX] = (UINT16)(XmsFreeTotal(C) / 1024);
      *R8p(C, 3) = (C->R[R_AX] == 0) ? 0xA0 : 0;
      break;
    case 0x09:
      Size = (UINT32)C->R[R_DX] * 1024;
      for (I = 1; I < X86_MAX_EMB && C->Emb[I].Used; I++) {
      }
      if (I >= X86_MAX_EMB) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA1;
      } else if (!XmsFit(C, Size, &Start)) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA0;
      } else {
        C->Emb[I].Used = TRUE;
        C->Emb[I].Start = Start;
        C->Emb[I].Size = Size;
        C->Emb[I].Locks = 0;
        C->R[R_AX] = 1;
        C->R[R_DX] = (UINT16)I;
        *R8p(C, 3) = 0;
      }
      break;
    case 0x0A:
      I = C->R[R_DX];
      if (I == 0 || I >= X86_MAX_EMB || !C->Emb[I].Used) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA2;
      } else {
        C->Emb[I].Used = FALSE;
        C->R[R_AX] = 1;
        *R8p(C, 3) = 0;
      }
      break;
    case 0x0B: {                                  // move extended memory block
      UINT16   Ds = C->S[S_DS];
      UINT16   Si = C->R[R_SI];
      UINT32   Len = (UINT32)Rd16(C, Ds, Si) | ((UINT32)Rd16(C, Ds, (UINT16)(Si + 2)) << 16);
      UINT16   SrcH = Rd16(C, Ds, (UINT16)(Si + 4));
      UINT32   SrcO = (UINT32)Rd16(C, Ds, (UINT16)(Si + 6)) | ((UINT32)Rd16(C, Ds, (UINT16)(Si + 8)) << 16);
      UINT16   DstH = Rd16(C, Ds, (UINT16)(Si + 10));
      UINT32   DstO = (UINT32)Rd16(C, Ds, (UINT16)(Si + 12)) | ((UINT32)Rd16(C, Ds, (UINT16)(Si + 14)) << 16);
      BOOLEAN  OkS;
      BOOLEAN  OkD;
      UINT8    *Sp = XmsPtr(C, SrcH, SrcO, Len, &OkS);
      UINT8    *Dp = XmsPtr(C, DstH, DstO, Len, &OkD);

      if (!OkS) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = (SrcH != 0 && (SrcH >= X86_MAX_EMB || !C->Emb[SrcH].Used)) ? 0xA3 : 0xA4;
      } else if (!OkD) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = (DstH != 0 && (DstH >= X86_MAX_EMB || !C->Emb[DstH].Used)) ? 0xA5 : 0xA6;
      } else {
        if (Dp <= Sp) {
          for (I = 0; I < Len; I++) Dp[I] = Sp[I];
        } else {
          for (I = Len; I > 0; I--) Dp[I - 1] = Sp[I - 1];
        }
        C->R[R_AX] = 1;
        *R8p(C, 3) = 0;
      }
      break;
    }
    case 0x0C:
      I = C->R[R_DX];
      if (I == 0 || I >= X86_MAX_EMB || !C->Emb[I].Used) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA2;
      } else {
        C->Emb[I].Locks++;
        Start = 0x100000u + C->Emb[I].Start;
        C->R[R_DX] = (UINT16)(Start >> 16);
        C->R[R_BX] = (UINT16)Start;
        C->R[R_AX] = 1;
      }
      break;
    case 0x0D:
      I = C->R[R_DX];
      if (I == 0 || I >= X86_MAX_EMB || !C->Emb[I].Used) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA2;
      } else {
        if (C->Emb[I].Locks > 0) C->Emb[I].Locks--;
        C->R[R_AX] = 1;
      }
      break;
    case 0x0E:
      I = C->R[R_DX];
      if (I == 0 || I >= X86_MAX_EMB || !C->Emb[I].Used) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA2;
      } else {
        UINT32  Freeh = 0;
        UINT32  K;

        for (K = 1; K < X86_MAX_EMB; K++) {
          if (!C->Emb[K].Used) Freeh++;
        }
        C->R[R_AX] = 1;
        *R8p(C, 7) = C->Emb[I].Locks;
        *R8p(C, 3) = (UINT8)Freeh;
        C->R[R_DX] = (UINT16)(C->Emb[I].Size / 1024);
      }
      break;
    case 0x0F:
      I = C->R[R_DX];
      Size = (UINT32)C->R[R_BX] * 1024;
      if (I == 0 || I >= X86_MAX_EMB || !C->Emb[I].Used) {
        C->R[R_AX] = 0;
        *R8p(C, 3) = 0xA2;
      } else if (Size <= C->Emb[I].Size) {
        C->Emb[I].Size = Size;
        C->R[R_AX] = 1;
        *R8p(C, 3) = 0;
      } else {
        UINT32  OldSize = C->Emb[I].Size;

        C->Emb[I].Used = FALSE;
        if (XmsFit(C, Size, &Start) && Start == C->Emb[I].Start) {
          C->Emb[I].Size = Size;
          C->R[R_AX] = 1;
          *R8p(C, 3) = 0;
        } else {
          C->Emb[I].Size = OldSize;
          C->R[R_AX] = 0;
          *R8p(C, 3) = 0xA0;
        }
        C->Emb[I].Used = TRUE;
      }
      break;
    case 0x10:
      C->R[R_AX] = 0;
      C->R[R_DX] = 0;
      *R8p(C, 3) = 0xB1;
      break;
    case 0x11:
    case 0x12:
      C->R[R_AX] = 0;
      *R8p(C, 3) = 0xB2;
      break;
    default:
      C->R[R_AX] = 0;
      *R8p(C, 3) = 0x80;
      break;
  }

  C->IP = Pop(C);                                 // far call: RETF
  C->S[S_CS] = Pop(C);
}

//
// ---------------------------------------------------------------
// Interrupt dispatch
// ---------------------------------------------------------------
//
STATIC
VOID
BiosTimer(X86 *C)
{
  UINT32  Ticks = (UINT32)M16(C, BDA(0x6C)) | ((UINT32)M16(C, BDA(0x6E)) << 16);
  UINT16  Seg;
  UINT16  Off;

  Ticks++;
  if (Ticks >= 0x1800B0) {
    Ticks = 0;
    C->Mem[BDA(0x70)] = 1;
  }
  SM16(C, BDA(0x6C), (UINT16)Ticks);
  SM16(C, BDA(0x6E), (UINT16)(Ticks >> 16));

  Off = M16(C, 0x1C * 4);
  Seg = M16(C, 0x1C * 4 + 2);

  if (!(Seg == BIOS_SEG && Off == STUB_BASE + 0x1C * 4)) {
    // Someone hooked INT 1Ch: call it, and come back to TICK_END.
    Push(C, (UINT16)(C->F | F_FIXED));
    Push(C, BIOS_SEG);
    Push(C, TICK_END);
    C->F = (UINT16)(C->F & ~(F_IF | F_TF));
    C->IP = Off;
    C->S[S_CS] = Seg;
    return;
  }
  DoIret(C);
}

STATIC
BOOLEAN
HleDispatch(X86 *C)
{
  UINT16  Ip;
  UINT8   Vec;

  if (C->S[S_CS] != BIOS_SEG) {
    return FALSE;
  }
  Ip = C->IP;

  if (Ip == XMS_ENTRY) {
    XmsCall(C);
    return TRUE;
  }
  if (Ip == TICK_END) {
    DoIret(C);
    return TRUE;
  }
  if (Ip < STUB_BASE || Ip >= STUB_BASE + STUB_SPAN || ((Ip - STUB_BASE) & 3) != 0) {
    return FALSE;
  }

  Vec = (UINT8)((Ip - STUB_BASE) >> 2);
  if (!gHleVec[Vec]) {
    return FALSE;
  }

  switch (Vec) {
    case 0x08:
      BiosTimer(C);
      return TRUE;
    case 0x10:
      BiosVideo(C);
      break;
    case 0x11:
      C->R[R_AX] = M16(C, BDA(0x10));
      break;
    case 0x12:
      C->R[R_AX] = M16(C, BDA(0x13));
      break;
    case 0x13:
      BiosDisk(C);
      break;
    case 0x14:
      C->R[R_AX] = 0x8000;                        // serial: nothing attached
      break;
    case 0x15:
      switch (GetAH(C)) {
        case 0x88:
          C->R[R_AX] = 0;                         // extended memory is only offered through XMS
          RetCF(C, FALSE);
          break;
        case 0x86:
          RetCF(C, FALSE);
          break;
        default:
          SetAH(C, 0x86);
          RetCF(C, TRUE);
          break;
      }
      break;
    case 0x16:
      BiosKeyboard(C);
      if (C->WaitInput) {
        return TRUE;                              // blocked: do not return yet
      }
      break;
    case 0x17:
      SetAH(C, 0x01);                             // printer: timeout / none
      break;
    case 0x19:
      C->Reboot = TRUE;
      return TRUE;
    case 0x1A:
      BiosTime(C);
      break;
    case 0x2F:
      if (C->R[R_AX] == 0x4300) {
        SetAL(C, 0x80);
      } else if (C->R[R_AX] == 0x4310) {
        C->S[S_ES] = BIOS_SEG;
        C->R[R_BX] = XMS_ENTRY;
      }
      break;
    default:
      break;
  }

  DoIret(C);
  return TRUE;
}

//
// ---------------------------------------------------------------
// Power-on: memory map, vectors, BIOS data area, boot
// ---------------------------------------------------------------
//
STATIC
VOID
BiosInit(X86 *C, BOOLEAN HaveXms)
{
  UINT32       I;
  UINT32       Floppies = (UINT32)C->Disk[0].Present + (UINT32)C->Disk[1].Present;
  UINT32       Hdds = (UINT32)C->Disk[2].Present + (UINT32)C->Disk[3].Present;
  CONST UINT8  DiskTable[11] = { 0xAF, 0x02, 0x25, 0x02, 0x12, 0x1B, 0xFF, 0x6C, 0xF6, 0x0F, 0x08 };
  CONST CHAR8  *Date = "01/01/94";

  X86Tables();
  MemFill(C->Mem, 0, X86_MEM_SIZE);
  C->A20 = FALSE;
  C->Halted = FALSE;
  C->WaitInput = FALSE;
  C->Dead = FALSE;
  C->Reboot = FALSE;
  C->HmaUsed = FALSE;
  C->DiskStatus = 0;
  C->Pit = 0;
  C->Port61 = 0;
  C->A20Cmd = 0;
  C->LastPort60 = 0xFF;
  for (I = 0; I < X86_MAX_EMB; I++) {
    C->Emb[I].Used = FALSE;
  }

  // Stubs: IRET for every vector, RETF for the XMS entry.
  for (I = 0; I < 256; I++) {
    C->Mem[(BIOS_SEG << 4) + STUB_BASE + I * 4] = 0xCF;
    C->Mem[(BIOS_SEG << 4) + STUB_BASE + I * 4 + 1] = 0x90;
    SM16(C, I * 4, (UINT16)(STUB_BASE + I * 4));
    SM16(C, I * 4 + 2, BIOS_SEG);
    gHleVec[I] = 0;
  }
  C->Mem[(BIOS_SEG << 4) + XMS_ENTRY] = 0xCB;
  C->Mem[(BIOS_SEG << 4) + TICK_END] = 0xCF;

  gHleVec[0x08] = 1;
  gHleVec[0x10] = 1;
  gHleVec[0x11] = 1;
  gHleVec[0x12] = 1;
  gHleVec[0x13] = 1;
  gHleVec[0x14] = 1;
  gHleVec[0x15] = 1;
  gHleVec[0x16] = 1;
  gHleVec[0x17] = 1;
  gHleVec[0x19] = 1;
  gHleVec[0x1A] = 1;
  gHleVec[0x2F] = HaveXms ? 1 : 0;

  for (I = 0; I < 11; I++) {
    C->Mem[(BIOS_SEG << 4) + DISK_PARAMS + I] = DiskTable[I];
  }
  SM16(C, 0x1E * 4, DISK_PARAMS);
  SM16(C, 0x1E * 4 + 2, BIOS_SEG);

  for (I = 0; Date[I] != 0; I++) {
    C->Mem[(BIOS_SEG << 4) + 0xFFF5 + I] = (UINT8)Date[I];
  }
  C->Mem[(BIOS_SEG << 4) + 0xFFFE] = 0xFC;                 // PC/AT

  // BIOS data area
  SM16(C, BDA(0x10), (UINT16)(0x0020 | (Floppies ? (0x0001 | ((Floppies - 1) << 6)) : 0)));
  SM16(C, BDA(0x13), 640);
  SM16(C, BDA(0x1A), 0x1E);
  SM16(C, BDA(0x1C), 0x1E);
  SM16(C, BDA(0x80), 0x1E);
  SM16(C, BDA(0x82), 0x3E);
  C->Mem[BDA(0x49)] = 3;
  SM16(C, BDA(0x4A), 80);
  SM16(C, BDA(0x4C), 0x1000);
  SM16(C, BDA(0x60), 0x0607);
  SM16(C, BDA(0x63), 0x3D4);
  C->Mem[BDA(0x75)] = (UINT8)Hdds;
  C->Mem[BDA(0x84)] = 24;
  SM16(C, BDA(0x85), 16);
  C->Mem[BDA(0x89)] = 0x51;
  VidClear(C, 0x07);
}

//
// Loads the boot sector of the chosen drive and prepares to run it.
//
STATIC
BOOLEAN
BiosBoot(X86 *C, UINT8 Drive)
{
  X86DISK  *D = DiskFor(C, Drive);
  UINT8    Sec[512];
  UINT32   I;

  if (D == NULL || !HostDiskRead(DiskIndex(C, D), 0, 1, Sec)) {
    return FALSE;
  }
  if (Sec[510] != 0x55 || Sec[511] != 0xAA) {
    HostLog((CONST CHAR8 *)"boot sector has no 55AA signature", Drive, 0);
    return FALSE;
  }

  for (I = 0; I < 512; I++) {
    C->Mem[0x7C00 + I] = Sec[I];
  }

  for (I = 0; I < 8; I++) {
    C->R[I] = 0;
  }
  *R8p(C, 2) = Drive;
  C->S[S_ES] = 0;
  C->S[S_CS] = 0;
  C->S[S_SS] = 0;
  C->S[S_DS] = 0;
  C->R[R_SP] = 0x7C00;
  C->IP = 0x7C00;
  C->F = (UINT16)(0x0002 | F_IF);
  return TRUE;
}

//
// Runs up to Max instructions. Stops early when the guest waits for a key,
// halts, powers off or reboots.
//
STATIC
UINT32
X86Run(X86 *C, UINT32 Max)
{
  UINT32  I;

  for (I = 0; I < Max; I++) {
    if (C->Dead || C->Reboot) {
      break;
    }
    if (C->WaitInput) {
      if (!KeyAvail(C)) {
        break;
      }
      C->WaitInput = FALSE;
    }
    if (C->Halted) {
      break;
    }
    if (HleDispatch(C)) {
      continue;
    }
    X86Step(C);
  }
  return I;
}

//
// Delivers a timer tick (IRQ0) if the guest can take interrupts.
//
STATIC
BOOLEAN
X86Irq0(X86 *C)
{
  if ((C->F & F_IF) || C->WaitInput || C->Halted) {
    IntEnter(C, 8);
    return TRUE;
  }
  return FALSE;
}
