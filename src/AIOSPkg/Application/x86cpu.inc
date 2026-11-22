//
// =====================================================================
//  AIOS DOS emulator - x86 CPU core (real mode, 8086 / 186 / 286)
//
//  Host-agnostic: needs only the basic types (UINT8/16/32, INT8/16,
//  UINTN, BOOLEAN, STATIC, VOID) and the host hooks declared below.
//  Define X86_MODEL_8086 to get 8086 behaviour (used to validate the
//  core against real 8088 test vectors); the default is a 286.
// =====================================================================
//

#define X86_MEM_SIZE  0x110000u      // 1 MB + 64 KB high memory area

#define F_CF  0x0001
#define F_PF  0x0004
#define F_AF  0x0010
#define F_ZF  0x0040
#define F_SF  0x0080
#define F_TF  0x0100
#define F_IF  0x0200
#define F_DF  0x0400
#define F_OF  0x0800

#ifdef X86_MODEL_8086
#define F_FIXED   0xF002
#define F_POPSET  0xF002
#else
#define F_FIXED   0x0002
#define F_POPSET  0x0002
#endif
#define F_POPMASK  0x0FD5

#define R_AX  0
#define R_CX  1
#define R_DX  2
#define R_BX  3
#define R_SP  4
#define R_BP  5
#define R_SI  6
#define R_DI  7

#define S_ES  0
#define S_CS  1
#define S_SS  2
#define S_DS  3

#define X86_MAX_EMB  32

typedef struct {
  UINT32   Start;
  UINT32   Size;
  BOOLEAN  Used;
  UINT8    Locks;
} X86EMB;

typedef struct {
  BOOLEAN  Present;
  BOOLEAN  ReadOnly;
  BOOLEAN  IsHdd;
  UINT64   Sectors;
  UINT32   Cyls;
  UINT32   Heads;
  UINT32   Spt;
} X86DISK;

typedef struct {
  UINT16   R[8];
  UINT16   S[4];
  UINT16   IP;
  UINT16   F;
  UINT8    *Mem;
  BOOLEAN  A20;
  BOOLEAN  Halted;
  BOOLEAN  WaitInput;
  BOOLEAN  Dead;
  BOOLEAN  Reboot;
  INT8     SegOv;
  UINT32   Insn;
  UINT16   StartIP;

  X86DISK  Disk[4];                 // 0,1 floppies   2,3 hard disks (80h, 81h)
  UINT8    DiskStatus;

  UINT8    *Xms;
  UINT32   XmsSize;
  X86EMB   Emb[X86_MAX_EMB];
  BOOLEAN  HmaUsed;

  UINT16   Pit;
  UINT8    Port61;
  UINT8    A20Cmd;
  UINT8    LastPort60;
} X86;

//
// ---- host hooks (implemented by the host that includes this file) ----
//
STATIC BOOLEAN  HostDiskRead(UINTN Drive, UINT64 Lba, UINTN Count, UINT8 *Buf);
STATIC BOOLEAN  HostDiskWrite(UINTN Drive, UINT64 Lba, UINTN Count, CONST UINT8 *Buf);
STATIC VOID     HostLog(CONST CHAR8 *Msg, UINT32 A, UINT32 B);
STATIC VOID     HostTime(UINT16 *Year, UINT8 *Month, UINT8 *Day, UINT8 *Hour, UINT8 *Min, UINT8 *Sec);

//
// ---------------------------------------------------------------
// Memory
// ---------------------------------------------------------------
//
STATIC UINT8  gX86Par[256];
STATIC BOOLEAN  gX86ParReady = FALSE;

STATIC
VOID
X86Tables(VOID)
{
  UINTN  I;
  UINTN  B;
  UINTN  N;

  if (gX86ParReady) {
    return;
  }
  for (I = 0; I < 256; I++) {
    N = 0;
    for (B = 0; B < 8; B++) {
      if ((I >> B) & 1) {
        N++;
      }
    }
    gX86Par[I] = (UINT8)((N & 1) == 0);
  }
  gX86ParReady = TRUE;
}

STATIC
UINT32
Lin(X86 *C, UINT16 Seg, UINT16 Off)
{
  UINT32  A = ((UINT32)Seg << 4) + Off;

  if (!C->A20) {
    A &= 0xFFFFF;
  }
  return A;
}

STATIC UINT8  Rd8(X86 *C, UINT16 Seg, UINT16 Off) { return C->Mem[Lin(C, Seg, Off)]; }
STATIC VOID   Wr8(X86 *C, UINT16 Seg, UINT16 Off, UINT8 V) { C->Mem[Lin(C, Seg, Off)] = V; }

STATIC
UINT16
Rd16(X86 *C, UINT16 Seg, UINT16 Off)
{
  return (UINT16)(C->Mem[Lin(C, Seg, Off)] | ((UINT16)C->Mem[Lin(C, Seg, (UINT16)(Off + 1))] << 8));
}

STATIC
VOID
Wr16(X86 *C, UINT16 Seg, UINT16 Off, UINT16 V)
{
  C->Mem[Lin(C, Seg, Off)] = (UINT8)V;
  C->Mem[Lin(C, Seg, (UINT16)(Off + 1))] = (UINT8)(V >> 8);
}

STATIC UINT16 M16(X86 *C, UINT32 A) { return (UINT16)(C->Mem[A] | ((UINT16)C->Mem[A + 1] << 8)); }
STATIC VOID   SM16(X86 *C, UINT32 A, UINT16 V) { C->Mem[A] = (UINT8)V; C->Mem[A + 1] = (UINT8)(V >> 8); }

STATIC
UINT8
Fetch8(X86 *C)
{
  UINT8  V = Rd8(C, C->S[S_CS], C->IP);
  C->IP++;
  return V;
}

STATIC
UINT16
Fetch16(X86 *C)
{
  UINT16  V = Rd16(C, C->S[S_CS], C->IP);
  C->IP = (UINT16)(C->IP + 2);
  return V;
}

STATIC
VOID
Push(X86 *C, UINT16 V)
{
  C->R[R_SP] = (UINT16)(C->R[R_SP] - 2);
  Wr16(C, C->S[S_SS], C->R[R_SP], V);
}

STATIC
UINT16
Pop(X86 *C)
{
  UINT16  V = Rd16(C, C->S[S_SS], C->R[R_SP]);
  C->R[R_SP] = (UINT16)(C->R[R_SP] + 2);
  return V;
}

STATIC
UINT8 *
R8p(X86 *C, UINT8 I)
{
  return (UINT8 *)&C->R[I & 3] + (I >> 2);
}

//
// ---------------------------------------------------------------
// Flags and ALU
// ---------------------------------------------------------------
//
STATIC
VOID
SetSZP(X86 *C, UINT32 Res, BOOLEAN W)
{
  UINT32  Mask = W ? 0xFFFF : 0xFF;
  UINT32  Sign = W ? 0x8000 : 0x80;

  Res &= Mask;
  C->F = (UINT16)((C->F & ~(F_ZF | F_SF | F_PF)) |
                  (Res == 0 ? F_ZF : 0) |
                  ((Res & Sign) ? F_SF : 0) |
                  (gX86Par[Res & 0xFF] ? F_PF : 0));
}

STATIC
UINT16
Alu(X86 *C, UINT8 Op, UINT32 A, UINT32 B, BOOLEAN W)
{
  UINT32  Mask = W ? 0xFFFF : 0xFF;
  UINT32  Sign = W ? 0x8000 : 0x80;
  UINT32  Cf = (C->F & F_CF) ? 1 : 0;
  UINT32  R;
  UINT16  Flags = 0;

  A &= Mask;
  B &= Mask;

  switch (Op) {
    case 0:                                   // ADD
    case 2:                                   // ADC
      R = A + B + ((Op == 2) ? Cf : 0);
      if (R > Mask) Flags |= F_CF;
      if (((A ^ B ^ R) & 0x10) != 0) Flags |= F_AF;
      if (((A ^ R) & (B ^ R) & Sign) != 0) Flags |= F_OF;
      break;

    case 3:                                   // SBB
    case 5:                                   // SUB
    case 7:                                   // CMP
      R = A - B - ((Op == 3) ? Cf : 0);
      if ((UINT32)A < (UINT32)B + ((Op == 3) ? Cf : 0)) Flags |= F_CF;
      if (((A ^ B ^ R) & 0x10) != 0) Flags |= F_AF;
      if (((A ^ B) & (A ^ R) & Sign) != 0) Flags |= F_OF;
      break;

    case 1:                                   // OR
      R = A | B;
      break;
    case 4:                                   // AND
      R = A & B;
      break;
    default:                                  // XOR
      R = A ^ B;
      break;
  }

  R &= Mask;
  C->F = (UINT16)((C->F & ~(F_CF | F_AF | F_OF)) | Flags);
  SetSZP(C, R, W);
  return (UINT16)R;
}

STATIC
UINT16
IncDec(X86 *C, UINT16 V, BOOLEAN Dec, BOOLEAN W)
{
  UINT16  Cf = (UINT16)(C->F & F_CF);
  UINT16  R = Alu(C, Dec ? 5 : 0, V, 1, W);

  C->F = (UINT16)((C->F & ~F_CF) | Cf);
  return R;
}

STATIC
UINT16
Shift(X86 *C, UINT8 Op, UINT16 V, UINT8 Count, BOOLEAN W)
{
  UINT32  Mask = W ? 0xFFFF : 0xFF;
  UINT32  Sign = W ? 0x8000 : 0x80;
  UINT32  Val = V & Mask;
  UINT32  Cf;
  UINT32  Old;
  UINT32  I;

#ifndef X86_MODEL_8086
  Count &= 31;
#endif
  if (Count == 0) {
    return (UINT16)Val;
  }

  for (I = 0; I < Count; I++) {
    Cf = (C->F & F_CF) ? 1 : 0;
    switch (Op) {
      case 0:                                  // ROL
        Old = (Val & Sign) ? 1 : 0;
        Val = ((Val << 1) | Old) & Mask;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | (Old ? F_CF : 0) | ((((Val & Sign) ? 1 : 0) ^ Old) ? F_OF : 0));
        break;
      case 1:                                  // ROR
        Old = Val & 1;
        Val = (Val >> 1) | (Old ? Sign : 0);
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | (Old ? F_CF : 0) |
                        (((((Val & Sign) ? 1 : 0) ^ ((Val & (Sign >> 1)) ? 1 : 0))) ? F_OF : 0));
        break;
      case 2:                                  // RCL
        Old = (Val & Sign) ? 1 : 0;
        Val = ((Val << 1) | Cf) & Mask;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | (Old ? F_CF : 0) | ((((Val & Sign) ? 1 : 0) ^ Old) ? F_OF : 0));
        break;
      case 3:                                  // RCR
        Old = Val & 1;
        Val = (Val >> 1) | (Cf ? Sign : 0);
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | (Old ? F_CF : 0) |
                        (((((Val & Sign) ? 1 : 0) ^ ((Val & (Sign >> 1)) ? 1 : 0))) ? F_OF : 0));
        break;
      case 4:                                  // SHL
      case 6:                                  // SAL
        Old = (Val & Sign) ? 1 : 0;
        Val = (Val << 1) & Mask;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF | F_AF)) | (Old ? F_CF : 0) | ((((Val & Sign) ? 1 : 0) ^ Old) ? F_OF : 0));
        SetSZP(C, Val, W);
        break;
      case 5:                                  // SHR
        Old = Val & 1;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF | F_AF)) | (Old ? F_CF : 0) | ((Val & Sign) ? F_OF : 0));
        Val = Val >> 1;
        SetSZP(C, Val, W);
        break;
      default:                                 // SAR
        Old = Val & 1;
        Val = (Val >> 1) | (Val & Sign);
        C->F = (UINT16)((C->F & ~(F_CF | F_OF | F_AF)) | (Old ? F_CF : 0));
        SetSZP(C, Val, W);
        break;
    }
  }

  return (UINT16)Val;
}

//
// ---------------------------------------------------------------
// Interrupts
// ---------------------------------------------------------------
//
STATIC
VOID
IntEnter(X86 *C, UINT8 N)
{
  Push(C, (UINT16)(C->F | F_FIXED));
  Push(C, C->S[S_CS]);
  Push(C, C->IP);
  C->F = (UINT16)(C->F & ~(F_IF | F_TF));
  C->IP = M16(C, (UINT32)N * 4);
  C->S[S_CS] = M16(C, (UINT32)N * 4 + 2);
  C->Halted = FALSE;
}

STATIC
VOID
DoIret(X86 *C)
{
  UINT16  F;

  C->IP = Pop(C);
  C->S[S_CS] = Pop(C);
  F = Pop(C);
  C->F = (UINT16)((F & F_POPMASK) | F_POPSET);
}

STATIC
VOID
DivideError(X86 *C)
{
#ifndef X86_MODEL_8086
  C->IP = C->StartIP;
#endif
  IntEnter(C, 0);
}

//
// ---------------------------------------------------------------
// ModRM
// ---------------------------------------------------------------
//
typedef struct {
  UINT8    Mod;
  UINT8    Reg;
  UINT8    Rm;
  BOOLEAN  IsReg;
  UINT8    Seg;
  UINT16   Off;
} MRM;

STATIC
VOID
DecodeModRM(X86 *C, MRM *M)
{
  UINT8   B = Fetch8(C);
  UINT16  Off = 0;
  UINT8   Seg = S_DS;

  M->Mod = (UINT8)(B >> 6);
  M->Reg = (UINT8)((B >> 3) & 7);
  M->Rm = (UINT8)(B & 7);
  M->IsReg = (BOOLEAN)(M->Mod == 3);

  if (M->IsReg) {
    return;
  }

  switch (M->Rm) {
    case 0: Off = (UINT16)(C->R[R_BX] + C->R[R_SI]); break;
    case 1: Off = (UINT16)(C->R[R_BX] + C->R[R_DI]); break;
    case 2: Off = (UINT16)(C->R[R_BP] + C->R[R_SI]); Seg = S_SS; break;
    case 3: Off = (UINT16)(C->R[R_BP] + C->R[R_DI]); Seg = S_SS; break;
    case 4: Off = C->R[R_SI]; break;
    case 5: Off = C->R[R_DI]; break;
    case 6: Off = C->R[R_BP]; Seg = S_SS; break;
    default: Off = C->R[R_BX]; break;
  }

  if (M->Mod == 0 && M->Rm == 6) {
    Off = Fetch16(C);
    Seg = S_DS;
  } else if (M->Mod == 1) {
    Off = (UINT16)(Off + (UINT16)(INT16)(INT8)Fetch8(C));
  } else if (M->Mod == 2) {
    Off = (UINT16)(Off + Fetch16(C));
  }

  M->Seg = (C->SegOv >= 0) ? (UINT8)C->SegOv : Seg;
  M->Off = Off;
}

STATIC UINT8  RdE8(X86 *C, MRM *M)  { return M->IsReg ? *R8p(C, M->Rm) : Rd8(C, C->S[M->Seg], M->Off); }
STATIC UINT16 RdE16(X86 *C, MRM *M) { return M->IsReg ? C->R[M->Rm] : Rd16(C, C->S[M->Seg], M->Off); }

STATIC
VOID
WrE8(X86 *C, MRM *M, UINT8 V)
{
  if (M->IsReg) {
    *R8p(C, M->Rm) = V;
  } else {
    Wr8(C, C->S[M->Seg], M->Off, V);
  }
}

STATIC
VOID
WrE16(X86 *C, MRM *M, UINT16 V)
{
  if (M->IsReg) {
    C->R[M->Rm] = V;
  } else {
    Wr16(C, C->S[M->Seg], M->Off, V);
  }
}

//
// ---------------------------------------------------------------
// I/O ports (a handful of PC devices)
// ---------------------------------------------------------------
//
STATIC
UINT8
PortIn8(X86 *C, UINT16 Port)
{
  switch (Port) {
    case 0x40:
      C->Pit = (UINT16)(C->Pit - 3);
      return (UINT8)C->Pit;
    case 0x60:
      return C->LastPort60;
    case 0x61:
      C->Port61 ^= 0x10;                      // refresh toggle for delay loops
      return C->Port61;
    case 0x64:
      return 0x00;
    case 0x92:
      return (UINT8)(C->A20 ? 0x02 : 0x00);
    case 0x3DA:
      C->Port61 ^= 0x08;                      // vertical retrace toggle
      return (UINT8)((C->Port61 & 0x08) | 0x01);
    default:
      return 0xFF;
  }
}

STATIC
VOID
PortOut8(X86 *C, UINT16 Port, UINT8 V)
{
  switch (Port) {
    case 0x61:
      C->Port61 = (UINT8)((C->Port61 & 0x10) | (V & 0xEF));
      break;
    case 0x64:
      C->A20Cmd = V;
      if (V == 0xDF) {
        C->A20 = TRUE;
      } else if (V == 0xDD) {
        C->A20 = FALSE;
      }
      break;
    case 0x60:
      if (C->A20Cmd == 0xD1) {
        C->A20 = (BOOLEAN)((V & 0x02) != 0);
        C->A20Cmd = 0;
      }
      break;
    case 0x92:
      C->A20 = (BOOLEAN)((V & 0x02) != 0);
      break;
    default:
      break;
  }
}

//
// ---------------------------------------------------------------
// Instruction execution
// ---------------------------------------------------------------
//
STATIC
VOID
UndefinedOp(X86 *C, UINT8 Op)
{
  HostLog((CONST CHAR8 *)"invalid opcode", Op, C->S[S_CS] * 16u + C->StartIP);
  C->IP = C->StartIP;
  IntEnter(C, 6);
}

STATIC
BOOLEAN
Cond(X86 *C, UINT8 Cc)
{
  BOOLEAN  R;
  UINT16   F = C->F;

  switch (Cc >> 1) {
    case 0: R = (BOOLEAN)((F & F_OF) != 0); break;
    case 1: R = (BOOLEAN)((F & F_CF) != 0); break;
    case 2: R = (BOOLEAN)((F & F_ZF) != 0); break;
    case 3: R = (BOOLEAN)((F & (F_CF | F_ZF)) != 0); break;
    case 4: R = (BOOLEAN)((F & F_SF) != 0); break;
    case 5: R = (BOOLEAN)((F & F_PF) != 0); break;
    case 6: R = (BOOLEAN)(((F & F_SF) != 0) != ((F & F_OF) != 0)); break;
    default: R = (BOOLEAN)(((F & F_ZF) != 0) || (((F & F_SF) != 0) != ((F & F_OF) != 0))); break;
  }
  return (Cc & 1) ? (BOOLEAN)!R : R;
}

STATIC
VOID
StringOp(X86 *C, UINT8 Op, BOOLEAN W, UINT8 Rep)
{
  UINT16  Step = W ? 2 : 1;
  UINT8   SrcSeg = (C->SegOv >= 0) ? (UINT8)C->SegOv : S_DS;
  UINT16  A;
  UINT16  B;

  if (C->F & F_DF) {
    Step = (UINT16)(0 - Step);
  }

  for (;;) {
    if (Rep != 0 && C->R[R_CX] == 0) {
      break;
    }

    switch (Op) {
      case 0xA4:                                 // MOVS
        if (W) {
          Wr16(C, C->S[S_ES], C->R[R_DI], Rd16(C, C->S[SrcSeg], C->R[R_SI]));
        } else {
          Wr8(C, C->S[S_ES], C->R[R_DI], Rd8(C, C->S[SrcSeg], C->R[R_SI]));
        }
        C->R[R_SI] = (UINT16)(C->R[R_SI] + Step);
        C->R[R_DI] = (UINT16)(C->R[R_DI] + Step);
        break;

      case 0xA6:                                 // CMPS
        A = W ? Rd16(C, C->S[SrcSeg], C->R[R_SI]) : Rd8(C, C->S[SrcSeg], C->R[R_SI]);
        B = W ? Rd16(C, C->S[S_ES], C->R[R_DI]) : Rd8(C, C->S[S_ES], C->R[R_DI]);
        Alu(C, 7, A, B, W);
        C->R[R_SI] = (UINT16)(C->R[R_SI] + Step);
        C->R[R_DI] = (UINT16)(C->R[R_DI] + Step);
        break;

      case 0xAA:                                 // STOS
        if (W) {
          Wr16(C, C->S[S_ES], C->R[R_DI], C->R[R_AX]);
        } else {
          Wr8(C, C->S[S_ES], C->R[R_DI], *R8p(C, 0));
        }
        C->R[R_DI] = (UINT16)(C->R[R_DI] + Step);
        break;

      case 0xAC:                                 // LODS
        if (W) {
          C->R[R_AX] = Rd16(C, C->S[SrcSeg], C->R[R_SI]);
        } else {
          *R8p(C, 0) = Rd8(C, C->S[SrcSeg], C->R[R_SI]);
        }
        C->R[R_SI] = (UINT16)(C->R[R_SI] + Step);
        break;

      default:                                   // SCAS
        B = W ? Rd16(C, C->S[S_ES], C->R[R_DI]) : Rd8(C, C->S[S_ES], C->R[R_DI]);
        Alu(C, 7, W ? C->R[R_AX] : *R8p(C, 0), B, W);
        C->R[R_DI] = (UINT16)(C->R[R_DI] + Step);
        break;
    }

    if (Rep == 0) {
      break;
    }
    C->R[R_CX] = (UINT16)(C->R[R_CX] - 1);
    if (Op == 0xA6 || Op == 0xAE) {
      if (Rep == 1 && (C->F & F_ZF) == 0) break;      // REPE
      if (Rep == 2 && (C->F & F_ZF) != 0) break;      // REPNE
    }
  }
}

STATIC
VOID
Group3(X86 *C, BOOLEAN W)
{
  MRM     M;
  UINT32  V;
  UINT32  R;
  INT32   Q;
  INT32   Rem;
  UINT32  Dividend;

  DecodeModRM(C, &M);
  V = W ? RdE16(C, &M) : RdE8(C, &M);

  switch (M.Reg) {
    case 0:
    case 1:                                        // TEST E, imm
      Alu(C, 4, V, W ? Fetch16(C) : Fetch8(C), W);
      break;

    case 2:                                        // NOT
      if (W) WrE16(C, &M, (UINT16)~V); else WrE8(C, &M, (UINT8)~V);
      break;

    case 3:                                        // NEG
      R = Alu(C, 5, 0, V, W);
      C->F = (UINT16)((C->F & ~F_CF) | (((V & (W ? 0xFFFF : 0xFF)) != 0) ? F_CF : 0));
      if (W) WrE16(C, &M, (UINT16)R); else WrE8(C, &M, (UINT8)R);
      break;

    case 4:                                        // MUL
      if (W) {
        R = (UINT32)C->R[R_AX] * V;
        C->R[R_AX] = (UINT16)R;
        C->R[R_DX] = (UINT16)(R >> 16);
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | ((R >> 16) != 0 ? (F_CF | F_OF) : 0));
      } else {
        R = (UINT32)(*R8p(C, 0)) * V;
        C->R[R_AX] = (UINT16)R;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | ((R >> 8) != 0 ? (F_CF | F_OF) : 0));
      }
      break;

    case 5:                                        // IMUL
      if (W) {
        Q = (INT32)(INT16)C->R[R_AX] * (INT32)(INT16)V;
        C->R[R_AX] = (UINT16)Q;
        C->R[R_DX] = (UINT16)((UINT32)Q >> 16);
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | ((Q != (INT32)(INT16)Q) ? (F_CF | F_OF) : 0));
      } else {
        Q = (INT32)(INT8)(*R8p(C, 0)) * (INT32)(INT8)V;
        C->R[R_AX] = (UINT16)Q;
        C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | ((Q != (INT32)(INT8)Q) ? (F_CF | F_OF) : 0));
      }
      break;

    case 6:                                        // DIV
      if (V == 0) {
        DivideError(C);
        return;
      }
      if (W) {
        Dividend = ((UINT32)C->R[R_DX] << 16) | C->R[R_AX];
        R = Dividend / V;
        if (R > 0xFFFF) {
          DivideError(C);
          return;
        }
        C->R[R_AX] = (UINT16)R;
        C->R[R_DX] = (UINT16)(Dividend % V);
      } else {
        Dividend = C->R[R_AX];
        R = Dividend / V;
        if (R > 0xFF) {
          DivideError(C);
          return;
        }
        C->R[R_AX] = (UINT16)((R & 0xFF) | ((Dividend % V) << 8));
      }
      break;

    default:                                       // IDIV
      if (V == 0) {
        DivideError(C);
        return;
      }
      if (W) {
        Q = (INT32)(((UINT32)C->R[R_DX] << 16) | C->R[R_AX]);
        if ((INT32)(INT16)V == -1 && (UINT32)(((UINT32)C->R[R_DX] << 16) | C->R[R_AX]) == 0x80000000u) {
          DivideError(C);
          return;
        }
        Rem = Q % (INT32)(INT16)V;
        Q = Q / (INT32)(INT16)V;
#ifdef X86_MODEL_8086
        if (Q > 32767 || Q < -32767) {
#else
        if (Q > 32767 || Q < -32768) {
#endif
          DivideError(C);
          return;
        }
        C->R[R_AX] = (UINT16)Q;
        C->R[R_DX] = (UINT16)Rem;
      } else {
        Q = (INT32)(INT16)C->R[R_AX];
        Rem = Q % (INT32)(INT8)V;
        Q = Q / (INT32)(INT8)V;
#ifdef X86_MODEL_8086
        if (Q > 127 || Q < -127) {
#else
        if (Q > 127 || Q < -128) {
#endif
          DivideError(C);
          return;
        }
        C->R[R_AX] = (UINT16)((UINT8)Q | ((UINT16)(UINT8)Rem << 8));
      }
      break;
  }
}

STATIC
VOID
X86Step(X86 *C)
{
  UINT8    Op;
  UINT8    Rep = 0;
  MRM      M;
  UINT32   T;
  UINT16   U;
  UINT16   V;
  UINT8    B;
  INT32    S32;
  BOOLEAN  Prefix = TRUE;

  C->StartIP = C->IP;
  C->SegOv = -1;
  C->Insn++;

  while (Prefix) {
    Op = Fetch8(C);
    switch (Op) {
      case 0x26: C->SegOv = S_ES; break;
      case 0x2E: C->SegOv = S_CS; break;
      case 0x36: C->SegOv = S_SS; break;
      case 0x3E: C->SegOv = S_DS; break;
      case 0xF0: break;
      case 0xF2: Rep = 2; break;
      case 0xF3: Rep = 1; break;
      default: Prefix = FALSE; break;
    }
  }

  // ---- ALU block 00-3F ----
  if (Op < 0x40 && (Op & 7) < 6) {
    UINT8  A = (UINT8)(Op >> 3);

    switch (Op & 7) {
      case 0:
        DecodeModRM(C, &M);
        B = (UINT8)Alu(C, A, RdE8(C, &M), *R8p(C, M.Reg), FALSE);
        if (A != 7) WrE8(C, &M, B);
        break;
      case 1:
        DecodeModRM(C, &M);
        U = Alu(C, A, RdE16(C, &M), C->R[M.Reg], TRUE);
        if (A != 7) WrE16(C, &M, U);
        break;
      case 2:
        DecodeModRM(C, &M);
        B = (UINT8)Alu(C, A, *R8p(C, M.Reg), RdE8(C, &M), FALSE);
        if (A != 7) *R8p(C, M.Reg) = B;
        break;
      case 3:
        DecodeModRM(C, &M);
        U = Alu(C, A, C->R[M.Reg], RdE16(C, &M), TRUE);
        if (A != 7) C->R[M.Reg] = U;
        break;
      case 4:
        B = (UINT8)Alu(C, A, *R8p(C, 0), Fetch8(C), FALSE);
        if (A != 7) *R8p(C, 0) = B;
        break;
      default:
        U = Alu(C, A, C->R[R_AX], Fetch16(C), TRUE);
        if (A != 7) C->R[R_AX] = U;
        break;
    }
    return;
  }

  switch (Op) {
    case 0x06: case 0x0E: case 0x16: case 0x1E:           // PUSH seg
      Push(C, C->S[Op >> 3]);
      break;
    case 0x07: case 0x17: case 0x1F:                      // POP seg
      C->S[Op >> 3] = Pop(C);
      break;

    case 0x27:                                            // DAA
    case 0x2F: {                                          // DAS
      UINT8   Al = *R8p(C, 0);
      UINT8   OldAl = Al;
      BOOLEAN OldCf = (BOOLEAN)((C->F & F_CF) != 0);
      BOOLEAN Cf = FALSE;
      BOOLEAN Af = FALSE;

      // Behaviour verified against real 8088 hardware test vectors.
      if ((Al & 0x0F) > 9 || (C->F & F_AF)) {
        Al = (UINT8)(Al + ((Op == 0x27) ? 6 : -6));
        Af = TRUE;
      }
      if (OldAl > ((C->F & F_AF) ? 0x9F : 0x99) || OldCf) {
        Al = (UINT8)(Al + ((Op == 0x27) ? 0x60 : -0x60));
        Cf = TRUE;
      }
      *R8p(C, 0) = Al;
      C->F = (UINT16)((C->F & ~(F_CF | F_AF)) | (Cf ? F_CF : 0) | (Af ? F_AF : 0));
      SetSZP(C, Al, FALSE);
      break;
    }

    case 0x37:                                            // AAA
    case 0x3F:                                            // AAS
      if ((*R8p(C, 0) & 0x0F) > 9 || (C->F & F_AF)) {
        if (Op == 0x37) {
          *R8p(C, 0) = (UINT8)(*R8p(C, 0) + 6);
          *R8p(C, 4) = (UINT8)(*R8p(C, 4) + 1);
        } else {
          *R8p(C, 0) = (UINT8)(*R8p(C, 0) - 6);
          *R8p(C, 4) = (UINT8)(*R8p(C, 4) - 1);
        }
        C->F |= (F_AF | F_CF);
      } else {
        C->F &= (UINT16)~(F_AF | F_CF);
      }
      *R8p(C, 0) &= 0x0F;
      break;

    case 0x40: case 0x41: case 0x42: case 0x43:
    case 0x44: case 0x45: case 0x46: case 0x47:
      C->R[Op & 7] = IncDec(C, C->R[Op & 7], FALSE, TRUE);
      break;
    case 0x48: case 0x49: case 0x4A: case 0x4B:
    case 0x4C: case 0x4D: case 0x4E: case 0x4F:
      C->R[Op & 7] = IncDec(C, C->R[Op & 7], TRUE, TRUE);
      break;

    case 0x50: case 0x51: case 0x52: case 0x53:
    case 0x54: case 0x55: case 0x56: case 0x57:
#ifdef X86_MODEL_8086
      if ((Op & 7) == R_SP) {
        C->R[R_SP] = (UINT16)(C->R[R_SP] - 2);
        Wr16(C, C->S[S_SS], C->R[R_SP], C->R[R_SP]);
        break;
      }
#endif
      Push(C, C->R[Op & 7]);
      break;
    case 0x58: case 0x59: case 0x5A: case 0x5B:
    case 0x5C: case 0x5D: case 0x5E: case 0x5F:
      C->R[Op & 7] = Pop(C);
      break;

#ifndef X86_MODEL_8086
    case 0x60: {                                          // PUSHA
      UINT16  Sp = C->R[R_SP];
      UINT8   K;

      for (K = 0; K < 8; K++) {
        Push(C, (K == R_SP) ? Sp : C->R[K]);
      }
      break;
    }
    case 0x61: {                                          // POPA
      UINT8  K;

      for (K = 8; K > 0; K--) {
        U = Pop(C);
        if ((K - 1) != R_SP) {
          C->R[K - 1] = U;
        }
      }
      break;
    }
    case 0x62:                                            // BOUND
      DecodeModRM(C, &M);
      if (M.IsReg) {
        UndefinedOp(C, Op);
      } else {
        S32 = (INT16)C->R[M.Reg];
        if (S32 < (INT16)Rd16(C, C->S[M.Seg], M.Off) ||
            S32 > (INT16)Rd16(C, C->S[M.Seg], (UINT16)(M.Off + 2))) {
          C->IP = C->StartIP;
          IntEnter(C, 5);
        }
      }
      break;
    case 0x68:
      Push(C, Fetch16(C));
      break;
    case 0x6A:
      Push(C, (UINT16)(INT16)(INT8)Fetch8(C));
      break;
    case 0x69:
    case 0x6B:
      DecodeModRM(C, &M);
      V = RdE16(C, &M);
      U = (Op == 0x69) ? Fetch16(C) : (UINT16)(INT16)(INT8)Fetch8(C);
      S32 = (INT32)(INT16)V * (INT32)(INT16)U;
      C->R[M.Reg] = (UINT16)S32;
      C->F = (UINT16)((C->F & ~(F_CF | F_OF)) | ((S32 != (INT32)(INT16)S32) ? (F_CF | F_OF) : 0));
      break;
    case 0x6C: case 0x6D: case 0x6E: case 0x6F: {        // INS / OUTS (rarely used)
      UINT16  Step = ((Op & 1) ? 2 : 1);
      UINT8   SrcSeg = (C->SegOv >= 0) ? (UINT8)C->SegOv : S_DS;

      if (C->F & F_DF) Step = (UINT16)(0 - Step);
      for (;;) {
        if (Rep && C->R[R_CX] == 0) break;
        if (Op < 0x6E) {
          if (Op & 1) {
            Wr16(C, C->S[S_ES], C->R[R_DI], (UINT16)(PortIn8(C, C->R[R_DX]) | (PortIn8(C, (UINT16)(C->R[R_DX] + 1)) << 8)));
          } else {
            Wr8(C, C->S[S_ES], C->R[R_DI], PortIn8(C, C->R[R_DX]));
          }
          C->R[R_DI] = (UINT16)(C->R[R_DI] + Step);
        } else {
          if (Op & 1) {
            V = Rd16(C, C->S[SrcSeg], C->R[R_SI]);
            PortOut8(C, C->R[R_DX], (UINT8)V);
            PortOut8(C, (UINT16)(C->R[R_DX] + 1), (UINT8)(V >> 8));
          } else {
            PortOut8(C, C->R[R_DX], Rd8(C, C->S[SrcSeg], C->R[R_SI]));
          }
          C->R[R_SI] = (UINT16)(C->R[R_SI] + Step);
        }
        if (!Rep) break;
        C->R[R_CX] = (UINT16)(C->R[R_CX] - 1);
      }
      break;
    }
#endif

    case 0x70: case 0x71: case 0x72: case 0x73:
    case 0x74: case 0x75: case 0x76: case 0x77:
    case 0x78: case 0x79: case 0x7A: case 0x7B:
    case 0x7C: case 0x7D: case 0x7E: case 0x7F:
      B = Fetch8(C);
      if (Cond(C, (UINT8)(Op & 0x0F))) {
        C->IP = (UINT16)(C->IP + (UINT16)(INT16)(INT8)B);
      }
      break;

    case 0x80: case 0x81: case 0x82: case 0x83:
      DecodeModRM(C, &M);
      if (Op == 0x81) {
        V = RdE16(C, &M);
        U = Alu(C, M.Reg, V, Fetch16(C), TRUE);
        if (M.Reg != 7) WrE16(C, &M, U);
      } else if (Op == 0x83) {
        V = RdE16(C, &M);
        U = Alu(C, M.Reg, V, (UINT16)(INT16)(INT8)Fetch8(C), TRUE);
        if (M.Reg != 7) WrE16(C, &M, U);
      } else {
        B = RdE8(C, &M);
        B = (UINT8)Alu(C, M.Reg, B, Fetch8(C), FALSE);
        if (M.Reg != 7) WrE8(C, &M, B);
      }
      break;

    case 0x84:
      DecodeModRM(C, &M);
      Alu(C, 4, RdE8(C, &M), *R8p(C, M.Reg), FALSE);
      break;
    case 0x85:
      DecodeModRM(C, &M);
      Alu(C, 4, RdE16(C, &M), C->R[M.Reg], TRUE);
      break;
    case 0x86:
      DecodeModRM(C, &M);
      B = RdE8(C, &M);
      WrE8(C, &M, *R8p(C, M.Reg));
      *R8p(C, M.Reg) = B;
      break;
    case 0x87:
      DecodeModRM(C, &M);
      U = RdE16(C, &M);
      WrE16(C, &M, C->R[M.Reg]);
      C->R[M.Reg] = U;
      break;
    case 0x88:
      DecodeModRM(C, &M);
      WrE8(C, &M, *R8p(C, M.Reg));
      break;
    case 0x89:
      DecodeModRM(C, &M);
      WrE16(C, &M, C->R[M.Reg]);
      break;
    case 0x8A:
      DecodeModRM(C, &M);
      *R8p(C, M.Reg) = RdE8(C, &M);
      break;
    case 0x8B:
      DecodeModRM(C, &M);
      C->R[M.Reg] = RdE16(C, &M);
      break;
    case 0x8C:
      DecodeModRM(C, &M);
      if (M.Reg > 3) { UndefinedOp(C, Op); break; }
      WrE16(C, &M, C->S[M.Reg]);
      break;
    case 0x8D:
      DecodeModRM(C, &M);
      if (M.IsReg) { UndefinedOp(C, Op); break; }
      C->R[M.Reg] = M.Off;
      break;
    case 0x8E:
      DecodeModRM(C, &M);
#ifdef X86_MODEL_8086
      if (M.Reg > 3) { UndefinedOp(C, Op); break; }
#else
      if (M.Reg > 3 || M.Reg == S_CS) { UndefinedOp(C, Op); break; }
#endif
      C->S[M.Reg] = RdE16(C, &M);
      break;
    case 0x8F:
      DecodeModRM(C, &M);
      U = Pop(C);
      WrE16(C, &M, U);
      break;

    case 0x90:
      break;
    case 0x91: case 0x92: case 0x93: case 0x94:
    case 0x95: case 0x96: case 0x97:
      U = C->R[Op & 7];
      C->R[Op & 7] = C->R[R_AX];
      C->R[R_AX] = U;
      break;
    case 0x98:
      C->R[R_AX] = (UINT16)(INT16)(INT8)*R8p(C, 0);
      break;
    case 0x99:
      C->R[R_DX] = (C->R[R_AX] & 0x8000) ? 0xFFFF : 0;
      break;
    case 0x9A:
      U = Fetch16(C);
      V = Fetch16(C);
      Push(C, C->S[S_CS]);
      Push(C, C->IP);
      C->IP = U;
      C->S[S_CS] = V;
      break;
    case 0x9B:
      break;
    case 0x9C:
      Push(C, (UINT16)(C->F | F_FIXED));
      break;
    case 0x9D:
      U = Pop(C);
      C->F = (UINT16)((U & F_POPMASK) | F_POPSET);
      break;
    case 0x9E:
      C->F = (UINT16)((C->F & 0xFF00) | (UINT16)(*R8p(C, 4) & 0xD5) | 0x02);
      break;
    case 0x9F:
      *R8p(C, 4) = (UINT8)((C->F & 0xD5) | 0x02);
      break;

    case 0xA0:
      U = Fetch16(C);
      *R8p(C, 0) = Rd8(C, C->S[(C->SegOv >= 0) ? C->SegOv : S_DS], U);
      break;
    case 0xA1:
      U = Fetch16(C);
      C->R[R_AX] = Rd16(C, C->S[(C->SegOv >= 0) ? C->SegOv : S_DS], U);
      break;
    case 0xA2:
      U = Fetch16(C);
      Wr8(C, C->S[(C->SegOv >= 0) ? C->SegOv : S_DS], U, *R8p(C, 0));
      break;
    case 0xA3:
      U = Fetch16(C);
      Wr16(C, C->S[(C->SegOv >= 0) ? C->SegOv : S_DS], U, C->R[R_AX]);
      break;
    case 0xA4: case 0xA5: case 0xA6: case 0xA7:
    case 0xAA: case 0xAB: case 0xAC: case 0xAD:
    case 0xAE: case 0xAF:
      StringOp(C, (UINT8)(Op & 0xFE), (BOOLEAN)(Op & 1), Rep);
      break;
    case 0xA8:
      Alu(C, 4, *R8p(C, 0), Fetch8(C), FALSE);
      break;
    case 0xA9:
      Alu(C, 4, C->R[R_AX], Fetch16(C), TRUE);
      break;

    case 0xB0: case 0xB1: case 0xB2: case 0xB3:
    case 0xB4: case 0xB5: case 0xB6: case 0xB7:
      *R8p(C, (UINT8)(Op & 7)) = Fetch8(C);
      break;
    case 0xB8: case 0xB9: case 0xBA: case 0xBB:
    case 0xBC: case 0xBD: case 0xBE: case 0xBF:
      C->R[Op & 7] = Fetch16(C);
      break;

#ifndef X86_MODEL_8086
    case 0xC0:
    case 0xC1:
      DecodeModRM(C, &M);
      if (Op == 0xC0) {
        B = RdE8(C, &M);
        B = (UINT8)Shift(C, M.Reg, B, Fetch8(C), FALSE);
        WrE8(C, &M, B);
      } else {
        U = RdE16(C, &M);
        U = Shift(C, M.Reg, U, Fetch8(C), TRUE);
        WrE16(C, &M, U);
      }
      break;
#endif
    case 0xC2:
      U = Fetch16(C);
      C->IP = Pop(C);
      C->R[R_SP] = (UINT16)(C->R[R_SP] + U);
      break;
    case 0xC3:
      C->IP = Pop(C);
      break;
    case 0xC4:
    case 0xC5:
      DecodeModRM(C, &M);
      if (M.IsReg) { UndefinedOp(C, Op); break; }
      C->R[M.Reg] = Rd16(C, C->S[M.Seg], M.Off);
      C->S[(Op == 0xC4) ? S_ES : S_DS] = Rd16(C, C->S[M.Seg], (UINT16)(M.Off + 2));
      break;
    case 0xC6:
      DecodeModRM(C, &M);
      WrE8(C, &M, Fetch8(C));
      break;
    case 0xC7:
      DecodeModRM(C, &M);
      WrE16(C, &M, Fetch16(C));
      break;
#ifndef X86_MODEL_8086
    case 0xC8: {                                          // ENTER
      UINT16  Size = Fetch16(C);
      UINT8   Level = (UINT8)(Fetch8(C) & 31);
      UINT16  Frame;
      UINT8   K;

      Push(C, C->R[R_BP]);
      Frame = C->R[R_SP];
      if (Level > 0) {
        for (K = 1; K < Level; K++) {
          C->R[R_BP] = (UINT16)(C->R[R_BP] - 2);
          Push(C, Rd16(C, C->S[S_SS], C->R[R_BP]));
        }
        Push(C, Frame);
      }
      C->R[R_BP] = Frame;
      C->R[R_SP] = (UINT16)(C->R[R_SP] - Size);
      break;
    }
    case 0xC9:                                            // LEAVE
      C->R[R_SP] = C->R[R_BP];
      C->R[R_BP] = Pop(C);
      break;
#endif
    case 0xCA:
      U = Fetch16(C);
      C->IP = Pop(C);
      C->S[S_CS] = Pop(C);
      C->R[R_SP] = (UINT16)(C->R[R_SP] + U);
      break;
    case 0xCB:
      C->IP = Pop(C);
      C->S[S_CS] = Pop(C);
      break;
    case 0xCC:
      IntEnter(C, 3);
      break;
    case 0xCD:
      B = Fetch8(C);
      IntEnter(C, B);
      break;
    case 0xCE:
      if (C->F & F_OF) {
        IntEnter(C, 4);
      }
      break;
    case 0xCF:
      DoIret(C);
      break;

    case 0xD0: case 0xD1: case 0xD2: case 0xD3:
      DecodeModRM(C, &M);
      B = (Op < 0xD2) ? 1 : *R8p(C, 1);
      if (Op & 1) {
        U = RdE16(C, &M);
        U = Shift(C, M.Reg, U, B, TRUE);
        WrE16(C, &M, U);
      } else {
        T = RdE8(C, &M);
        T = Shift(C, M.Reg, (UINT16)T, B, FALSE);
        WrE8(C, &M, (UINT8)T);
      }
      break;
    case 0xD4:                                            // AAM
      B = Fetch8(C);
      if (B == 0) {
        DivideError(C);
        break;
      }
      {
        UINT8  Al = *R8p(C, 0);
        *R8p(C, 4) = (UINT8)(Al / B);
        *R8p(C, 0) = (UINT8)(Al % B);
        C->F &= (UINT16)~(F_CF | F_AF | F_OF);
        SetSZP(C, *R8p(C, 0), FALSE);
      }
      break;
    case 0xD5:                                            // AAD
      B = Fetch8(C);
      {
        UINT8  Al = (UINT8)(*R8p(C, 0) + (UINT8)(*R8p(C, 4) * B));
        *R8p(C, 0) = Al;
        *R8p(C, 4) = 0;
        C->F &= (UINT16)~(F_CF | F_AF | F_OF);
        SetSZP(C, Al, FALSE);
      }
      break;
    case 0xD6:                                            // SALC
      *R8p(C, 0) = (C->F & F_CF) ? 0xFF : 0x00;
      break;
    case 0xD7:
      *R8p(C, 0) = Rd8(C, C->S[(C->SegOv >= 0) ? C->SegOv : S_DS], (UINT16)(C->R[R_BX] + *R8p(C, 0)));
      break;
    case 0xD8: case 0xD9: case 0xDA: case 0xDB:
    case 0xDC: case 0xDD: case 0xDE: case 0xDF:           // FPU escape: no coprocessor
      DecodeModRM(C, &M);
      break;

    case 0xE0: case 0xE1: case 0xE2: case 0xE3:
      B = Fetch8(C);
      if (Op == 0xE3) {
        if (C->R[R_CX] == 0) {
          C->IP = (UINT16)(C->IP + (UINT16)(INT16)(INT8)B);
        }
      } else {
        C->R[R_CX] = (UINT16)(C->R[R_CX] - 1);
        if (C->R[R_CX] != 0 &&
            (Op == 0xE2 || (Op == 0xE1 && (C->F & F_ZF)) || (Op == 0xE0 && !(C->F & F_ZF)))) {
          C->IP = (UINT16)(C->IP + (UINT16)(INT16)(INT8)B);
        }
      }
      break;
    case 0xE4:
      *R8p(C, 0) = PortIn8(C, Fetch8(C));
      break;
    case 0xE5:
      U = Fetch8(C);
      C->R[R_AX] = (UINT16)(PortIn8(C, U) | (PortIn8(C, (UINT16)(U + 1)) << 8));
      break;
    case 0xE6:
      PortOut8(C, Fetch8(C), *R8p(C, 0));
      break;
    case 0xE7:
      U = Fetch8(C);
      PortOut8(C, U, *R8p(C, 0));
      PortOut8(C, (UINT16)(U + 1), *R8p(C, 4));
      break;
    case 0xE8:
      U = Fetch16(C);
      Push(C, C->IP);
      C->IP = (UINT16)(C->IP + U);
      break;
    case 0xE9:
      U = Fetch16(C);
      C->IP = (UINT16)(C->IP + U);
      break;
    case 0xEA:
      U = Fetch16(C);
      V = Fetch16(C);
      C->IP = U;
      C->S[S_CS] = V;
      break;
    case 0xEB:
      B = Fetch8(C);
      C->IP = (UINT16)(C->IP + (UINT16)(INT16)(INT8)B);
      break;
    case 0xEC:
      *R8p(C, 0) = PortIn8(C, C->R[R_DX]);
      break;
    case 0xED:
      C->R[R_AX] = (UINT16)(PortIn8(C, C->R[R_DX]) | (PortIn8(C, (UINT16)(C->R[R_DX] + 1)) << 8));
      break;
    case 0xEE:
      PortOut8(C, C->R[R_DX], *R8p(C, 0));
      break;
    case 0xEF:
      PortOut8(C, C->R[R_DX], *R8p(C, 0));
      PortOut8(C, (UINT16)(C->R[R_DX] + 1), *R8p(C, 4));
      break;

    case 0xF4:
      C->Halted = TRUE;
      break;
    case 0xF5:
      C->F ^= F_CF;
      break;
    case 0xF6:
      Group3(C, FALSE);
      break;
    case 0xF7:
      Group3(C, TRUE);
      break;
    case 0xF8: C->F &= (UINT16)~F_CF; break;
    case 0xF9: C->F |= F_CF; break;
    case 0xFA: C->F &= (UINT16)~F_IF; break;
    case 0xFB: C->F |= F_IF; break;
    case 0xFC: C->F &= (UINT16)~F_DF; break;
    case 0xFD: C->F |= F_DF; break;

    case 0xFE:
      DecodeModRM(C, &M);
      if (M.Reg > 1) { UndefinedOp(C, Op); break; }
      B = (UINT8)IncDec(C, RdE8(C, &M), (BOOLEAN)(M.Reg == 1), FALSE);
      WrE8(C, &M, B);
      break;
    case 0xFF:
      DecodeModRM(C, &M);
      switch (M.Reg) {
        case 0:
        case 1:
          U = IncDec(C, RdE16(C, &M), (BOOLEAN)(M.Reg == 1), TRUE);
          WrE16(C, &M, U);
          break;
        case 2:
          U = RdE16(C, &M);
          Push(C, C->IP);
          C->IP = U;
          break;
        case 3:
          if (M.IsReg) { UndefinedOp(C, Op); break; }
          U = Rd16(C, C->S[M.Seg], M.Off);
          V = Rd16(C, C->S[M.Seg], (UINT16)(M.Off + 2));
          Push(C, C->S[S_CS]);
          Push(C, C->IP);
          C->IP = U;
          C->S[S_CS] = V;
          break;
        case 4:
          C->IP = RdE16(C, &M);
          break;
        case 5:
          if (M.IsReg) { UndefinedOp(C, Op); break; }
          U = Rd16(C, C->S[M.Seg], M.Off);
          V = Rd16(C, C->S[M.Seg], (UINT16)(M.Off + 2));
          C->IP = U;
          C->S[S_CS] = V;
          break;
        case 6:
          U = RdE16(C, &M);
#ifdef X86_MODEL_8086
          if (M.IsReg && M.Rm == R_SP) {
            U = (UINT16)(C->R[R_SP] - 2);
          }
#endif
          Push(C, U);
          break;
        default:
          UndefinedOp(C, Op);
          break;
      }
      break;

    case 0x0F:
#ifndef X86_MODEL_8086
      B = Fetch8(C);
      if (B == 0x01) {                                    // SMSW / LMSW (real mode only)
        DecodeModRM(C, &M);
        if (M.Reg == 4) {
          WrE16(C, &M, 0xFFF0);
          break;
        }
        if (M.Reg == 6) {
          break;
        }
      }
      C->IP = C->StartIP;
      HostLog((CONST CHAR8 *)"unsupported 0F opcode", B, C->S[S_CS] * 16u + C->StartIP);
      IntEnter(C, 6);
      break;
#else
      UndefinedOp(C, Op);
      break;
#endif

    default:
      UndefinedOp(C, Op);
      break;
  }

  (VOID)T;
  (VOID)S32;
}
