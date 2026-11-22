//
// =====================================================================
//  AIOS desktop (GUI).  This file is #included from main.c.
//
//  Everything is drawn into a back buffer and sent to the screen with
//  the UEFI Graphics Output Protocol (GOP).  The Terminal window runs
//  the real AIOS shell: while the desktop is open, the shell's console
//  output is redirected into a character grid that the window draws.
//
//  Keys:  F1 = start menu   F10 = back to the text shell
// =====================================================================
//

#define GUI_MAX_WINS  8
#define NOTE_MAX      16384
#define FILES_MAX     200
#define DBL_TICKS     120
#define VC_MAX_COLS   100
#define VC_MAX_ROWS   40

#define C_DESK   0x008080
#define C_FACE   0xC0C0C0
#define C_HI     0xFFFFFF
#define C_SH     0x808080
#define C_DK     0x404040
#define C_BLACK  0x000000
#define C_NAVY   0x000080
#define C_WHITE  0xFFFFFF

//
// 8x8 font for ASCII 32..126 (public-domain font8x8 layout, bit 0 = left pixel)
//
STATIC CONST UINT8  gFont[95][8] = {
  {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00},
  {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00},
  {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00},
  {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00},
  {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00},
  {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00},
  {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00},
  {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00},
  {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00},
  {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00},
  {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00},
  {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00},
  {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06},
  {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00},
  {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00},
  {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00},
  {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00},
  {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00},
  {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00},
  {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00},
  {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00},
  {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00},
  {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00},
  {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00},
  {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00},
  {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00},
  {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00},
  {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00},
  {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00},
  {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00},
  {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF},
  {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00},
  {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00},
  {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00},
  {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F},
  {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00},
  {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00},
  {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00},
  {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00},
  {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78},
  {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00},
  {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00},
  {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00},
  {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F},
  {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00},
  {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00},
  {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}
};

STATIC CONST UINT32  gPal16[16] = {
  0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
  0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF
};

STATIC CONST CHAR8 *CONST  gArrow[17] = {
  "X          ",
  "XX         ",
  "X.X        ",
  "X..X       ",
  "X...X      ",
  "X....X     ",
  "X.....X    ",
  "X......X   ",
  "X.......X  ",
  "X........X ",
  "X.....XXXXX",
  "X..X..X    ",
  "X.X X..X   ",
  "XX  X..X   ",
  "X    X..X  ",
  "     X..X  ",
  "      XX   "
};

typedef struct {
  INT32  X0;
  INT32  Y0;
  INT32  X1;
  INT32  Y1;
} RECT;

typedef enum {
  W_TERM = 0,
  W_FILES,
  W_NOTE,
  W_SYSINFO,
  W_ABOUT
} WTYPE;

typedef struct {
  CHAR16   Name[48];
  UINT64   Size;
  BOOLEAN  IsDir;
} FENT;

typedef struct {
  UINTN   Vol;
  CHAR16  Path[MAX_PATH];
  FENT    *Ents;
  UINTN   Count;
  UINTN   Sel;
  UINTN   Top;
} FILESDATA;

typedef struct {
  CHAR16   *Buf;
  UINTN    Len;
  UINTN    Cur;
  UINTN    Top;
  UINTN    Left;
  UINTN    Vol;
  CHAR16   Path[MAX_PATH];
  BOOLEAN  Dirty;
  CHAR16   Msg[48];
} NOTEDATA;

typedef struct {
  BOOLEAN  Used;
  BOOLEAN  Minimized;
  WTYPE    Type;
  INT32    X;
  INT32    Y;
  INT32    W;
  INT32    H;
  VOID     *Data;
  BOOLEAN  Max;
  INT32    Rx;
  INT32    Ry;
  INT32    Rw;
  INT32    Rh;
} WIN;

typedef struct {
  INT32  TbY;
  INT32  TbH;
  INT32  PathY;
  INT32  ListX;
  INT32  ListY;
  INT32  ListW;
  INT32  ListH;
  INT32  Rows;
  INT32  StatY;
  INT32  ScrollX;
} FLAY;

typedef struct {
  INT32  TbH;
  INT32  TextX;
  INT32  TextY;
  INT32  Cols;
  INT32  Rows;
  INT32  StatY;
} NLAY;

//
// ---- state ----
//
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL  *gGop = NULL;
STATIC UINT32                        *gScene = NULL;
STATIC UINT32                        *gFrame = NULL;
STATIC INT32                         gSW = 0;
STATIC INT32                         gSH = 0;
STATIC INT32                         gS = 1;
STATIC INT32                         gCW = 8;
STATIC INT32                         gCH = 10;
STATIC INT32                         gB = 2;
STATIC INT32                         gTH = 14;
STATIC INT32                         gTB = 20;

STATIC RECT                          gDirty;
STATIC BOOLEAN                       gHasDirty = FALSE;
STATIC RECT                          gClip;

STATIC WIN                           gWins[GUI_MAX_WINS];
STATIC INT32                         gOrder[GUI_MAX_WINS];
STATIC UINTN                         gWinCount = 0;

STATIC BOOLEAN                       gMenuOpen = FALSE;
STATIC INT32                         gMenuSel = 0;
STATIC BOOLEAN                       gKbMouse = FALSE;
STATIC BOOLEAN                       gPs2Ok = FALSE;
STATIC BOOLEAN                       gNoPs2 = FALSE;
STATIC BOOLEAN                       gKbDrag = FALSE;
STATIC BOOLEAN                       gGuiQuit = FALSE;

STATIC INT32                         gMx = 0;
STATIC INT32                         gMy = 0;
STATIC BOOLEAN                       gBtn = FALSE;
STATIC BOOLEAN                       gPrevBtn = FALSE;
STATIC INT32                         gCurX = -1000;
STATIC INT32                         gCurY = -1000;
STATIC BOOLEAN                       gCursorMoved = FALSE;
STATIC INT32                         gDrag = -1;
STATIC INT32                         gDragDX = 0;
STATIC INT32                         gDragDY = 0;

STATIC UINTN                         gTicks = 0;
STATIC UINTN                         gTicksPerSec = 1000;
STATIC UINTN                         gSecTick = 0;
STATIC UINTN                         gPtrEventsRel = 0;
STATIC UINTN                         gPtrEventsAbs = 0;
STATIC UINTN                         gLastClickTick = 0;
STATIC INT32                         gLastClickX = -1;
STATIC INT32                         gLastClickY = -1;
STATIC UINT8                         gLastSec = 99;

STATIC EFI_SIMPLE_POINTER_PROTOCOL   *gSp[4];
STATIC UINTN                         gSpCount = 0;
STATIC EFI_ABSOLUTE_POINTER_PROTOCOL *gAp[4];
STATIC UINTN                         gApCount = 0;

STATIC CHAR16                        gPending[MAX_PATH];
STATIC BOOLEAN                       gTermDirty = FALSE;

STATIC EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  gVcProto;
STATIC EFI_SIMPLE_TEXT_OUTPUT_MODE      gVcMode;
STATIC CHAR16                           gVcCh[VC_MAX_ROWS][VC_MAX_COLS];
STATIC UINT8                            gVcAt[VC_MAX_ROWS][VC_MAX_COLS];
STATIC UINTN                            gVcCols = 80;
STATIC UINTN                            gVcRows = 25;
STATIC UINTN                            gVcX = 0;
STATIC UINTN                            gVcY = 0;
STATIC UINT8                            gVcAttr = 0x07;
STATIC BOOLEAN                          gVcCursor = TRUE;

#define SB_LINES  200
STATIC CHAR16                           gSbCh[SB_LINES][VC_MAX_COLS];
STATIC UINT8                            gSbAt[SB_LINES][VC_MAX_COLS];
STATIC UINTN                            gSbHead = 0;
STATIC UINTN                            gSbCount = 0;
STATIC UINTN                            gSbOff = 0;

STATIC CONST CHAR16  *gMenuItems[6] = {
  L"Terminal", L"Files", L"Notepad", L"System Info", L"About AIOS", L"Exit to shell"
};

STATIC VOID  GuiPump(VOID);
STATIC BOOLEAN  GuiKeyFilter(EFI_INPUT_KEY *Key);

//
// ---------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------
//
STATIC INT32 Imin(INT32 A, INT32 B) { return (A < B) ? A : B; }
STATIC INT32 Imax(INT32 A, INT32 B) { return (A > B) ? A : B; }

STATIC
VOID
SetRect(RECT *R, INT32 X, INT32 Y, INT32 W, INT32 H)
{
  R->X0 = X;
  R->Y0 = Y;
  R->X1 = X + W;
  R->Y1 = Y + H;
}

STATIC
BOOLEAN
RectHit(CONST RECT *R, INT32 X, INT32 Y)
{
  return (BOOLEAN)(X >= R->X0 && X < R->X1 && Y >= R->Y0 && Y < R->Y1);
}

STATIC
BOOLEAN
RectsTouch(CONST RECT *A, CONST RECT *B)
{
  return (BOOLEAN)(A->X0 < B->X1 && B->X0 < A->X1 && A->Y0 < B->Y1 && B->Y0 < A->Y1);
}

STATIC
VOID
Inval(INT32 X, INT32 Y, INT32 W, INT32 H)
{
  RECT  R;

  SetRect(&R, X, Y, W, H);
  R.X0 = Imax(R.X0, 0);
  R.Y0 = Imax(R.Y0, 0);
  R.X1 = Imin(R.X1, gSW);
  R.Y1 = Imin(R.Y1, gSH);

  if (R.X0 >= R.X1 || R.Y0 >= R.Y1) {
    return;
  }

  if (!gHasDirty) {
    gDirty = R;
    gHasDirty = TRUE;
  } else {
    gDirty.X0 = Imin(gDirty.X0, R.X0);
    gDirty.Y0 = Imin(gDirty.Y0, R.Y0);
    gDirty.X1 = Imax(gDirty.X1, R.X1);
    gDirty.Y1 = Imax(gDirty.Y1, R.Y1);
  }
}

STATIC
VOID
InvalAll(VOID)
{
  Inval(0, 0, gSW, gSH);
}

//
// Sets the clip rectangle to R intersected with the dirty region.
//
STATIC
VOID
ClipTo(INT32 X0, INT32 Y0, INT32 X1, INT32 Y1)
{
  gClip.X0 = Imax(Imax(X0, gDirty.X0), 0);
  gClip.Y0 = Imax(Imax(Y0, gDirty.Y0), 0);
  gClip.X1 = Imin(Imin(X1, gDirty.X1), gSW);
  gClip.Y1 = Imin(Imin(Y1, gDirty.Y1), gSH);
}

STATIC
VOID
Fill(INT32 X, INT32 Y, INT32 W, INT32 H, UINT32 Color)
{
  INT32   X0 = Imax(X, gClip.X0);
  INT32   Y0 = Imax(Y, gClip.Y0);
  INT32   X1 = Imin(X + W, gClip.X1);
  INT32   Y1 = Imin(Y + H, gClip.Y1);
  INT32   I;
  INT32   J;
  UINT32  *P;

  if (X0 >= X1 || Y0 >= Y1) {
    return;
  }

  for (J = Y0; J < Y1; J++) {
    P = gScene + (UINTN)J * (UINTN)gSW + (UINTN)X0;
    for (I = X0; I < X1; I++) {
      *P++ = Color;
    }
  }
}

STATIC
VOID
DrawGlyph(INT32 X, INT32 Y, CHAR16 Ch, UINT32 Color)
{
  CONST UINT8  *G;
  UINT8        Bits;
  INT32        Gx;
  INT32        Gy;
  INT32        Sx;
  INT32        Sy;
  INT32        Px;
  INT32        Py;

  if (X + 8 * gS <= gClip.X0 || X >= gClip.X1 || Y + 8 * gS <= gClip.Y0 || Y >= gClip.Y1) {
    return;
  }

  G = (Ch >= 32 && Ch <= 126) ? gFont[Ch - 32] : gFont['?' - 32];

  for (Gy = 0; Gy < 8; Gy++) {
    Bits = G[Gy];
    if (Bits == 0) {
      continue;
    }
    for (Sy = 0; Sy < gS; Sy++) {
      Py = Y + Gy * gS + Sy;
      if (Py < gClip.Y0 || Py >= gClip.Y1) {
        continue;
      }
      for (Gx = 0; Gx < 8; Gx++) {
        if (((Bits >> Gx) & 1) == 0) {
          continue;
        }
        for (Sx = 0; Sx < gS; Sx++) {
          Px = X + Gx * gS + Sx;
          if (Px >= gClip.X0 && Px < gClip.X1) {
            gScene[(UINTN)Py * (UINTN)gSW + (UINTN)Px] = Color;
          }
        }
      }
    }
  }
}

STATIC
VOID
DrawTextN(INT32 X, INT32 Y, CONST CHAR16 *S, UINTN Max, UINT32 Color)
{
  UINTN  I;

  for (I = 0; S[I] != L'\0' && I < Max; I++) {
    DrawGlyph(X + (INT32)I * gCW, Y, S[I], Color);
  }
}

STATIC
VOID
DrawText(INT32 X, INT32 Y, CONST CHAR16 *S, UINT32 Color)
{
  DrawTextN(X, Y, S, 1000, Color);
}

//
// Text vertically centred in a line of height gCH.
//
STATIC
INT32
TextY(INT32 LineTop)
{
  return LineTop + (gCH - 8 * gS) / 2;
}

STATIC
VOID
Bevel(INT32 X, INT32 Y, INT32 W, INT32 H, BOOLEAN Raised)
{
  UINT32  Tl = Raised ? C_HI : C_DK;
  UINT32  Br = Raised ? C_DK : C_HI;

  Fill(X, Y, W, H, C_FACE);
  Fill(X, Y, W, gS, Tl);
  Fill(X, Y, gS, H, Tl);
  Fill(X, Y + H - gS, W, gS, Br);
  Fill(X + W - gS, Y, gS, H, Br);
}

STATIC
VOID
Sunken(INT32 X, INT32 Y, INT32 W, INT32 H, UINT32 Inner)
{
  Fill(X, Y, W, H, C_HI);
  Fill(X, Y, W, gS, C_SH);
  Fill(X, Y, gS, H, C_SH);
  Fill(X + gS, Y + gS, W - 2 * gS, H - 2 * gS, Inner);
}

STATIC
VOID
Button(INT32 X, INT32 Y, INT32 W, INT32 H, CONST CHAR16 *Label, BOOLEAN Pressed)
{
  UINTN  Len = StrLen(Label);
  INT32  Tx = X + (W - (INT32)Len * gCW) / 2;

  Bevel(X, Y, W, H, (BOOLEAN)!Pressed);
  DrawText(Tx + (Pressed ? gS : 0), TextY(Y + (H - gCH) / 2) + (Pressed ? gS : 0), Label, C_BLACK);
}

STATIC
VOID
FmtSize(UINT64 Bytes, CHAR16 *Out, UINTN OutBytes)
{
  UINT64  Tenths;

  if (Bytes >= 1048576ULL) {
    Tenths = (Bytes * 10) / 1048576ULL;
    UnicodeSPrint(Out, OutBytes, L"%u.%u MB", (UINT32)(Tenths / 10), (UINT32)(Tenths % 10));
  } else if (Bytes >= 1024ULL) {
    UnicodeSPrint(Out, OutBytes, L"%u KB", (UINT32)(Bytes / 1024));
  } else {
    UnicodeSPrint(Out, OutBytes, L"%u B", (UINT32)Bytes);
  }
}

STATIC
INTN
CmpText(CONST CHAR16 *A, CONST CHAR16 *B)
{
  while (*A != L'\0' && *B != L'\0') {
    if (Up(*A) != Up(*B)) {
      return (Up(*A) < Up(*B)) ? -1 : 1;
    }
    A++;
    B++;
  }
  if (*A == *B) {
    return 0;
  }
  return (*A == L'\0') ? -1 : 1;
}

//
// ---------------------------------------------------------------
// Virtual console: the shell prints here while the desktop is open
// ---------------------------------------------------------------
//
STATIC
VOID
VcClearRows(UINTN From, UINTN To)
{
  UINTN  R;
  UINTN  C;

  for (R = From; R < To && R < VC_MAX_ROWS; R++) {
    for (C = 0; C < VC_MAX_COLS; C++) {
      gVcCh[R][C] = L' ';
    }
    SetMem(gVcAt[R], VC_MAX_COLS, gVcAttr);     // explicit call: keeps the compiler from inventing memset
  }
}

STATIC
VOID
VcScroll(VOID)
{
  UINTN  R;

  CopyMem(gSbCh[gSbHead % SB_LINES], gVcCh[0], sizeof(gVcCh[0]));
  CopyMem(gSbAt[gSbHead % SB_LINES], gVcAt[0], sizeof(gVcAt[0]));
  gSbHead++;
  if (gSbCount < SB_LINES) {
    gSbCount++;
  }

  for (R = 1; R < gVcRows; R++) {
    CopyMem(gVcCh[R - 1], gVcCh[R], sizeof(gVcCh[0]));
    CopyMem(gVcAt[R - 1], gVcAt[R], sizeof(gVcAt[0]));
  }
  VcClearRows(gVcRows - 1, gVcRows);
}

STATIC
VOID
VcSyncMode(VOID)
{
  gVcMode.CursorColumn = (INT32)gVcX;
  gVcMode.CursorRow = (INT32)gVcY;
  gVcMode.Attribute = (INT32)gVcAttr;
  gVcMode.CursorVisible = gVcCursor;
}

STATIC
VOID
VcPutChar(CHAR16 Ch)
{
  UINTN  N;

  if (Ch == L'\r') {
    gVcX = 0;
    return;
  }
  if (Ch == L'\n') {
    gVcY++;
    if (gVcY >= gVcRows) {
      VcScroll();
      gVcY = gVcRows - 1;
    }
    return;
  }
  if (Ch == L'\b') {
    if (gVcX > 0) {
      gVcX--;
    }
    return;
  }
  if (Ch == L'\t') {
    N = 8 - (gVcX % 8);
    while (N > 0) {
      VcPutChar(L' ');
      N--;
    }
    return;
  }
  if (Ch < L' ') {
    return;
  }

  gVcCh[gVcY][gVcX] = Ch;
  gVcAt[gVcY][gVcX] = gVcAttr;
  gVcX++;
  if (gVcX >= gVcCols) {
    gVcX = 0;
    gVcY++;
    if (gVcY >= gVcRows) {
      VcScroll();
      gVcY = gVcRows - 1;
    }
  }
}

STATIC
EFI_STATUS
EFIAPI
VcReset(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN BOOLEAN Ext)
{
  UNREFERENCED_PARAMETER(This);
  UNREFERENCED_PARAMETER(Ext);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcOutputString(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN CHAR16 *String)
{
  UNREFERENCED_PARAMETER(This);

  gSbOff = 0;
  while (*String != L'\0') {
    VcPutChar(*String);
    String++;
  }
  VcSyncMode();
  gTermDirty = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcTestString(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN CHAR16 *String)
{
  UNREFERENCED_PARAMETER(This);
  UNREFERENCED_PARAMETER(String);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcQueryMode(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN UINTN Mode, OUT UINTN *Columns, OUT UINTN *Rows)
{
  UNREFERENCED_PARAMETER(This);

  if (Mode != 0) {
    return EFI_UNSUPPORTED;
  }
  *Columns = gVcCols;
  *Rows = gVcRows;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcSetMode(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN UINTN Mode)
{
  UNREFERENCED_PARAMETER(This);
  return (Mode == 0) ? EFI_SUCCESS : EFI_UNSUPPORTED;
}

STATIC
EFI_STATUS
EFIAPI
VcSetAttribute(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN UINTN Attribute)
{
  UNREFERENCED_PARAMETER(This);
  gVcAttr = (UINT8)(Attribute & 0x7F);
  VcSyncMode();
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcClearScreen(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This)
{
  UNREFERENCED_PARAMETER(This);
  VcClearRows(0, gVcRows);
  gVcX = 0;
  gVcY = 0;
  VcSyncMode();
  gTermDirty = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcSetCursorPosition(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN UINTN Column, IN UINTN Row)
{
  UNREFERENCED_PARAMETER(This);

  if (Column >= gVcCols || Row >= gVcRows) {
    return EFI_UNSUPPORTED;
  }
  gVcX = Column;
  gVcY = Row;
  VcSyncMode();
  gTermDirty = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
VcEnableCursor(IN EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *This, IN BOOLEAN Visible)
{
  UNREFERENCED_PARAMETER(This);
  gVcCursor = Visible;
  VcSyncMode();
  return EFI_SUCCESS;
}

STATIC
VOID
VconInit(UINTN Cols, UINTN Rows)
{
  gVcCols = (Cols > VC_MAX_COLS) ? VC_MAX_COLS : Cols;
  gVcRows = (Rows > VC_MAX_ROWS) ? VC_MAX_ROWS : Rows;
  gVcAttr = 0x07;
  gVcCursor = TRUE;
  gSbHead = 0;
  gSbCount = 0;
  gSbOff = 0;
  VcClearRows(0, VC_MAX_ROWS);
  gVcX = 0;
  gVcY = 0;

  gVcMode.MaxMode = 1;
  gVcMode.Mode = 0;

  gVcProto.Reset = VcReset;
  gVcProto.OutputString = VcOutputString;
  gVcProto.TestString = VcTestString;
  gVcProto.QueryMode = VcQueryMode;
  gVcProto.SetMode = VcSetMode;
  gVcProto.SetAttribute = VcSetAttribute;
  gVcProto.ClearScreen = VcClearScreen;
  gVcProto.SetCursorPosition = VcSetCursorPosition;
  gVcProto.EnableCursor = VcEnableCursor;
  gVcProto.Mode = &gVcMode;

  VcSyncMode();
}

//
// ---------------------------------------------------------------
// Windows
// ---------------------------------------------------------------
//
STATIC
VOID
WinRect(INT32 Slot, RECT *R)
{
  SetRect(R, gWins[Slot].X, gWins[Slot].Y, gWins[Slot].W, gWins[Slot].H);
}

STATIC
VOID
WinClient(INT32 Slot, RECT *R)
{
  R->X0 = gWins[Slot].X + gB;
  R->Y0 = gWins[Slot].Y + gB + gTH;
  R->X1 = gWins[Slot].X + gWins[Slot].W - gB;
  R->Y1 = gWins[Slot].Y + gWins[Slot].H - gB;
}

STATIC
VOID
InvalWin(INT32 Slot)
{
  Inval(gWins[Slot].X, gWins[Slot].Y, gWins[Slot].W, gWins[Slot].H);
}

STATIC
VOID
InvalClient(INT32 Slot)
{
  RECT  C;

  WinClient(Slot, &C);
  Inval(C.X0, C.Y0, C.X1 - C.X0, C.Y1 - C.Y0);
}

STATIC
INT32
FocusSlot(VOID)
{
  INTN  I;

  for (I = (INTN)gWinCount - 1; I >= 0; I--) {
    INT32  S = gOrder[I];
    if (gWins[S].Used && !gWins[S].Minimized) {
      return S;
    }
  }
  return -1;
}

STATIC
VOID
InvalFocus(VOID)
{
  INT32  F = FocusSlot();

  if (F >= 0) {
    InvalWin(F);
  }
}

STATIC
INT32
FindWin(WTYPE Type)
{
  INT32  I;

  for (I = 0; I < GUI_MAX_WINS; I++) {
    if (gWins[I].Used && gWins[I].Type == Type) {
      return I;
    }
  }
  return -1;
}

STATIC
VOID
OrderRemove(INT32 Slot)
{
  UINTN  I;
  UINTN  J = 0;

  for (I = 0; I < gWinCount; I++) {
    if (gOrder[I] != Slot) {
      gOrder[J++] = gOrder[I];
    }
  }
  gWinCount = J;
}

STATIC
VOID
BringToFront(INT32 Slot)
{
  INT32  OldFocus = FocusSlot();

  OrderRemove(Slot);
  gOrder[gWinCount++] = Slot;
  gWins[Slot].Minimized = FALSE;
  InvalWin(Slot);
  if (OldFocus >= 0 && OldFocus != Slot) {
    InvalWin(OldFocus);
  }
}

STATIC
VOID
CloseWin(INT32 Slot)
{
  InvalWin(Slot);

  if (gWins[Slot].Type == W_FILES) {
    FILESDATA  *D = (FILESDATA *)gWins[Slot].Data;
    if (D != NULL && D->Ents != NULL) {
      gBS->FreePool(D->Ents);
    }
  } else if (gWins[Slot].Type == W_NOTE) {
    NOTEDATA  *N = (NOTEDATA *)gWins[Slot].Data;
    if (N != NULL && N->Buf != NULL) {
      gBS->FreePool(N->Buf);
    }
  } else if (gWins[Slot].Type == W_TERM) {
    gAbortRead = TRUE;
  }

  if (gWins[Slot].Data != NULL) {
    gBS->FreePool(gWins[Slot].Data);
  }

  gWins[Slot].Used = FALSE;
  gWins[Slot].Data = NULL;
  OrderRemove(Slot);
  Inval(0, gSH - gTB, gSW, gTB);
  InvalFocus();

  if (gDrag == Slot) {
    gDrag = -1;
  }
}

STATIC
INT32
NewWin(WTYPE Type, INT32 ClientW, INT32 ClientH, VOID *Data)
{
  INT32  I;
  INT32  Slot = -1;
  INT32  N;
  WIN    *W;

  for (I = 0; I < GUI_MAX_WINS; I++) {
    if (!gWins[I].Used) {
      Slot = I;
      break;
    }
  }
  if (Slot < 0) {
    return -1;
  }

  W = &gWins[Slot];
  ZeroMem(W, sizeof(*W));
  W->Used = TRUE;
  W->Type = Type;
  W->Data = Data;
  W->W = Imin(ClientW + 2 * gB, gSW - 8);
  W->H = Imin(ClientH + 2 * gB + gTH, gSH - gTB - 8);

  N = (INT32)gWinCount;
  W->X = 12 * gS + N * 12 * gS;
  W->Y = 12 * gS + N * 12 * gS;
  if (W->X + W->W > gSW) {
    W->X = Imax(0, gSW - W->W);
  }
  if (W->Y + W->H > gSH - gTB) {
    W->Y = Imax(0, gSH - gTB - W->H);
  }

  gOrder[gWinCount++] = Slot;
  InvalWin(Slot);
  Inval(0, gSH - gTB, gSW, gTB);
  return Slot;
}

STATIC
CONST CHAR16 *
WinTitle(INT32 Slot, CHAR16 *Tmp, UINTN TmpBytes)
{
  NOTEDATA  *N;

  switch (gWins[Slot].Type) {
    case W_TERM:
      return L"Terminal";
    case W_FILES:
      return L"Files";
    case W_SYSINFO:
      return L"System Info";
    case W_ABOUT:
      return L"About AIOS";
    case W_NOTE:
      N = (NOTEDATA *)gWins[Slot].Data;
      UnicodeSPrint(Tmp, TmpBytes, L"Notepad - %c:%s%s", DriveLetter(N->Vol), N->Path, N->Dirty ? L" *" : L"");
      return Tmp;
    default:
      return L"";
  }
}

//
// Title-bar button rectangles. Which: 0 = close, 1 = minimize.
//
STATIC
BOOLEAN
HasMax(WTYPE T)
{
  return (BOOLEAN)(T == W_FILES || T == W_NOTE);
}

//
// Title-bar button rectangles. Which: 0 = close, 1 = maximize, 2 = minimize.
//
STATIC
VOID
TitleBtn(INT32 Slot, INT32 Which, RECT *R)
{
  INT32  Sz = gTH - 2 * gS;
  INT32  Right = gWins[Slot].X + gWins[Slot].W - gB - gS;
  INT32  Y = gWins[Slot].Y + gB + gS;
  INT32  Idx;

  if (Which == 0) {
    Idx = 0;
  } else if (Which == 1) {
    Idx = 1;
  } else {
    Idx = HasMax(gWins[Slot].Type) ? 2 : 1;
  }

  SetRect(R, Right - Sz - Idx * (Sz + gS), Y, Sz, Sz);
}

//
// Title bar with a left-to-right colour gradient.
//
STATIC
VOID
DrawTitleBar(INT32 X, INT32 Y, INT32 W, INT32 H, BOOLEAN Focused)
{
  UINT32  C0 = Focused ? 0x000080 : 0x808080;
  UINT32  C1 = Focused ? 0x1084D0 : 0xA8A8A8;
  INT32   Step = 4 * gS;
  INT32   N = (W + Step - 1) / Step;
  INT32   Den = (N > 1) ? N - 1 : 1;
  INT32   I;
  UINT32  R;
  UINT32  G;
  UINT32  B;

  for (I = 0; I < N; I++) {
    R = (((C0 >> 16) & 0xFF) * (UINT32)(Den - I) + ((C1 >> 16) & 0xFF) * (UINT32)I) / (UINT32)Den;
    G = (((C0 >> 8) & 0xFF) * (UINT32)(Den - I) + ((C1 >> 8) & 0xFF) * (UINT32)I) / (UINT32)Den;
    B = ((C0 & 0xFF) * (UINT32)(Den - I) + (C1 & 0xFF) * (UINT32)I) / (UINT32)Den;
    Fill(X + I * Step, Y, Step, H, (R << 16) | (G << 8) | B);
  }
}

//
// ---------------------------------------------------------------
// App: Terminal
// ---------------------------------------------------------------
//
STATIC
VOID
DrawTerm(INT32 Slot, CONST RECT *C, BOOLEAN Focused)
{
  UINTN   R;
  UINTN   Col;
  UINT8   At;
  UINT32  Fg;
  UINT32  Bg;
  INT32   X;
  INT32   Y;
  CONST CHAR16  *RowCh;
  CONST UINT8   *RowAt;
  UINTN   Back;
  UINTN   Idx;

  Fill(C->X0, C->Y0, C->X1 - C->X0, C->Y1 - C->Y0, C_BLACK);
  UNREFERENCED_PARAMETER(Slot);

  for (R = 0; R < gVcRows; R++) {
    Y = C->Y0 + (INT32)R * gCH;
    if (Y >= gClip.Y1 || Y + gCH <= gClip.Y0) {
      continue;
    }
    if (R < gSbOff) {
      Back = gSbOff - 1 - R;
      Idx = (gSbHead + SB_LINES - 1 - Back) % SB_LINES;
      RowCh = gSbCh[Idx];
      RowAt = gSbAt[Idx];
    } else {
      RowCh = gVcCh[R - gSbOff];
      RowAt = gVcAt[R - gSbOff];
    }

    for (Col = 0; Col < gVcCols; Col++) {
      X = C->X0 + (INT32)Col * gCW;
      At = RowAt[Col];
      Fg = gPal16[At & 0x0F];
      Bg = gPal16[(At >> 4) & 0x07];

      if (Bg != C_BLACK) {
        Fill(X, Y, gCW, gCH, Bg);
      }

      if (Focused && gVcCursor && gSbOff == 0 && R == gVcY && Col == gVcX) {
        Fill(X, Y, gCW, gCH, 0xAAAAAA);
        DrawGlyph(X, TextY(Y), RowCh[Col], C_BLACK);
      } else if (RowCh[Col] > L' ') {
        DrawGlyph(X, TextY(Y), RowCh[Col], Fg);
      }
    }
  }
}

STATIC
INT32
OpenTerm(VOID)
{
  INT32  Slot = FindWin(W_TERM);

  if (Slot >= 0) {
    BringToFront(Slot);
    return Slot;
  }

  Slot = NewWin(W_TERM, (INT32)gVcCols * gCW, (INT32)gVcRows * gCH, NULL);
  if (Slot >= 0) {
    gWins[Slot].X = Imin(10 * gCW + 20, Imax(0, gSW - gWins[Slot].W));
    gWins[Slot].Y = 8 * gS;
    InvalWin(Slot);
  }
  return Slot;
}

//
// ---------------------------------------------------------------
// App: Files
// ---------------------------------------------------------------
//
STATIC
VOID
FilesLayout(CONST RECT *R, FLAY *L)
{
  INT32  H = R->Y1 - R->Y0;
  INT32  W = R->X1 - R->X0;
  INT32  PathH = gCH + 2 * gS;
  INT32  StatH = gCH + 2 * gS;

  L->TbY = R->Y0;
  L->TbH = gCH + 4 * gS;
  L->PathY = R->Y0 + L->TbH;
  L->ListX = R->X0 + gS * 2;
  L->ListY = L->PathY + PathH;
  L->ListW = W - gS * 4;
  L->ListH = H - L->TbH - PathH - StatH - gS * 2;
  L->Rows = Imax(1, (L->ListH - 2 * gS) / gCH);
  L->StatY = R->Y1 - StatH;
  L->ScrollX = L->ListX + L->ListW - 2 * gCW - gS;
}

STATIC
VOID
FilesLoad(FILESDATA *D)
{
  EFI_FILE_PROTOCOL  *Dir;
  UINT64             Buf[128];
  EFI_FILE_INFO      *I = (EFI_FILE_INFO *)Buf;
  UINTN              Size;
  UINTN              K;
  UINTN              J;
  FENT               Tmp;

  D->Count = 0;
  D->Sel = 0;
  D->Top = 0;

  if (D->Vol >= gVolCount) {
    return;
  }

  if (StrCmp(D->Path, L"\\") != 0) {
    StrCpyS(D->Ents[0].Name, 48, L"..");
    D->Ents[0].IsDir = TRUE;
    D->Ents[0].Size = 0;
    D->Count = 1;
  }

  if (EFI_ERROR(gVols[D->Vol].Root->Open(gVols[D->Vol].Root, &Dir, D->Path, EFI_FILE_MODE_READ, 0))) {
    return;
  }

  for (;;) {
    Size = sizeof(Buf);
    if (EFI_ERROR(Dir->Read(Dir, &Size, Buf)) || Size == 0) {
      break;
    }
    if (StrCmp(I->FileName, L".") == 0 || StrCmp(I->FileName, L"..") == 0) {
      continue;
    }
    if (D->Count >= FILES_MAX) {
      break;
    }

    for (K = 0; K < 47 && I->FileName[K] != L'\0'; K++) {
      D->Ents[D->Count].Name[K] = I->FileName[K];
    }
    D->Ents[D->Count].Name[K] = L'\0';
    D->Ents[D->Count].IsDir = (BOOLEAN)((I->Attribute & EFI_FILE_DIRECTORY) != 0);
    D->Ents[D->Count].Size = D->Ents[D->Count].IsDir ? 0 : I->FileSize;
    D->Count++;
  }

  Dir->Close(Dir);

  // Directories first, then by name ("..", if present, stays on top).
  for (K = 1; K < D->Count; K++) {
    Tmp = D->Ents[K];
    J = K;
    while (J > 0) {
      FENT  *P = &D->Ents[J - 1];
      BOOLEAN  PIsUp = (BOOLEAN)(StrCmp(P->Name, L"..") == 0);
      BOOLEAN  Swap;

      if (PIsUp) {
        break;
      }
      if (Tmp.IsDir != P->IsDir) {
        Swap = Tmp.IsDir;
      } else {
        Swap = (BOOLEAN)(CmpText(Tmp.Name, P->Name) < 0);
      }
      if (!Swap) {
        break;
      }
      D->Ents[J] = D->Ents[J - 1];
      J--;
    }
    D->Ents[J] = Tmp;
  }
}

STATIC
VOID
FilesEnsureVisible(FILESDATA *D, INT32 Rows)
{
  if (D->Sel < D->Top) {
    D->Top = D->Sel;
  }
  if (D->Sel >= D->Top + (UINTN)Rows) {
    D->Top = D->Sel - (UINTN)Rows + 1;
  }
}

STATIC
VOID
DrawFiles(INT32 Slot, CONST RECT *C)
{
  FILESDATA  *D = (FILESDATA *)gWins[Slot].Data;
  FLAY       L;
  INT32      I;
  INT32      X;
  INT32      Bw = 5 * gCW;
  CHAR16     Tmp[80];
  UINTN      Idx;
  UINT32     Fg;

  Fill(C->X0, C->Y0, C->X1 - C->X0, C->Y1 - C->Y0, C_FACE);
  FilesLayout(C, &L);

  // Toolbar: drive buttons and Up
  for (I = 0; I < (INT32)gVolCount && I < 8; I++) {
    UnicodeSPrint(Tmp, sizeof(Tmp), L"%c:", DriveLetter((UINTN)I));
    X = C->X0 + 2 * gS + I * (Bw + 2 * gS);
    Button(X, L.TbY + gS, Bw, L.TbH - 2 * gS, Tmp, (BOOLEAN)((UINTN)I == D->Vol));
  }
  Button(C->X1 - 2 * gS - Bw, L.TbY + gS, Bw, L.TbH - 2 * gS, L"Up", FALSE);

  // Path line
  UnicodeSPrint(Tmp, sizeof(Tmp), L"%c:%s", DriveLetter(D->Vol), D->Path);
  DrawTextN(C->X0 + 4 * gS, TextY(L.PathY + gS), Tmp, (UINTN)((C->X1 - C->X0) / gCW) - 1, C_BLACK);

  // List
  Sunken(L.ListX, L.ListY, L.ListW, L.ListH, C_WHITE);
  for (I = 0; I < L.Rows; I++) {
    Idx = D->Top + (UINTN)I;
    if (Idx >= D->Count) {
      break;
    }
    INT32  Y = L.ListY + gS + I * gCH;
    Fg = D->Ents[Idx].IsDir ? C_NAVY : C_BLACK;

    if (Idx == D->Sel) {
      Fill(L.ListX + gS, Y, L.ListW - 2 * gS - 2 * gCW - 2 * gS, gCH, C_NAVY);
      Fg = C_WHITE;
    }

    if (D->Ents[Idx].IsDir) {
      Fill(L.ListX + 3 * gS, Y + gS, 4 * gS, gS, 0xC0A020);
      Fill(L.ListX + 3 * gS, Y + 2 * gS, 9 * gS, 6 * gS, 0xFFD040);
      Fill(L.ListX + 3 * gS, Y + 2 * gS, 9 * gS, gS, 0xFFE890);
    } else {
      Fill(L.ListX + 4 * gS, Y + gS, 7 * gS, 8 * gS, C_WHITE);
      Fill(L.ListX + 4 * gS, Y + gS, 7 * gS, gS, C_SH);
      Fill(L.ListX + 4 * gS, Y + 8 * gS, 7 * gS, gS, C_SH);
      Fill(L.ListX + 4 * gS, Y + gS, gS, 8 * gS, C_SH);
      Fill(L.ListX + 10 * gS, Y + gS, gS, 8 * gS, C_SH);
      Fill(L.ListX + 6 * gS, Y + 3 * gS, 3 * gS, gS, 0x4040C0);
      Fill(L.ListX + 6 * gS, Y + 5 * gS, 3 * gS, gS, 0x4040C0);
    }
    DrawTextN(L.ListX + 14 * gS, TextY(Y), D->Ents[Idx].Name, 28, Fg);

    if (D->Ents[Idx].IsDir) {
      DrawText(L.ScrollX - 7 * gCW, TextY(Y), L"<DIR>", Fg);
    } else {
      FmtSize(D->Ents[Idx].Size, Tmp, sizeof(Tmp));
      DrawText(L.ScrollX - ((INT32)StrLen(Tmp) + 1) * gCW, TextY(Y), Tmp, Fg);
    }
  }

  // Scroll buttons
  Button(L.ScrollX, L.ListY + gS, 2 * gCW, gCH + gS, L"^", FALSE);
  Button(L.ScrollX, L.ListY + L.ListH - gCH - 2 * gS, 2 * gCW, gCH + gS, L"v", FALSE);

  // Status
  UnicodeSPrint(Tmp, sizeof(Tmp), L"%u item(s)  Enter=open Bksp=up F5=reload", (UINT32)D->Count);
  DrawTextN(C->X0 + 4 * gS, TextY(L.StatY + gS), Tmp, (UINTN)((C->X1 - C->X0) / gCW) - 1, C_BLACK);
}

STATIC
INT32
OpenNote(UINTN Vol, CONST CHAR16 *Path);

STATIC
VOID
JoinPath(CHAR16 *Out, CONST CHAR16 *Dir, CONST CHAR16 *Name)
{
  StrCpyS(Out, MAX_PATH, Dir);
  if (StrCmp(Dir, L"\\") != 0) {
    StrCatS(Out, MAX_PATH, L"\\");
  }
  StrCatS(Out, MAX_PATH, Name);
}

STATIC
VOID
ParentPath(CHAR16 *Path)
{
  UINTN  N = StrLen(Path);

  while (N > 1 && Path[N - 1] != L'\\') {
    N--;
  }
  if (N <= 1) {
    StrCpyS(Path, MAX_PATH, L"\\");
  } else {
    Path[N - 1] = L'\0';
    if (Path[0] == L'\0') {
      StrCpyS(Path, MAX_PATH, L"\\");
    }
  }
}

STATIC
VOID
GuiQueueCommand(CONST CHAR16 *Cmd)
{
  StrCpyS(gPending, MAX_PATH, Cmd);
  gAbortRead = TRUE;
}

STATIC
VOID
FilesOpenSelected(INT32 Slot)
{
  FILESDATA  *D = (FILESDATA *)gWins[Slot].Data;
  CHAR16     Full[MAX_PATH];
  CHAR16     Cmd[MAX_PATH + 8];
  FENT       *E;
  INT32      Term;

  if (D->Sel >= D->Count) {
    return;
  }
  E = &D->Ents[D->Sel];

  if (StrCmp(E->Name, L"..") == 0) {
    ParentPath(D->Path);
    FilesLoad(D);
    InvalClient(Slot);
    return;
  }

  JoinPath(Full, D->Path, E->Name);

  if (E->IsDir) {
    StrCpyS(D->Path, MAX_PATH, Full);
    FilesLoad(D);
    InvalClient(Slot);
    return;
  }

  if (HasExtension(E->Name, L".EFI")) {
    if (HasSpace(Full)) {
      return;
    }
    UnicodeSPrint(Cmd, sizeof(Cmd), L"%c:%s", DriveLetter(D->Vol), Full);
    Term = OpenTerm();
    if (Term >= 0) {
      GuiQueueCommand(Cmd);
    }
    return;
  }

  OpenNote(D->Vol, Full);
}

STATIC
VOID
FilesClick(INT32 Slot, CONST RECT *C, INT32 Px, INT32 Py, BOOLEAN Dbl)
{
  FILESDATA  *D = (FILESDATA *)gWins[Slot].Data;
  FLAY       L;
  INT32      I;
  INT32      X;
  INT32      Bw = 5 * gCW;
  UINTN      Idx;

  FilesLayout(C, &L);

  // toolbar
  if (Py >= L.TbY && Py < L.TbY + L.TbH) {
    for (I = 0; I < (INT32)gVolCount && I < 8; I++) {
      X = C->X0 + 2 * gS + I * (Bw + 2 * gS);
      if (Px >= X && Px < X + Bw) {
        D->Vol = (UINTN)I;
        StrCpyS(D->Path, MAX_PATH, L"\\");
        FilesLoad(D);
        InvalClient(Slot);
        return;
      }
    }
    if (Px >= C->X1 - 2 * gS - Bw) {
      ParentPath(D->Path);
      FilesLoad(D);
      InvalClient(Slot);
    }
    return;
  }

  // scroll buttons
  if (Px >= L.ScrollX && Px < L.ScrollX + 2 * gCW) {
    if (Py < L.ListY + gCH + 2 * gS && D->Top > 0) {
      D->Top--;
      InvalClient(Slot);
    } else if (Py >= L.ListY + L.ListH - gCH - 2 * gS && D->Top + (UINTN)L.Rows < D->Count) {
      D->Top++;
      InvalClient(Slot);
    }
    return;
  }

  // list rows
  if (Px >= L.ListX && Px < L.ListX + L.ListW && Py >= L.ListY + gS && Py < L.ListY + gS + L.Rows * gCH) {
    Idx = D->Top + (UINTN)((Py - L.ListY - gS) / gCH);
    if (Idx < D->Count) {
      D->Sel = Idx;
      InvalClient(Slot);
      if (Dbl) {
        FilesOpenSelected(Slot);
      }
    }
  }
}

STATIC
VOID
FilesKey(INT32 Slot, CONST RECT *C, EFI_INPUT_KEY *Key)
{
  FILESDATA  *D = (FILESDATA *)gWins[Slot].Data;
  FLAY       L;

  FilesLayout(C, &L);

  if (Key->UnicodeChar == CHAR_CARRIAGE_RETURN) {
    FilesOpenSelected(Slot);
    return;
  }
  if (Key->UnicodeChar == CHAR_BACKSPACE) {
    ParentPath(D->Path);
    FilesLoad(D);
    InvalClient(Slot);
    return;
  }

  switch (Key->ScanCode) {
    case SCAN_UP:
      if (D->Sel > 0) {
        D->Sel--;
      }
      break;
    case SCAN_DOWN:
      if (D->Sel + 1 < D->Count) {
        D->Sel++;
      }
      break;
    case SCAN_PAGE_UP:
      D->Sel = (D->Sel > (UINTN)L.Rows) ? D->Sel - (UINTN)L.Rows : 0;
      break;
    case SCAN_PAGE_DOWN:
      D->Sel += (UINTN)L.Rows;
      if (D->Count == 0) {
        D->Sel = 0;
      } else if (D->Sel >= D->Count) {
        D->Sel = D->Count - 1;
      }
      break;
    case SCAN_HOME:
      D->Sel = 0;
      break;
    case SCAN_END:
      D->Sel = (D->Count > 0) ? D->Count - 1 : 0;
      break;
    case SCAN_F5:
      FilesLoad(D);
      break;
    default:
      return;
  }

  FilesEnsureVisible(D, L.Rows);
  InvalClient(Slot);
}

STATIC
INT32
OpenFiles(VOID)
{
  FILESDATA  *D = NULL;
  INT32      Slot;

  if (gVolCount == 0) {
    return -1;
  }

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(FILESDATA), (VOID **)&D))) {
    return -1;
  }
  ZeroMem(D, sizeof(*D));

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(FENT) * FILES_MAX, (VOID **)&D->Ents))) {
    gBS->FreePool(D);
    return -1;
  }

  D->Vol = gCur;
  StrCpyS(D->Path, MAX_PATH, gCwd);
  FilesLoad(D);

  Slot = NewWin(W_FILES, 46 * gCW, 17 * gCH, D);
  if (Slot < 0) {
    gBS->FreePool(D->Ents);
    gBS->FreePool(D);
  }
  return Slot;
}

//
// ---------------------------------------------------------------
// App: Notepad
// ---------------------------------------------------------------
//
STATIC
VOID
NoteLayout(CONST RECT *R, NLAY *L)
{
  INT32  W = R->X1 - R->X0;
  INT32  H = R->Y1 - R->Y0;
  INT32  StatH = gCH + 2 * gS;

  L->TbH = gCH + 4 * gS;
  L->TextX = R->X0 + 3 * gS;
  L->TextY = R->Y0 + L->TbH + 2 * gS;
  L->Cols = Imax(1, (W - 6 * gS) / gCW);
  L->Rows = Imax(1, (H - L->TbH - StatH - 4 * gS) / gCH);
  L->StatY = R->Y1 - StatH;
}

STATIC
UINTN
NLineStart(NOTEDATA *N, UINTN Pos)
{
  while (Pos > 0 && N->Buf[Pos - 1] != L'\n') {
    Pos--;
  }
  return Pos;
}

STATIC
UINTN
NLineEnd(NOTEDATA *N, UINTN Pos)
{
  while (Pos < N->Len && N->Buf[Pos] != L'\n') {
    Pos++;
  }
  return Pos;
}

STATIC
UINTN
NLineNo(NOTEDATA *N, UINTN Pos)
{
  UINTN  I;
  UINTN  Line = 0;

  for (I = 0; I < Pos && I < N->Len; I++) {
    if (N->Buf[I] == L'\n') {
      Line++;
    }
  }
  return Line;
}

STATIC
UINTN
NLinePos(NOTEDATA *N, UINTN Line)
{
  UINTN  Pos = 0;
  UINTN  E;

  while (Line > 0) {
    E = NLineEnd(N, Pos);
    if (E >= N->Len) {
      return N->Len;
    }
    Pos = E + 1;
    Line--;
  }
  return Pos;
}

STATIC
VOID
NoteVisible(NOTEDATA *N, INT32 Cols, INT32 Rows)
{
  UINTN  Line = NLineNo(N, N->Cur);
  UINTN  Col = N->Cur - NLineStart(N, N->Cur);

  if (Line < N->Top) {
    N->Top = Line;
  }
  if (Line >= N->Top + (UINTN)Rows) {
    N->Top = Line - (UINTN)Rows + 1;
  }
  if (Col < N->Left) {
    N->Left = Col;
  }
  if (Col >= N->Left + (UINTN)Cols) {
    N->Left = Col - (UINTN)Cols + 1;
  }
}

STATIC
VOID
DrawNote(INT32 Slot, CONST RECT *C, BOOLEAN Focused)
{
  NOTEDATA  *N = (NOTEDATA *)gWins[Slot].Data;
  NLAY      L;
  UINTN     Pos;
  UINTN     End;
  UINTN     I;
  INT32     Row;
  INT32     Y;
  UINTN     Col;
  CHAR16    Tmp[96];
  UINTN     CurLine;
  UINTN     CurCol;

  Fill(C->X0, C->Y0, C->X1 - C->X0, C->Y1 - C->Y0, C_FACE);
  NoteLayout(C, &L);

  Button(C->X0 + 2 * gS, C->Y0 + gS, 6 * gCW, L.TbH - 2 * gS, L"Save", FALSE);
  DrawTextN(C->X0 + 6 * gCW + 6 * gS + gCW, TextY(C->Y0 + 2 * gS), N->Msg, 30, N->Dirty ? 0x800000 : 0x006000);

  Sunken(C->X0 + gS, L.TextY - 2 * gS, C->X1 - C->X0 - 2 * gS, L.StatY - L.TextY + 2 * gS - gS, C_WHITE);

  Pos = NLinePos(N, N->Top);
  for (Row = 0; Row < L.Rows; Row++) {
    Y = L.TextY + Row * gCH;
    End = NLineEnd(N, Pos);
    for (I = Pos + N->Left, Col = 0; I < End && Col < (UINTN)L.Cols; I++, Col++) {
      DrawGlyph(L.TextX + (INT32)Col * gCW, TextY(Y), N->Buf[I], C_BLACK);
    }
    if (End >= N->Len) {
      break;
    }
    Pos = End + 1;
  }

  CurLine = NLineNo(N, N->Cur);
  CurCol = N->Cur - NLineStart(N, N->Cur);
  if (Focused && CurLine >= N->Top && CurLine < N->Top + (UINTN)L.Rows &&
      CurCol >= N->Left && CurCol < N->Left + (UINTN)L.Cols) {
    Fill(L.TextX + (INT32)(CurCol - N->Left) * gCW, L.TextY + (INT32)(CurLine - N->Top) * gCH,
         Imax(2, gS), gCH, C_BLACK);
  }

  UnicodeSPrint(Tmp, sizeof(Tmp), L"Ln %u, Col %u   %u chars   Ctrl+S = save",
                (UINT32)(CurLine + 1), (UINT32)(CurCol + 1), (UINT32)N->Len);
  DrawTextN(C->X0 + 4 * gS, TextY(L.StatY + gS), Tmp, (UINTN)((C->X1 - C->X0) / gCW) - 1, C_BLACK);
}

STATIC
VOID
NoteSave(NOTEDATA *N)
{
  EFI_FILE_PROTOCOL  *F = NULL;
  CHAR8              *Out = NULL;
  UINTN              I;
  UINTN              O = 0;
  UINTN              Size;
  EFI_STATUS         Status;

  if (N->Vol >= gVolCount) {
    StrCpyS(N->Msg, 48, L"No drive");
    return;
  }

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, N->Len * 2 + 2, (VOID **)&Out))) {
    StrCpyS(N->Msg, 48, L"Out of memory");
    return;
  }

  for (I = 0; I < N->Len; I++) {
    if (N->Buf[I] == L'\n') {
      Out[O++] = '\r';
      Out[O++] = '\n';
    } else {
      Out[O++] = (CHAR8)N->Buf[I];
    }
  }

  if (!EFI_ERROR(gVols[N->Vol].Root->Open(gVols[N->Vol].Root, &F, N->Path,
                                          EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0))) {
    F->Delete(F);
  }

  Status = gVols[N->Vol].Root->Open(gVols[N->Vol].Root, &F, N->Path,
                                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, 0);
  if (!EFI_ERROR(Status)) {
    Size = O;
    Status = F->Write(F, &Size, Out);
    F->Close(F);
  }

  gBS->FreePool(Out);

  if (EFI_ERROR(Status)) {
    StrCpyS(N->Msg, 48, L"Save failed");
  } else {
    StrCpyS(N->Msg, 48, L"Saved");
    N->Dirty = FALSE;
  }
}

STATIC
VOID
NoteLoad(NOTEDATA *N)
{
  EFI_FILE_PROTOCOL  *F;
  EFI_FILE_INFO      *Info;
  CHAR8              *Raw = NULL;
  UINTN              Size;
  UINTN              I;

  if (N->Vol >= gVolCount) {
    return;
  }
  if (EFI_ERROR(gVols[N->Vol].Root->Open(gVols[N->Vol].Root, &F, N->Path, EFI_FILE_MODE_READ, 0))) {
    StrCpyS(N->Msg, 48, L"New file");
    return;
  }

  Info = GetFileInfo(F);
  if (Info == NULL || (Info->Attribute & EFI_FILE_DIRECTORY) != 0) {
    if (Info != NULL) {
      gBS->FreePool(Info);
    }
    F->Close(F);
    return;
  }

  Size = (Info->FileSize > NOTE_MAX - 2) ? (UINTN)(NOTE_MAX - 2) : (UINTN)Info->FileSize;
  gBS->FreePool(Info);

  if (Size > 0 && !EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Size, (VOID **)&Raw))) {
    if (!EFI_ERROR(F->Read(F, &Size, Raw))) {
      for (I = 0; I < Size && N->Len < NOTE_MAX - 2; I++) {
        if (Raw[I] == '\r') {
          continue;
        }
        if (Raw[I] == '\t') {
          N->Buf[N->Len++] = L' ';
          continue;
        }
        if (Raw[I] == '\n' || (Raw[I] >= ' ' && Raw[I] <= '~')) {
          N->Buf[N->Len++] = (CHAR16)Raw[I];
        }
      }
    }
    gBS->FreePool(Raw);
  }

  N->Buf[N->Len] = L'\0';
  F->Close(F);
  StrCpyS(N->Msg, 48, L"Loaded");
}

STATIC
INT32
OpenNote(UINTN Vol, CONST CHAR16 *Path)
{
  NOTEDATA  *N = NULL;
  INT32     Slot;

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, sizeof(NOTEDATA), (VOID **)&N))) {
    return -1;
  }
  ZeroMem(N, sizeof(*N));

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, NOTE_MAX * sizeof(CHAR16), (VOID **)&N->Buf))) {
    gBS->FreePool(N);
    return -1;
  }
  N->Buf[0] = L'\0';
  N->Vol = Vol;
  StrCpyS(N->Path, MAX_PATH, Path);
  NoteLoad(N);

  Slot = NewWin(W_NOTE, 52 * gCW, 18 * gCH, N);
  if (Slot < 0) {
    gBS->FreePool(N->Buf);
    gBS->FreePool(N);
  }
  return Slot;
}

STATIC
VOID
NoteInsert(NOTEDATA *N, CHAR16 Ch)
{
  if (N->Len + 2 >= NOTE_MAX) {
    return;
  }
  CopyMem(&N->Buf[N->Cur + 1], &N->Buf[N->Cur], (N->Len - N->Cur + 1) * sizeof(CHAR16));
  N->Buf[N->Cur] = Ch;
  N->Cur++;
  N->Len++;
  N->Dirty = TRUE;
  N->Msg[0] = L'\0';
}

STATIC
VOID
NoteKey(INT32 Slot, CONST RECT *C, EFI_INPUT_KEY *Key)
{
  NOTEDATA  *N = (NOTEDATA *)gWins[Slot].Data;
  NLAY      L;
  UINTN     Start;
  UINTN     Col;
  UINTN     End;
  UINTN     PrevStart;
  UINTN     PrevEnd;
  UINTN     I;

  NoteLayout(C, &L);

  if (Key->UnicodeChar == 0x13 || Key->ScanCode == SCAN_F2) {
    NoteSave(N);
  } else if (Key->UnicodeChar == CHAR_CARRIAGE_RETURN) {
    NoteInsert(N, L'\n');
  } else if (Key->UnicodeChar == CHAR_BACKSPACE) {
    if (N->Cur > 0) {
      CopyMem(&N->Buf[N->Cur - 1], &N->Buf[N->Cur], (N->Len - N->Cur + 1) * sizeof(CHAR16));
      N->Cur--;
      N->Len--;
      N->Dirty = TRUE;
      N->Msg[0] = L'\0';
    }
  } else if (Key->UnicodeChar == CHAR_TAB) {
    for (I = 0; I < 4; I++) {
      NoteInsert(N, L' ');
    }
  } else if (Key->UnicodeChar >= L' ' && Key->UnicodeChar <= L'~') {
    NoteInsert(N, Key->UnicodeChar);
  } else {
    Start = NLineStart(N, N->Cur);
    Col = N->Cur - Start;

    switch (Key->ScanCode) {
      case SCAN_LEFT:
        if (N->Cur > 0) {
          N->Cur--;
        }
        break;
      case SCAN_RIGHT:
        if (N->Cur < N->Len) {
          N->Cur++;
        }
        break;
      case SCAN_HOME:
        N->Cur = Start;
        break;
      case SCAN_END:
        N->Cur = NLineEnd(N, N->Cur);
        break;
      case SCAN_DELETE:
        if (N->Cur < N->Len) {
          CopyMem(&N->Buf[N->Cur], &N->Buf[N->Cur + 1], (N->Len - N->Cur) * sizeof(CHAR16));
          N->Len--;
          N->Dirty = TRUE;
        }
        break;
      case SCAN_UP:
      case SCAN_PAGE_UP:
        for (I = 0; I < ((Key->ScanCode == SCAN_PAGE_UP) ? (UINTN)L.Rows : 1); I++) {
          Start = NLineStart(N, N->Cur);
          if (Start == 0) {
            break;
          }
          PrevEnd = Start - 1;
          PrevStart = NLineStart(N, PrevEnd);
          N->Cur = PrevStart + ((Col < PrevEnd - PrevStart) ? Col : PrevEnd - PrevStart);
        }
        break;
      case SCAN_DOWN:
      case SCAN_PAGE_DOWN:
        for (I = 0; I < ((Key->ScanCode == SCAN_PAGE_DOWN) ? (UINTN)L.Rows : 1); I++) {
          End = NLineEnd(N, N->Cur);
          if (End >= N->Len) {
            break;
          }
          PrevStart = End + 1;
          PrevEnd = NLineEnd(N, PrevStart);
          N->Cur = PrevStart + ((Col < PrevEnd - PrevStart) ? Col : PrevEnd - PrevStart);
        }
        break;
      default:
        return;
    }
  }

  NoteVisible(N, L.Cols, L.Rows);
  InvalWin(Slot);
}

STATIC
VOID
NoteClick(INT32 Slot, CONST RECT *C, INT32 Px, INT32 Py)
{
  NOTEDATA  *N = (NOTEDATA *)gWins[Slot].Data;
  NLAY      L;
  UINTN     Line;
  UINTN     Pos;
  UINTN     End;
  UINTN     Col;

  NoteLayout(C, &L);

  if (Py < C->Y0 + L.TbH) {
    if (Px >= C->X0 + 2 * gS && Px < C->X0 + 2 * gS + 6 * gCW) {
      NoteSave(N);
      InvalWin(Slot);
    }
    return;
  }

  if (Py >= L.TextY && Py < L.TextY + L.Rows * gCH && Px >= L.TextX) {
    Line = N->Top + (UINTN)((Py - L.TextY) / gCH);
    Pos = NLinePos(N, Line);
    End = NLineEnd(N, Pos);
    Col = N->Left + (UINTN)((Px - L.TextX) / gCW);
    N->Cur = Pos + ((Col < End - Pos) ? Col : End - Pos);
    InvalClient(Slot);
  }
}

//
// ---------------------------------------------------------------
// App: System Info and About
// ---------------------------------------------------------------
//
STATIC
VOID
GuiMemTotals(UINT64 *FreeMb, UINT64 *TotalMb)
{
  UINTN                  MapSize = 0;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  EFI_MEMORY_DESCRIPTOR  *Map = NULL;
  EFI_MEMORY_DESCRIPTOR  *D;
  UINT64                 Pages = 0;
  UINT64                 FreePages = 0;

  *FreeMb = 0;
  *TotalMb = 0;

  if (gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer) != EFI_BUFFER_TOO_SMALL) {
    return;
  }
  MapSize += 2 * DescSize;
  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, MapSize, (VOID **)&Map))) {
    return;
  }
  if (!EFI_ERROR(gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer))) {
    for (D = Map;
         (UINT8 *)D < (UINT8 *)Map + MapSize;
         D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
      Pages += D->NumberOfPages;
      if (D->Type == EfiConventionalMemory) {
        FreePages += D->NumberOfPages;
      }
    }
    *FreeMb = FreePages / 256;
    *TotalMb = Pages / 256;
  }
  gBS->FreePool(Map);
}

STATIC
VOID
DrawSysInfo(CONST RECT *C)
{
  CHAR16     T[100];
  EFI_TIME   Tm;
  UINT64     Free;
  UINT64     Total;
  INT32      Y = C->Y0 + 4 * gS;
  INT32      X = C->X0 + 6 * gS;

  Fill(C->X0, C->Y0, C->X1 - C->X0, C->Y1 - C->Y0, C_FACE);
  GuiMemTotals(&Free, &Total);

  DrawText(X, TextY(Y), L"AIOS " AIOS_VERSION, C_NAVY);
  Y += gCH + 2 * gS;

  UnicodeSPrint(T, sizeof(T), L"Firmware:  %s", gST->FirmwareVendor);
  DrawTextN(X, TextY(Y), T, 40, C_BLACK);
  Y += gCH;

  UnicodeSPrint(T, sizeof(T), L"UEFI:      %u.%u", (UINT32)(gST->Hdr.Revision >> 16),
                (UINT32)((gST->Hdr.Revision & 0xFFFF) / 10));
  DrawText(X, TextY(Y), T, C_BLACK);
  Y += gCH;

  UnicodeSPrint(T, sizeof(T), L"Screen:    %ux%u", (UINT32)gSW, (UINT32)gSH);
  DrawText(X, TextY(Y), T, C_BLACK);
  Y += gCH;

  UnicodeSPrint(T, sizeof(T), L"Memory:    %lu MB free of %lu MB", Free, Total);
  DrawText(X, TextY(Y), T, C_BLACK);
  Y += gCH;

  UnicodeSPrint(T, sizeof(T), L"Drives:    %u    Pointers: %u rel, %u abs", (UINT32)gVolCount,
                (UINT32)gSpCount, (UINT32)gApCount);
  DrawText(X, TextY(Y), T, C_BLACK);
  Y += gCH;

  UnicodeSPrint(T, sizeof(T), L"Mouse:     %d,%d  ps2=%s  events %u", gMx, gMy,
                gPs2Ok ? L"yes" : L"no", (UINT32)(gPtrEventsRel + gPtrEventsAbs));
  DrawTextN(X, TextY(Y), T, 46, C_BLACK);
  Y += gCH;

  if (!EFI_ERROR(gRT->GetTime(&Tm, NULL))) {
    UnicodeSPrint(T, sizeof(T), L"Date/time: %04u-%02u-%02u  %02u:%02u:%02u", (UINT32)Tm.Year,
                  (UINT32)Tm.Month, (UINT32)Tm.Day, (UINT32)Tm.Hour, (UINT32)Tm.Minute, (UINT32)Tm.Second);
    DrawText(X, TextY(Y), T, C_BLACK);
  }
}

STATIC
VOID
DrawAbout(CONST RECT *C)
{
  INT32  Y = C->Y0 + 6 * gS;
  INT32  X = C->X0 + 8 * gS;

  Fill(C->X0, C->Y0, C->X1 - C->X0, C->Y1 - C->Y0, C_FACE);
  DrawText(X, TextY(Y), L"AIOS " AIOS_VERSION, C_NAVY);
  Y += gCH * 2;
  DrawText(X, TextY(Y), L"A modern take on MS-DOS.", C_BLACK);
  Y += gCH;
  DrawText(X, TextY(Y), L"Runs as a UEFI application.", C_BLACK);
  Y += gCH * 2;
  DrawText(X, TextY(Y), L"F1 = start menu   F10 = exit", C_BLACK);
}

//
// ---------------------------------------------------------------
// Window drawing, taskbar, start menu
// ---------------------------------------------------------------
//
STATIC
VOID
DrawWindow(INT32 Slot, BOOLEAN Focused)
{
  RECT     R;
  RECT     C;
  RECT     B;
  CHAR16   Tmp[MAX_PATH + 24];
  CONST CHAR16  *Title;

  WinRect(Slot, &R);
  if (!RectsTouch(&R, &gDirty)) {
    return;
  }

  ClipTo(R.X0, R.Y0, R.X1, R.Y1);
  Bevel(R.X0, R.Y0, R.X1 - R.X0, R.Y1 - R.Y0, TRUE);

  // title bar
  DrawTitleBar(R.X0 + gB, R.Y0 + gB, R.X1 - R.X0 - 2 * gB, gTH, Focused);
  Title = WinTitle(Slot, Tmp, sizeof(Tmp));
  DrawTextN(R.X0 + gB + 3 * gS, TextY(R.Y0 + gB + (gTH - gCH) / 2), Title,
            (UINTN)((R.X1 - R.X0 - 2 * gB - 3 * (gTH)) / gCW), C_WHITE);

  TitleBtn(Slot, 0, &B);
  Button(B.X0, B.Y0, B.X1 - B.X0, B.Y1 - B.Y0, L"x", FALSE);
  if (HasMax(gWins[Slot].Type)) {
    TitleBtn(Slot, 1, &B);
    Button(B.X0, B.Y0, B.X1 - B.X0, B.Y1 - B.Y0, gWins[Slot].Max ? L"=" : L"+", FALSE);
  }
  TitleBtn(Slot, 2, &B);
  Button(B.X0, B.Y0, B.X1 - B.X0, B.Y1 - B.Y0, L"_", FALSE);

  // client
  WinClient(Slot, &C);
  ClipTo(C.X0, C.Y0, C.X1, C.Y1);

  switch (gWins[Slot].Type) {
    case W_TERM:
      DrawTerm(Slot, &C, Focused);
      break;
    case W_FILES:
      DrawFiles(Slot, &C);
      break;
    case W_NOTE:
      DrawNote(Slot, &C, Focused);
      break;
    case W_SYSINFO:
      DrawSysInfo(&C);
      break;
    case W_ABOUT:
      DrawAbout(&C);
      break;
    default:
      break;
  }
}

STATIC
INT32
TaskBtnWidth(VOID)
{
  INT32  Avail = gSW - (8 * gCW + 6 * gS) - (10 * gCW + 6 * gS);
  INT32  W = 14 * gCW;

  if (gWinCount > 0 && Avail / (INT32)gWinCount < W) {
    W = Avail / (INT32)gWinCount;
  }
  return Imax(W, 4 * gCW);
}

STATIC
VOID
MenuRect(RECT *R)
{
  INT32  ItemH = gCH + 6 * gS;
  INT32  W = 18 * gCW;
  INT32  H = 6 * ItemH + 4 * gS;

  SetRect(R, 0, gSH - gTB - H, W, H);
}

STATIC
VOID
DrawTaskbar(VOID)
{
  INT32     Y = gSH - gTB;
  INT32     X;
  INT32     Bw = TaskBtnWidth();
  UINTN     I;
  INT32     Focus = FocusSlot();
  CHAR16    T[40];
  CHAR16    Tmp[MAX_PATH + 24];
  EFI_TIME  Tm;
  INT32     Slot;
  CONST CHAR16  *Title;

  ClipTo(0, Y, gSW, gSH);
  Bevel(0, Y, gSW, gTB, TRUE);

  Button(2 * gS, Y + 2 * gS, 8 * gCW, gTB - 4 * gS, L"AIOS", gMenuOpen);

  X = 8 * gCW + 6 * gS;
  for (I = 0; I < gWinCount; I++) {
    Slot = gOrder[I];
    Title = WinTitle(Slot, Tmp, sizeof(Tmp));
    Bevel(X, Y + 2 * gS, Bw - 2 * gS, gTB - 4 * gS, (BOOLEAN)(Slot != Focus || gWins[Slot].Minimized));
    DrawTextN(X + 3 * gS, TextY(Y + (gTB - gCH) / 2), Title, (UINTN)((Bw - 8 * gS) / gCW), C_BLACK);
    X += Bw;
  }

  if (gKbMouse) {
    DrawText(gSW - 22 * gCW - 6 * gS, TextY(Y + (gTB - gCH) / 2), L"[KEY-MOUSE]", 0x800000);
  }

  if (!EFI_ERROR(gRT->GetTime(&Tm, NULL))) {
    UnicodeSPrint(T, sizeof(T), L"%02u:%02u:%02u", (UINT32)Tm.Hour, (UINT32)Tm.Minute, (UINT32)Tm.Second);
    Sunken(gSW - 10 * gCW - 6 * gS, Y + 3 * gS, 10 * gCW, gTB - 6 * gS, C_FACE);
    DrawText(gSW - 9 * gCW - 6 * gS, TextY(Y + (gTB - gCH) / 2), T, C_BLACK);
  }
}

STATIC
VOID
DrawMenu(VOID)
{
  RECT   R;
  INT32  I;
  INT32  ItemH = gCH + 6 * gS;

  MenuRect(&R);
  if (!RectsTouch(&R, &gDirty)) {
    return;
  }

  ClipTo(R.X0, R.Y0, R.X1, R.Y1);
  Bevel(R.X0, R.Y0, R.X1 - R.X0, R.Y1 - R.Y0, TRUE);

  for (I = 0; I < 6; I++) {
    INT32  Y = R.Y0 + 2 * gS + I * ItemH;
    BOOLEAN  Hot = (BOOLEAN)(I == gMenuSel);

    if (Hot) {
      Fill(R.X0 + 2 * gS, Y, R.X1 - R.X0 - 4 * gS, ItemH, C_NAVY);
    }
    DrawText(R.X0 + 8 * gS, TextY(Y + 3 * gS), gMenuItems[I], Hot ? C_WHITE : C_BLACK);
    if (I == 4) {
      Fill(R.X0 + 4 * gS, Y + ItemH - gS, R.X1 - R.X0 - 8 * gS, gS, C_SH);
    }
  }
}

//
// Desktop icons (double-click to open)
//
#define ICON_COUNT  4

STATIC CONST CHAR16  *gIconNames[ICON_COUNT] = { L"Terminal", L"Files", L"Notepad", L"System" };
STATIC INT32         gIconSel = -1;

STATIC
VOID
IconRect(INT32 I, RECT *R)
{
  INT32  CellW = 10 * gCW;
  INT32  CellH = 32 + gCH + 14;

  SetRect(R, 10, 28 + I * (CellH + 8), CellW, CellH);
}

STATIC
VOID
DrawIconPic(INT32 Kind, INT32 X, INT32 Y)
{
  INT32  I;

  switch (Kind) {
    case 0:                                         // terminal
      Fill(X + 2, Y + 4, 28, 22, 0x404040);
      Fill(X + 4, Y + 6, 24, 18, C_BLACK);
      DrawGlyph(X + 8, Y + 8, L'>', 0x55FF55);
      Fill(X + 12, Y + 26, 8, 3, 0x808080);
      Fill(X + 8, Y + 29, 16, 2, 0x808080);
      break;
    case 1:                                         // folder
      Fill(X + 2, Y + 6, 12, 4, 0xC0A020);
      Fill(X + 2, Y + 9, 28, 19, 0xFFD040);
      Fill(X + 2, Y + 9, 28, 2, 0xFFE890);
      Fill(X + 2, Y + 26, 28, 2, 0xB08810);
      break;
    case 2:                                         // notepad
      Fill(X + 6, Y + 2, 20, 28, C_WHITE);
      Fill(X + 6, Y + 2, 20, 4, C_NAVY);
      Fill(X + 6, Y + 2, 2, 28, C_SH);
      Fill(X + 24, Y + 2, 2, 28, C_SH);
      Fill(X + 6, Y + 28, 20, 2, C_SH);
      for (I = 0; I < 5; I++) {
        Fill(X + 10, Y + 10 + I * 4, 12, 2, 0x4040C0);
      }
      break;
    default:                                        // system info
      Fill(X + 4, Y + 4, 24, 22, C_NAVY);
      Fill(X + 6, Y + 6, 20, 16, 0x40A0E0);
      DrawGlyph(X + 16 - 4 * gS, Y + 15 - 4 * gS, L'i', C_WHITE);
      Fill(X + 12, Y + 26, 8, 3, 0x808080);
      Fill(X + 8, Y + 29, 16, 2, 0x808080);
      break;
  }
}

STATIC
VOID
DrawDesktopIcons(VOID)
{
  INT32  I;
  RECT   R;
  INT32  Tw;

  for (I = 0; I < ICON_COUNT; I++) {
    IconRect(I, &R);
    if (!RectsTouch(&R, &gDirty)) {
      continue;
    }
    ClipTo(R.X0, R.Y0, R.X1, R.Y1);
    DrawIconPic(I, R.X0 + (R.X1 - R.X0 - 32) / 2, R.Y0 + 2);

    Tw = (INT32)StrLen(gIconNames[I]) * gCW;
    if (I == gIconSel) {
      Fill(R.X0 + (R.X1 - R.X0 - Tw) / 2 - 2 * gS, R.Y0 + 36, Tw + 4 * gS, gCH, C_NAVY);
    }
    DrawText(R.X0 + (R.X1 - R.X0 - Tw) / 2, TextY(R.Y0 + 36), gIconNames[I], C_WHITE);
  }
}

STATIC
VOID
ToggleMax(INT32 Slot)
{
  WIN        *W = &gWins[Slot];
  RECT       C;
  FLAY       Lf;
  NLAY       Ln;

  InvalWin(Slot);
  if (!W->Max) {
    W->Rx = W->X;
    W->Ry = W->Y;
    W->Rw = W->W;
    W->Rh = W->H;
    W->X = 0;
    W->Y = 0;
    W->W = gSW;
    W->H = gSH - gTB;
    W->Max = TRUE;
  } else {
    W->X = W->Rx;
    W->Y = W->Ry;
    W->W = W->Rw;
    W->H = W->Rh;
    W->Max = FALSE;
  }
  InvalWin(Slot);

  WinClient(Slot, &C);
  if (W->Type == W_FILES) {
    FilesLayout(&C, &Lf);
    FilesEnsureVisible((FILESDATA *)W->Data, Lf.Rows);
  } else if (W->Type == W_NOTE) {
    NoteLayout(&C, &Ln);
    NoteVisible((NOTEDATA *)W->Data, Ln.Cols, Ln.Rows);
  }
}

STATIC
VOID
GuiCompose(VOID)
{
  UINTN  I;
  INT32  Slot;
  INT32  Focus = FocusSlot();

  ClipTo(0, 0, gSW, gSH);
  Fill(0, 0, gSW, gSH, C_DESK);
  DrawDesktopIcons();
  ClipTo(0, 0, gSW, gSH);
  DrawText(gSW - 14 * gCW, gSH - gTB - 2 * gCH, L"AIOS " AIOS_VERSION, 0x40A0A0);

  for (I = 0; I < gWinCount; I++) {
    Slot = gOrder[I];
    if (gWins[Slot].Used && !gWins[Slot].Minimized) {
      DrawWindow(Slot, (BOOLEAN)(Slot == Focus));
    }
  }

  DrawTaskbar();
  if (gMenuOpen) {
    DrawMenu();
  }
}

//
// ---------------------------------------------------------------
// Presenting to the screen (with the mouse cursor on top)
// ---------------------------------------------------------------
//
STATIC
VOID
Blit(INT32 X0, INT32 Y0, INT32 X1, INT32 Y1)
{
  X0 = Imax(X0, 0);
  Y0 = Imax(Y0, 0);
  X1 = Imin(X1, gSW);
  Y1 = Imin(Y1, gSH);

  if (X0 >= X1 || Y0 >= Y1) {
    return;
  }

  gGop->Blt(gGop, (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)gFrame, EfiBltBufferToVideo,
            (UINTN)X0, (UINTN)Y0, (UINTN)X0, (UINTN)Y0,
            (UINTN)(X1 - X0), (UINTN)(Y1 - Y0), (UINTN)gSW * 4);
}

STATIC
VOID
CopySceneToFrame(INT32 X0, INT32 Y0, INT32 X1, INT32 Y1)
{
  INT32  Y;

  X0 = Imax(X0, 0);
  Y0 = Imax(Y0, 0);
  X1 = Imin(X1, gSW);
  Y1 = Imin(Y1, gSH);

  if (X0 >= X1 || Y0 >= Y1) {
    return;
  }

  for (Y = Y0; Y < Y1; Y++) {
    CopyMem(gFrame + (UINTN)Y * (UINTN)gSW + (UINTN)X0,
            gScene + (UINTN)Y * (UINTN)gSW + (UINTN)X0,
            (UINTN)(X1 - X0) * 4);
  }
}

STATIC
VOID
CursorSize(INT32 *W, INT32 *H)
{
  *W = 11 * gS;
  *H = 17 * gS;
}

STATIC
VOID
DrawCursorToFrame(INT32 Mx, INT32 My)
{
  INT32   Row;
  INT32   Col;
  INT32   Sy;
  INT32   Sx;
  INT32   Px;
  INT32   Py;
  CHAR8   Ch;
  UINT32  Color;

  for (Row = 0; Row < 17; Row++) {
    for (Col = 0; Col < 11; Col++) {
      Ch = gArrow[Row][Col];
      if (Ch != 'X' && Ch != '.') {
        continue;
      }
      Color = (Ch == 'X') ? C_BLACK : C_WHITE;
      for (Sy = 0; Sy < gS; Sy++) {
        for (Sx = 0; Sx < gS; Sx++) {
          Px = Mx + Col * gS + Sx;
          Py = My + Row * gS + Sy;
          if (Px >= 0 && Px < gSW && Py >= 0 && Py < gSH) {
            gFrame[(UINTN)Py * (UINTN)gSW + (UINTN)Px] = Color;
          }
        }
      }
    }
  }
}

STATIC
VOID
PresentDirty(VOID)
{
  INT32  Cw;
  INT32  Ch;
  RECT   Cr;

  CopySceneToFrame(gDirty.X0, gDirty.Y0, gDirty.X1, gDirty.Y1);

  CursorSize(&Cw, &Ch);
  SetRect(&Cr, gMx, gMy, Cw, Ch);
  if (RectsTouch(&Cr, &gDirty)) {
    DrawCursorToFrame(gMx, gMy);
  }

  Blit(gDirty.X0, gDirty.Y0, gDirty.X1, gDirty.Y1);
  if (RectsTouch(&Cr, &gDirty)) {
    gCurX = gMx;
    gCurY = gMy;
  }
}

STATIC
VOID
CursorOnlyUpdate(VOID)
{
  INT32  Cw;
  INT32  Ch;

  if (gCurX == gMx && gCurY == gMy) {
    return;
  }

  CursorSize(&Cw, &Ch);

  CopySceneToFrame(gCurX, gCurY, gCurX + Cw, gCurY + Ch);
  DrawCursorToFrame(gMx, gMy);
  Blit(gCurX, gCurY, gCurX + Cw, gCurY + Ch);
  Blit(gMx, gMy, gMx + Cw, gMy + Ch);

  gCurX = gMx;
  gCurY = gMy;
}

STATIC
VOID
GuiRender(VOID)
{
  INT32  Cw;
  INT32  Ch;

  CursorSize(&Cw, &Ch);

  if (gHasDirty) {
    // Erase the old cursor first: its pixels may lie outside the dirty area.
    if (gCurX > -500) {
      CopySceneToFrame(gCurX, gCurY, gCurX + Cw, gCurY + Ch);
      Blit(gCurX, gCurY, gCurX + Cw, gCurY + Ch);
      gCurX = -1000;
      gCurY = -1000;
    }

    GuiCompose();
    PresentDirty();
    gHasDirty = FALSE;

    if (gCurX != gMx || gCurY != gMy) {
      DrawCursorToFrame(gMx, gMy);
      Blit(gMx, gMy, gMx + Cw, gMy + Ch);
      gCurX = gMx;
      gCurY = gMy;
    }
  } else if (gCursorMoved) {
    CursorOnlyUpdate();
  }
  gCursorMoved = FALSE;
}

//
// ---------------------------------------------------------------
// Pointer input
// ---------------------------------------------------------------
//
//
// Firmware often has no mouse driver at all (QEMU/OVMF is one example),
// so AIOS also talks to the PS/2 mouse hardware directly through the
// keyboard controller (ports 60h/64h). This works in QEMU, VMware and
// on laptops with a PS/2 touchpad.
//
STATIC EFI_CPU_IO2_PROTOCOL  *gCpuIo = NULL;
STATIC UINT8                 gPs2Pkt[3];
STATIC UINTN                 gPs2N = 0;

STATIC
UINT8
KbcIn(UINT16 Port)
{
  UINT8  V = 0xFF;

  if (gCpuIo != NULL) {
    gCpuIo->Io.Read(gCpuIo, EfiCpuIoWidthUint8, Port, 1, &V);
  }
  return V;
}

STATIC
VOID
KbcOut(UINT16 Port, UINT8 V)
{
  if (gCpuIo != NULL) {
    gCpuIo->Io.Write(gCpuIo, EfiCpuIoWidthUint8, Port, 1, &V);
  }
}

STATIC
BOOLEAN
KbcWaitIn(VOID)
{
  UINTN  I;

  for (I = 0; I < 2000; I++) {
    if ((KbcIn(0x64) & 0x02) == 0) {
      return TRUE;
    }
    gBS->Stall(10);
  }
  return FALSE;
}

STATIC
BOOLEAN
KbcWaitOut(VOID)
{
  UINTN  I;

  for (I = 0; I < 2000; I++) {
    if ((KbcIn(0x64) & 0x01) != 0) {
      return TRUE;
    }
    gBS->Stall(10);
  }
  return FALSE;
}

STATIC
BOOLEAN
MouseCmd(UINT8 Cmd)
{
  if (!KbcWaitIn()) {
    return FALSE;
  }
  KbcOut(0x64, 0xD4);
  if (!KbcWaitIn()) {
    return FALSE;
  }
  KbcOut(0x60, Cmd);
  if (!KbcWaitOut()) {
    return FALSE;
  }
  return (BOOLEAN)(KbcIn(0x60) == 0xFA);
}

STATIC
VOID
Ps2MouseInit(VOID)
{
  UINT8  Cmd;
  UINTN  I;

  gPs2Ok = FALSE;
  gPs2N = 0;

  if (gNoPs2) {
    return;
  }

  if (gCpuIo == NULL &&
      EFI_ERROR(gBS->LocateProtocol(&gEfiCpuIo2ProtocolGuid, NULL, (VOID **)&gCpuIo))) {
    return;
  }
  if (KbcIn(0x64) == 0xFF) {                      // no keyboard controller
    return;
  }

  for (I = 0; I < 32 && (KbcIn(0x64) & 0x01) != 0; I++) {
    KbcIn(0x60);
  }

  if (!KbcWaitIn()) {
    return;
  }
  KbcOut(0x64, 0xA8);                             // enable the mouse port

  if (!KbcWaitIn()) {
    return;
  }
  KbcOut(0x64, 0x20);                             // read controller configuration
  if (!KbcWaitOut()) {
    return;
  }
  Cmd = KbcIn(0x60);
  Cmd = (UINT8)(Cmd & ~0x20);                     // mouse clock on

  if (!KbcWaitIn()) {
    return;
  }
  KbcOut(0x64, 0x60);
  if (!KbcWaitIn()) {
    return;
  }
  KbcOut(0x60, Cmd);

  MouseCmd(0xF6);                                 // set defaults
  gPs2Ok = MouseCmd(0xF4);                        // enable data reporting
}

STATIC
VOID
Ps2Poll(VOID)
{
  UINTN  N;
  UINT8  St;
  UINT8  B;
  INT32  Dx;
  INT32  Dy;

  if (!gPs2Ok) {
    return;
  }

  for (N = 0; N < 24; N++) {
    St = KbcIn(0x64);
    if ((St & 0x21) != 0x21) {                    // need: output full + from mouse
      break;
    }
    B = KbcIn(0x60);

    if (gPs2N == 0 && (B & 0x08) == 0) {          // out of sync, skip
      continue;
    }
    gPs2Pkt[gPs2N++] = B;

    if (gPs2N == 3) {
      gPs2N = 0;
      Dx = (INT32)gPs2Pkt[1] - (INT32)(((UINT32)gPs2Pkt[0] << 4) & 0x100);
      Dy = (INT32)gPs2Pkt[2] - (INT32)(((UINT32)gPs2Pkt[0] << 3) & 0x100);
      if ((gPs2Pkt[0] & 0xC0) != 0) {
        Dx = 0;
        Dy = 0;
      }
      if (Dx > 6 || Dx < -6) {
        Dx *= 2;
      }
      if (Dy > 6 || Dy < -6) {
        Dy *= 2;
      }
      gMx = Imax(0, Imin(gSW - 1, gMx + Dx));
      gMy = Imax(0, Imin(gSH - 1, gMy - Dy));
      gBtn = (BOOLEAN)((gPs2Pkt[0] & 0x01) != 0);
      gPtrEventsRel++;
    }
  }
}

STATIC
VOID
ConnectAllControllers(VOID)
{
  EFI_HANDLE  *All = NULL;
  UINTN       N = 0;
  UINTN       I;

  if (!EFI_ERROR(gBS->LocateHandleBuffer(AllHandles, NULL, NULL, &N, &All))) {
    for (I = 0; I < N; I++) {
      gBS->ConnectController(All[I], NULL, NULL, TRUE);
    }
    gBS->FreePool(All);
  }
}

STATIC
VOID
InitPointers(VOID)
{
  EFI_HANDLE  *H = NULL;
  UINTN       N = 0;
  UINTN       I;

  gSpCount = 0;
  gApCount = 0;

  ConnectAllControllers();

  if (!EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiSimplePointerProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N && gSpCount < 4; I++) {
      EFI_SIMPLE_POINTER_PROTOCOL  *P = NULL;
      if (!EFI_ERROR(gBS->HandleProtocol(H[I], &gEfiSimplePointerProtocolGuid, (VOID **)&P)) && P != NULL) {
        P->Reset(P, TRUE);
        gSp[gSpCount++] = P;
      }
    }
    gBS->FreePool(H);
  }

  H = NULL;
  N = 0;
  if (!EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiAbsolutePointerProtocolGuid, NULL, &N, &H))) {
    for (I = 0; I < N && gApCount < 4; I++) {
      EFI_ABSOLUTE_POINTER_PROTOCOL  *P = NULL;
      if (!EFI_ERROR(gBS->HandleProtocol(H[I], &gEfiAbsolutePointerProtocolGuid, (VOID **)&P)) && P != NULL) {
        P->Reset(P, TRUE);
        gAp[gApCount++] = P;
      }
    }
    gBS->FreePool(H);
  }

  Ps2MouseInit();
}

STATIC
VOID
PollPointers(VOID)
{
  UINTN                         I;
  EFI_SIMPLE_POINTER_STATE      Ss;
  EFI_ABSOLUTE_POINTER_STATE    As;
  INT32                         Dx;
  INT32                         Dy;
  UINT64                        Range;
  INT32                         OldX = gMx;
  INT32                         OldY = gMy;

  for (I = 0; I < gSpCount; I++) {
    if (EFI_ERROR(gSp[I]->GetState(gSp[I], &Ss))) {
      continue;
    }
    gPtrEventsRel++;
    Dx = (INT32)Ss.RelativeMovementX;
    Dy = (INT32)Ss.RelativeMovementY;
    if (Dx > 6 || Dx < -6) {
      Dx *= 2;
    }
    if (Dy > 6 || Dy < -6) {
      Dy *= 2;
    }
    gMx = Imax(0, Imin(gSW - 1, gMx + Dx));
    gMy = Imax(0, Imin(gSH - 1, gMy + Dy));
    gBtn = Ss.LeftButton;
  }

  for (I = 0; I < gApCount; I++) {
    if (EFI_ERROR(gAp[I]->GetState(gAp[I], &As))) {
      continue;
    }
    gPtrEventsAbs++;
    Range = gAp[I]->Mode->AbsoluteMaxX - gAp[I]->Mode->AbsoluteMinX;
    if (Range > 0) {
      gMx = (INT32)(((As.CurrentX - gAp[I]->Mode->AbsoluteMinX) * (UINT64)(gSW - 1)) / Range);
    }
    Range = gAp[I]->Mode->AbsoluteMaxY - gAp[I]->Mode->AbsoluteMinY;
    if (Range > 0) {
      gMy = (INT32)(((As.CurrentY - gAp[I]->Mode->AbsoluteMinY) * (UINT64)(gSH - 1)) / Range);
    }
    gMx = Imax(0, Imin(gSW - 1, gMx));
    gMy = Imax(0, Imin(gSH - 1, gMy));
    gBtn = (BOOLEAN)((As.ActiveButtons & 1) != 0);
  }

  Ps2Poll();

  if (gMx != OldX || gMy != OldY) {
    gCursorMoved = TRUE;
  }
}

//
// ---------------------------------------------------------------
// Mouse actions
// ---------------------------------------------------------------
//
STATIC
VOID
MenuAction(INT32 Item)
{
  INT32  Slot;

  switch (Item) {
    case 0:
      OpenTerm();
      break;
    case 1:
      OpenFiles();
      break;
    case 2:
      OpenNote(0, L"\\NOTE.TXT");
      break;
    case 3:
      Slot = FindWin(W_SYSINFO);
      if (Slot >= 0) {
        BringToFront(Slot);
      } else {
        NewWin(W_SYSINFO, 48 * gCW, 10 * gCH, NULL);
      }
      break;
    case 4:
      Slot = FindWin(W_ABOUT);
      if (Slot >= 0) {
        BringToFront(Slot);
      } else {
        NewWin(W_ABOUT, 32 * gCW, 8 * gCH, NULL);
      }
      break;
    case 5:
      gGuiQuit = TRUE;
      gAbortRead = TRUE;
      break;
    default:
      break;
  }
}

STATIC
VOID
HandlePress(VOID)
{
  RECT    M;
  RECT    R;
  RECT    C;
  RECT    B;
  INT32   I;
  INT32   Slot;
  INT32   ItemH = gCH + 6 * gS;
  INT32   X;
  INT32   Bw;
  BOOLEAN Dbl;

  Dbl = (BOOLEAN)(gTicks - gLastClickTick < (gTicksPerSec * 6) / 10 &&
                  Imax(gMx - gLastClickX, gLastClickX - gMx) < 6 * gS &&
                  Imax(gMy - gLastClickY, gLastClickY - gMy) < 6 * gS);
  gLastClickTick = gTicks;
  gLastClickX = gMx;
  gLastClickY = gMy;

  // start menu
  if (gMenuOpen) {
    MenuRect(&M);
    if (RectHit(&M, gMx, gMy)) {
      I = (gMy - M.Y0 - 2 * gS) / ItemH;
      gMenuOpen = FALSE;
      Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
      if (I >= 0 && I < 6) {
        MenuAction(I);
      }
      return;
    }
    gMenuOpen = FALSE;
    Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
    Inval(0, gSH - gTB, 10 * gCW, gTB);
  }

  // taskbar
  if (gMy >= gSH - gTB) {
    if (gMx < 8 * gCW + 4 * gS) {
      gMenuOpen = TRUE;
      gMenuSel = 0;
      MenuRect(&M);
      Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
      Inval(0, gSH - gTB, 10 * gCW, gTB);
      return;
    }
    Bw = TaskBtnWidth();
    X = 8 * gCW + 6 * gS;
    for (I = 0; I < (INT32)gWinCount; I++) {
      Slot = gOrder[I];
      if (gMx >= X && gMx < X + Bw) {
        if (gWins[Slot].Minimized) {
          BringToFront(Slot);
        } else if (Slot == FocusSlot()) {
          gWins[Slot].Minimized = TRUE;
          InvalWin(Slot);
          InvalFocus();
          Inval(0, gSH - gTB, gSW, gTB);
        } else {
          BringToFront(Slot);
        }
        Inval(0, gSH - gTB, gSW, gTB);
        return;
      }
      X += Bw;
    }
    return;
  }

  // windows, top first
  for (I = (INT32)gWinCount - 1; I >= 0; I--) {
    Slot = gOrder[I];
    if (!gWins[Slot].Used || gWins[Slot].Minimized) {
      continue;
    }
    WinRect(Slot, &R);
    if (!RectHit(&R, gMx, gMy)) {
      continue;
    }

    if (Slot != FocusSlot()) {
      BringToFront(Slot);
      Inval(0, gSH - gTB, gSW, gTB);
    }

    TitleBtn(Slot, 0, &B);
    if (RectHit(&B, gMx, gMy)) {
      CloseWin(Slot);
      Inval(0, gSH - gTB, gSW, gTB);
      return;
    }
    if (HasMax(gWins[Slot].Type)) {
      TitleBtn(Slot, 1, &B);
      if (RectHit(&B, gMx, gMy)) {
        ToggleMax(Slot);
        return;
      }
    }
    TitleBtn(Slot, 2, &B);
    if (RectHit(&B, gMx, gMy)) {
      gWins[Slot].Minimized = TRUE;
      InvalWin(Slot);
      InvalFocus();
      Inval(0, gSH - gTB, gSW, gTB);
      return;
    }

    WinClient(Slot, &C);
    if (gMy < C.Y0) {
      if (HasMax(gWins[Slot].Type) && Dbl) {
        ToggleMax(Slot);
        return;
      }
      if (!gWins[Slot].Max) {
        gDrag = Slot;
        gDragDX = gMx - gWins[Slot].X;
        gDragDY = gMy - gWins[Slot].Y;
      }
      return;
    }

    switch (gWins[Slot].Type) {
      case W_FILES:
        FilesClick(Slot, &C, gMx, gMy, Dbl);
        break;
      case W_NOTE:
        NoteClick(Slot, &C, gMx, gMy);
        break;
      default:
        break;
    }
    return;
  }

  // desktop icons
  {
    INT32  Hit = -1;
    INT32  Old = gIconSel;

    for (I = 0; I < ICON_COUNT; I++) {
      IconRect(I, &R);
      if (RectHit(&R, gMx, gMy)) {
        Hit = I;
        break;
      }
    }

    gIconSel = Hit;
    if (Old >= 0 && Old != Hit) {
      IconRect(Old, &R);
      Inval(R.X0, R.Y0, R.X1 - R.X0, R.Y1 - R.Y0);
    }
    if (Hit >= 0) {
      IconRect(Hit, &R);
      Inval(R.X0, R.Y0, R.X1 - R.X0, R.Y1 - R.Y0);
      if (Dbl) {
        gIconSel = -1;
        MenuAction(Hit);
      }
    }
  }
}

STATIC
VOID
HandleDrag(VOID)
{
  INT32  Nx;
  INT32  Ny;

  if (gDrag < 0 || !gWins[gDrag].Used) {
    return;
  }

  Nx = gMx - gDragDX;
  Ny = gMy - gDragDY;
  Ny = Imax(0, Imin(Ny, gSH - gTB - gTH - 2 * gB));
  Nx = Imax(-(gWins[gDrag].W - 4 * gCW), Imin(Nx, gSW - 4 * gCW));

  if (Nx != gWins[gDrag].X || Ny != gWins[gDrag].Y) {
    InvalWin(gDrag);
    gWins[gDrag].X = Nx;
    gWins[gDrag].Y = Ny;
    InvalWin(gDrag);
  }
}

//
// ---------------------------------------------------------------
// Keyboard routing
// ---------------------------------------------------------------
//
STATIC
BOOLEAN
GuiKeyFilter(EFI_INPUT_KEY *Key)
{
  INT32  Slot;
  RECT   C;

  if (Key->ScanCode == SCAN_F10) {
    gGuiQuit = TRUE;
    gAbortRead = TRUE;
    return TRUE;
  }

  if (Key->ScanCode == SCAN_F1) {
    RECT  M;
    gMenuOpen = (BOOLEAN)!gMenuOpen;
    gMenuSel = 0;
    MenuRect(&M);
    Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
    Inval(0, gSH - gTB, 10 * gCW, gTB);
    return TRUE;
  }

  if (Key->ScanCode == SCAN_F3) {                 // next window
    UINTN  K;
    INT32  Focus = FocusSlot();

    for (K = 0; K < gWinCount; K++) {
      if (gWins[gOrder[K]].Used && !gWins[gOrder[K]].Minimized && gOrder[K] != Focus) {
        BringToFront(gOrder[K]);
        Inval(0, gSH - gTB, gSW, gTB);
        break;
      }
    }
    return TRUE;
  }

  if (Key->ScanCode == SCAN_F4) {                 // close the focused window
    Slot = FocusSlot();
    if (Slot >= 0) {
      CloseWin(Slot);
    }
    return TRUE;
  }

  if (Key->ScanCode == SCAN_F2) {
    gKbMouse = (BOOLEAN)!gKbMouse;
    gKbDrag = FALSE;
    gDrag = -1;
    Inval(0, gSH - gTB, gSW, gTB);
    return TRUE;
  }

  if (gMenuOpen && !gKbMouse) {
    RECT  M;

    MenuRect(&M);
    if (Key->ScanCode == SCAN_UP && gMenuSel > 0) {
      gMenuSel--;
    } else if (Key->ScanCode == SCAN_DOWN && gMenuSel < 5) {
      gMenuSel++;
    } else if (Key->ScanCode == SCAN_ESC) {
      gMenuOpen = FALSE;
    } else if (Key->UnicodeChar == CHAR_CARRIAGE_RETURN) {
      gMenuOpen = FALSE;
      MenuAction(gMenuSel);
    }
    Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
    Inval(0, gSH - gTB, 10 * gCW, gTB);
    return TRUE;
  }

  if (gKbMouse) {
    INT32  Step = 8 * gS;
    INT32  Ox = gMx;
    INT32  Oy = gMy;

    switch (Key->ScanCode) {
      case SCAN_UP:
        gMy -= Step;
        break;
      case SCAN_DOWN:
        gMy += Step;
        break;
      case SCAN_LEFT:
        gMx -= Step;
        break;
      case SCAN_RIGHT:
        gMx += Step;
        break;
      case SCAN_PAGE_UP:
        gMy -= Step * 4;
        break;
      case SCAN_PAGE_DOWN:
        gMy += Step * 4;
        break;
      case SCAN_HOME:
        gMx -= Step * 4;
        break;
      case SCAN_END:
        gMx += Step * 4;
        break;
      default:
        break;
    }

    gMx = Imax(0, Imin(gSW - 1, gMx));
    gMy = Imax(0, Imin(gSH - 1, gMy));
    if (gMx != Ox || gMy != Oy) {
      gCursorMoved = TRUE;
      if (gKbDrag) {
        HandleDrag();
      }
    }

    if (Key->ScanCode == SCAN_ESC && gMenuOpen) {
      RECT  M;
      gMenuOpen = FALSE;
      MenuRect(&M);
      Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
      Inval(0, gSH - gTB, 10 * gCW, gTB);
      return TRUE;
    }

    if (Key->UnicodeChar == CHAR_CARRIAGE_RETURN) {          // click
      HandlePress();
      if (!gKbDrag) {
        gDrag = -1;
      }
    } else if (Key->UnicodeChar == CHAR_TAB) {               // double-click
      HandlePress();
      HandlePress();
      gDrag = -1;
    } else if (Key->UnicodeChar == L' ') {                   // hold / release (drag)
      if (!gKbDrag) {
        gKbDrag = TRUE;
        HandlePress();
      } else {
        gKbDrag = FALSE;
        gDrag = -1;
      }
    }
    return TRUE;
  }

  Slot = FocusSlot();
  if (Slot < 0) {
    return TRUE;
  }

  if (gWins[Slot].Type == W_TERM) {
    if (Key->ScanCode == SCAN_PAGE_UP || Key->ScanCode == SCAN_PAGE_DOWN) {
      UINTN  Step = (gVcRows > 2) ? gVcRows - 2 : 1;

      if (Key->ScanCode == SCAN_PAGE_UP) {
        gSbOff = (gSbOff + Step > gSbCount) ? gSbCount : gSbOff + Step;
      } else {
        gSbOff = (gSbOff > Step) ? gSbOff - Step : 0;
      }
      InvalClient(Slot);
      return TRUE;
    }
    if (gSbOff != 0) {
      gSbOff = 0;
      InvalClient(Slot);
    }
    return FALSE;                 // the shell takes it
  }

  WinClient(Slot, &C);
  switch (gWins[Slot].Type) {
    case W_FILES:
      FilesKey(Slot, &C, Key);
      break;
    case W_NOTE:
      NoteKey(Slot, &C, Key);
      break;
    default:
      break;
  }
  return TRUE;
}

//
// ---------------------------------------------------------------
// Main pump: called whenever the shell is waiting for a key
// ---------------------------------------------------------------
//
STATIC
VOID
GuiPump(VOID)
{
  EFI_TIME  Tm;
  INT32     Slot;
  BOOLEAN   WasDown = gPrevBtn;

  gTicks++;

  if (gTermDirty) {
    gTermDirty = FALSE;
    Slot = FindWin(W_TERM);
    if (Slot >= 0 && !gWins[Slot].Minimized) {
      InvalClient(Slot);
    }
  }

  PollPointers();

  if (gBtn && !WasDown) {
    HandlePress();
  } else if (gBtn && gDrag >= 0) {
    HandleDrag();
  } else if (!gBtn && !gKbDrag) {
    gDrag = -1;
  }
  gPrevBtn = gBtn;

  if (gMenuOpen && gCursorMoved) {
    RECT   M;
    INT32  Item;

    MenuRect(&M);
    if (RectHit(&M, gMx, gMy)) {
      Item = (gMy - M.Y0 - 2 * gS) / (gCH + 6 * gS);
      if (Item >= 0 && Item < 6 && Item != gMenuSel) {
        gMenuSel = Item;
        Inval(M.X0, M.Y0, M.X1 - M.X0, M.Y1 - M.Y0);
      }
    }
  }

  if (!EFI_ERROR(gRT->GetTime(&Tm, NULL)) && Tm.Second != gLastSec) {
    if (gLastSec != 99 && gTicks - gSecTick > 50) {
      gTicksPerSec = gTicks - gSecTick;           // how many loop passes make one real second
    }
    gSecTick = gTicks;
    gLastSec = Tm.Second;
    Inval(gSW - 10 * gCW - 6 * gS, gSH - gTB, 10 * gCW + 6 * gS, gTB);
    Slot = FindWin(W_SYSINFO);
    if (Slot >= 0 && !gWins[Slot].Minimized) {
      InvalClient(Slot);
    }
  }

  GuiRender();
}

//
// ---------------------------------------------------------------
// Start / stop
// ---------------------------------------------------------------
//
STATIC
BOOLEAN
GuiInit(INT32 ForceScale)
{
  EFI_STATUS  Status;
  UINTN       Bytes;

  if (EFI_ERROR(gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&gGop)) || gGop == NULL) {
    return FALSE;
  }

  gSW = (INT32)gGop->Mode->Info->HorizontalResolution;
  gSH = (INT32)gGop->Mode->Info->VerticalResolution;
  if (gSW < 640 || gSH < 400) {
    return FALSE;
  }

  gS = (ForceScale == 1 || ForceScale == 2) ? ForceScale : ((gSW >= 1024) ? 2 : 1);
  gCW = 8 * gS;
  gCH = 10 * gS;
  gB = 2 * gS;
  gTH = gCH + 2 * gS;
  gTB = gCH + 8 * gS;

  Bytes = (UINTN)gSW * (UINTN)gSH * 4;
  Status = gBS->AllocatePool(EfiBootServicesData, Bytes, (VOID **)&gScene);
  if (EFI_ERROR(Status)) {
    return FALSE;
  }
  Status = gBS->AllocatePool(EfiBootServicesData, Bytes, (VOID **)&gFrame);
  if (EFI_ERROR(Status)) {
    gBS->FreePool(gScene);
    gScene = NULL;
    return FALSE;
  }
  SetMem(gScene, Bytes, 0);
  SetMem(gFrame, Bytes, 0);

  ZeroMem(gWins, sizeof(gWins));
  gWinCount = 0;
  gMenuOpen = FALSE;
  gGuiQuit = FALSE;
  gDrag = -1;
  gMx = gSW / 2;
  gMy = gSH / 2;
  gBtn = FALSE;
  gPrevBtn = FALSE;
  gCurX = -1000;
  gCurY = -1000;
  gLastSec = 99;
  gPending[0] = L'\0';
  gTermDirty = FALSE;
  gHasDirty = FALSE;

  InitPointers();
  return TRUE;
}

STATIC
VOID
GuiShutdown(VOID)
{
  INT32  I;

  for (I = 0; I < GUI_MAX_WINS; I++) {
    if (gWins[I].Used) {
      BOOLEAN  Saved = gAbortRead;
      CloseWin(I);
      gAbortRead = Saved;
    }
  }
  if (gScene != NULL) {
    gBS->FreePool(gScene);
    gScene = NULL;
  }
  if (gFrame != NULL) {
    gBS->FreePool(gFrame);
    gFrame = NULL;
  }
}

STATIC
VOID
GuiPollKeysIdle(VOID)
{
  EFI_INPUT_KEY  Key;

  if (gBS->CheckEvent(gST->ConIn->WaitForKey) == EFI_SUCCESS) {
    if (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, &Key))) {
      GuiKeyFilter(&Key);
    }
  }
}

STATIC
VOID
GuiRun(INT32 ForceScale)
{
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *OrigOut = gST->ConOut;
  CHAR16                           Input[INPUT_SIZE];
  UINTN                            Cols;
  UINTN                            Rows;

  if (!GuiInit(ForceScale)) {
    Print(L"GUI: no usable graphics output (need at least 640x400).\r\n");
    return;
  }

  Cols = (UINTN)((gSW * 3 / 4 - 2 * gB) / gCW);
  Rows = (UINTN)((gSH * 3 / 4 - 2 * gB - gTH) / gCH);
  if (Cols > 80) {
    Cols = 80;
  }
  if (Rows > 30) {
    Rows = 30;
  }

  VconInit(Cols, Rows);
  OrigOut->EnableCursor(OrigOut, FALSE);
  gST->ConOut = &gVcProto;

  gIdleHook = GuiPump;
  gKeyFilter = GuiKeyFilter;
  gAbortRead = FALSE;

  OpenTerm();
  PrintBanner();
  Print(L"Desktop: F1 menu  F2 key-mouse  F3 next  F4 close  F10 exit\r\n");
  InvalAll();

  while (!gGuiQuit) {
    if (gPending[0] != L'\0') {
      StrCpyS(Input, INPUT_SIZE, gPending);
      gPending[0] = L'\0';
      gAbortRead = FALSE;
      Print(L"\r\n%s\r\n", Input);
      RunCommand(Input);
      gExit = FALSE;
      continue;
    }

    if (FindWin(W_TERM) >= 0) {
      gAbortRead = FALSE;
      PrintPrompt();
      ReadCommandLine(Input, INPUT_SIZE);

      if (gAbortRead) {
        gAbortRead = FALSE;
        continue;
      }

      AddHistory(Input);
      RunCommand(Input);

      if (gExit) {
        gExit = FALSE;
        CloseWin(FindWin(W_TERM));
        gAbortRead = FALSE;
      }
    } else {
      GuiPump();
      GuiPollKeysIdle();
      gBS->Stall(1000);
    }
  }

  gIdleHook = NULL;
  gKeyFilter = NULL;
  gAbortRead = FALSE;
  gST->ConOut = OrigOut;
  GuiShutdown();

  OrigOut->EnableCursor(OrigOut, TRUE);
  OrigOut->SetAttribute(OrigOut, gAttr);
  OrigOut->ClearScreen(OrigOut);
}

STATIC
VOID
CmdGui(CHAR16 *Args)
{
  CHAR16  Arg[16];
  CHAR16  *C = Args;
  INT32   Scale = 0;

  gNoPs2 = FALSE;

  while (NextToken(&C, Arg, 16)) {
    if (StrCmp(Arg, L"1") == 0) {
      Scale = 1;
    } else if (StrCmp(Arg, L"2") == 0) {
      Scale = 2;
    } else if (SameText(Arg, L"NOPS2")) {
      gNoPs2 = TRUE;
    } else {
      Print(L"Usage: GUI [1|2] [nops2]\r\n");
      Print(L"  1 / 2   small / large text\r\n");
      Print(L"  nops2   do not probe the PS/2 mouse (use F2 key-mouse instead)\r\n");
      return;
    }
  }

  GuiRun(Scale);
}
