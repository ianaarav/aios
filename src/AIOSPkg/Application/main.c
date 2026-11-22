/*
 * AIOS 0.6.0 - a modern take on MS-DOS, running as a UEFI application.
 *
 * New in 0.6.0:
 *   - GUI: a desktop with windows, mouse, taskbar and start menu (type GUI).
 *     Apps: Terminal (the real shell), Files, Notepad, System Info.
 *
 * From 0.5.0:
 *   - "Did you mean?" for mistyped commands and file names
 *   - Tab completion for commands and files (press Tab again to cycle)
 *   - Drive letters: C:, D:, E: ... for every file system UEFI can see
 *     (USB sticks, internal HDD/SSD partitions); type "D:" to switch
 *   - VOL (list drives) and DISKS (list physical disks)
 *   - REN (rename) and RUN (start any .EFI program, or just type its name)
 *   - Cross-drive paths:  COPY C:\a.txt D:\backup\a.txt
 *
 * From 0.4.0: line editor, history, DIR/CD/TYPE/MKDIR/DEL/COPY/TOUCH/INFO/PWD,
 * COLOR, MEM, REBOOT, SHUTDOWN, table-driven commands.
 */

#include <Uefi.h>

#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/PrintLib.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/BlockIo.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/SimplePointer.h>
#include <Protocol/AbsolutePointer.h>
#include <Protocol/CpuIo2.h>
#include <Guid/FileInfo.h>
#include <Guid/FileSystemInfo.h>

//
// The Microsoft compiler sometimes turns simple loops into calls to
// memset/memcpy. UEFI has no C library, so provide them (VS builds only).
//
#if defined (_MSC_VER)
VOID *
__cdecl
memset (
  VOID   *Dest,
  int    Ch,
  UINTN  Count
  )
{
  volatile UINT8  *P = (volatile UINT8 *)Dest;      // volatile: stops the compiler turning this loop back into memset

  while (Count-- > 0) {
    *P++ = (UINT8)Ch;
  }
  return Dest;
}

VOID *
__cdecl
memcpy (
  VOID        *Dest,
  CONST VOID  *Src,
  UINTN       Count
  )
{
  volatile UINT8  *D = (volatile UINT8 *)Dest;
  CONST UINT8     *S = (CONST UINT8 *)Src;

  while (Count-- > 0) {
    *D++ = *S++;
  }
  return Dest;
}
#endif

#ifndef UNREFERENCED_PARAMETER
#define UNREFERENCED_PARAMETER(P)  ((VOID)(P))
#endif

#define AIOS_VERSION  L"0.7.0"
#define INPUT_SIZE    128
#define MAX_PATH      260
#define MAX_COMP      64
#define HISTORY_MAX   16
#define MAX_VOLS      24          // drive letters C: .. Z:

//
// ---------------------------------------------------------------
// Global state
// ---------------------------------------------------------------
//
typedef struct {
  EFI_HANDLE         Handle;
  EFI_FILE_PROTOCOL  *Root;
  CHAR16             Cwd[MAX_PATH];
} VOLUME;

STATIC VOLUME             gVols[MAX_VOLS];
STATIC UINTN              gVolCount = 0;
STATIC UINTN              gCur = 0;                 // index of current drive

STATIC EFI_FILE_PROTOCOL  *gRoot = NULL;            // root of current drive
STATIC CHAR16             gCwd[MAX_PATH] = L"\\";   // cwd of current drive
STATIC BOOLEAN            gExit = FALSE;
STATIC UINTN              gAttr = (UINTN)(EFI_LIGHTGRAY | (EFI_BLACK << 4));
STATIC CHAR16             gHistory[HISTORY_MAX][INPUT_SIZE];
STATIC UINTN              gHistCount = 0;

//
// Hooks used by the desktop (GUI). In plain text mode they stay NULL.
//
STATIC VOID               (*gIdleHook)(VOID) = NULL;
STATIC BOOLEAN            (*gKeyFilter)(EFI_INPUT_KEY *Key) = NULL;
STATIC BOOLEAN            gAbortRead = FALSE;

typedef VOID (*CMD_FN)(CHAR16 *Args);

typedef struct {
  CONST CHAR16  *Name;
  CMD_FN        Fn;
  CONST CHAR16  *Usage;   // NULL = hidden alias
  CONST CHAR16  *Help;
} COMMAND;

STATIC VOID CmdHelp(CHAR16 *Args);
STATIC VOID RunCommand(CHAR16 *Line);
STATIC VOID ReadCommandLine(CHAR16 *Buf, UINTN Size);
STATIC VOID AddHistory(CONST CHAR16 *Line);
STATIC VOID PrintPrompt(VOID);
STATIC VOID PrintBanner(VOID);

//
// ---------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------
//
STATIC
VOID
SetColor(UINTN Attr)
{
  gST->ConOut->SetAttribute(gST->ConOut, Attr);
}

STATIC
UINTN
FgAttr(UINTN Foreground)
{
  return Foreground | (gAttr & 0x70);
}

//
// Waits for a key. In text mode this just blocks. While the desktop is
// open it keeps the desktop alive (mouse, redraws) and lets the desktop
// claim keys that are not meant for the shell.
//
STATIC
EFI_STATUS
NextKey(EFI_INPUT_KEY *Key)
{
  UINTN  Index;

  if (gIdleHook == NULL) {
    for (;;) {
      gBS->WaitForEvent(1, &gST->ConIn->WaitForKey, &Index);
      if (!EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, Key))) {
        return EFI_SUCCESS;
      }
    }
  }

  for (;;) {
    gIdleHook();
    if (gAbortRead) {
      return EFI_ABORTED;
    }

    if (gBS->CheckEvent(gST->ConIn->WaitForKey) == EFI_SUCCESS) {
      if (EFI_ERROR(gST->ConIn->ReadKeyStroke(gST->ConIn, Key))) {
        continue;
      }
      if (gKeyFilter != NULL && gKeyFilter(Key)) {
        if (gAbortRead) {
          return EFI_ABORTED;
        }
        continue;
      }
      return EFI_SUCCESS;
    }

    gBS->Stall(1000);
  }
}

STATIC
CHAR16
Up(CHAR16 C)
{
  if (C >= L'a' && C <= L'z') {
    return (CHAR16)(C - L'a' + L'A');
  }
  return C;
}

STATIC
CHAR16
Lo(CHAR16 C)
{
  if (C >= L'A' && C <= L'Z') {
    return (CHAR16)(C - L'A' + L'a');
  }
  return C;
}

STATIC
BOOLEAN
SameText(CONST CHAR16 *A, CONST CHAR16 *B)
{
  while (*A != L'\0' && *B != L'\0') {
    if (Up(*A) != Up(*B)) {
      return FALSE;
    }
    A++;
    B++;
  }
  return (BOOLEAN)(*A == *B);
}

STATIC
BOOLEAN
PrefixMatch(CONST CHAR16 *Str, CONST CHAR16 *Prefix)
{
  while (*Prefix != L'\0') {
    if (*Str == L'\0' || Up(*Str) != Up(*Prefix)) {
      return FALSE;
    }
    Str++;
    Prefix++;
  }
  return TRUE;
}

STATIC
BOOLEAN
HasSpace(CONST CHAR16 *S)
{
  while (*S != L'\0') {
    if (*S == L' ') {
      return TRUE;
    }
    S++;
  }
  return FALSE;
}

STATIC
BOOLEAN
NextToken(CHAR16 **Cursor, CHAR16 *Out, UINTN OutMax)
{
  CHAR16  *P = *Cursor;
  UINTN   N = 0;

  while (*P == L' ' || *P == L'\t') {
    P++;
  }

  if (*P == L'\0') {
    *Cursor = P;
    Out[0] = L'\0';
    return FALSE;
  }

  while (*P != L'\0' && *P != L' ' && *P != L'\t') {
    if (N < OutMax - 1) {
      Out[N++] = *P;
    }
    P++;
  }

  Out[N] = L'\0';
  *Cursor = P;
  return TRUE;
}

STATIC
UINTN
HexDigit(CHAR16 C)
{
  if (C >= L'0' && C <= L'9') return (UINTN)(C - L'0');
  if (C >= L'a' && C <= L'f') return (UINTN)(C - L'a' + 10);
  if (C >= L'A' && C <= L'F') return (UINTN)(C - L'A' + 10);
  return 0xFF;
}

STATIC
UINTN
Min3(UINTN A, UINTN B, UINTN C)
{
  UINTN  M = A;
  if (B < M) M = B;
  if (C < M) M = C;
  return M;
}

//
// Edit distance (Damerau-Levenshtein, case-insensitive). Used for
// the "Did you mean?" suggestions. Returns 99 for very long strings.
//
STATIC
UINTN
EditDistance(CONST CHAR16 *A, CONST CHAR16 *B)
{
  UINT8  D[41][41];
  UINTN  La = StrLen(A);
  UINTN  Lb = StrLen(B);
  UINTN  I;
  UINTN  J;
  UINTN  Cost;
  UINTN  V;

  if (La > 40 || Lb > 40) {
    return 99;
  }

  for (I = 0; I <= La; I++) {
    D[I][0] = (UINT8)I;
  }
  for (J = 0; J <= Lb; J++) {
    D[0][J] = (UINT8)J;
  }

  for (I = 1; I <= La; I++) {
    for (J = 1; J <= Lb; J++) {
      Cost = (Up(A[I - 1]) == Up(B[J - 1])) ? 0 : 1;
      V = Min3((UINTN)D[I - 1][J] + 1, (UINTN)D[I][J - 1] + 1, (UINTN)D[I - 1][J - 1] + Cost);

      if (I > 1 && J > 1 &&
          Up(A[I - 1]) == Up(B[J - 2]) &&
          Up(A[I - 2]) == Up(B[J - 1])) {
        if ((UINTN)D[I - 2][J - 2] + 1 < V) {
          V = (UINTN)D[I - 2][J - 2] + 1;
        }
      }
      D[I][J] = (UINT8)V;
    }
  }

  return D[La][Lb];
}

//
// ---------------------------------------------------------------
// Drives and paths
// ---------------------------------------------------------------
//
STATIC
VOID
AddVolume(EFI_HANDLE Handle)
{
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs = NULL;
  EFI_FILE_PROTOCOL                *Root = NULL;

  if (gVolCount >= MAX_VOLS) {
    return;
  }

  if (EFI_ERROR(gBS->HandleProtocol(Handle, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&Fs))) {
    return;
  }

  if (EFI_ERROR(Fs->OpenVolume(Fs, &Root))) {
    return;
  }

  gVols[gVolCount].Handle = Handle;
  gVols[gVolCount].Root = Root;
  StrCpyS(gVols[gVolCount].Cwd, MAX_PATH, L"\\");
  gVolCount++;
}

//
// Finds every file system UEFI knows about. The drive we booted from
// becomes C:, the rest follow as D:, E:, ...
//
STATIC
VOID
InitVolumes(EFI_HANDLE ImageHandle)
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image = NULL;
  EFI_HANDLE                 *Handles = NULL;
  EFI_HANDLE                 Boot = NULL;
  UINTN                      Count = 0;
  UINTN                      I;
  UINTN                      Pass;

  if (!EFI_ERROR(gBS->HandleProtocol(ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image))) {
    Boot = Image->DeviceHandle;
  }

  if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, &Count, &Handles))) {
    return;
  }

  for (Pass = 0; Pass < 2; Pass++) {
    for (I = 0; I < Count; I++) {
      BOOLEAN  IsBoot = (BOOLEAN)(Handles[I] == Boot);
      if ((Pass == 0) == IsBoot) {
        AddVolume(Handles[I]);
      }
    }
  }

  gBS->FreePool(Handles);

  if (gVolCount > 0) {
    gCur = 0;
    gRoot = gVols[0].Root;
    StrCpyS(gCwd, MAX_PATH, gVols[0].Cwd);
  }
}

STATIC
CHAR16
DriveLetter(UINTN Index)
{
  return (CHAR16)(L'C' + Index);
}

STATIC
BOOLEAN
SwitchVolume(UINTN Index)
{
  if (Index >= gVolCount) {
    return FALSE;
  }

  StrCpyS(gVols[gCur].Cwd, MAX_PATH, gCwd);
  gCur = Index;
  gRoot = gVols[Index].Root;
  StrCpyS(gCwd, MAX_PATH, gVols[Index].Cwd);
  return TRUE;
}

STATIC
CHAR16 *
CwdOf(UINTN Vol)
{
  return (Vol == gCur) ? gCwd : gVols[Vol].Cwd;
}

//
// Splits an optional drive prefix ("D:") off a path.
// Returns FALSE if the prefix names a drive that does not exist.
//
STATIC
BOOLEAN
ParseDrive(CONST CHAR16 *In, UINTN *Vol, CONST CHAR16 **Rest)
{
  CHAR16  L;

  *Vol = gCur;
  *Rest = In;

  if (In[0] != L'\0' && In[1] == L':') {
    L = Up(In[0]);
    if (L >= L'A' && L <= L'Z') {
      if (L < L'C' || (UINTN)(L - L'C') >= gVolCount) {
        return FALSE;
      }
      *Vol = (UINTN)(L - L'C');
      *Rest = In + 2;
    }
  }

  return TRUE;
}

//
// Turns user input (relative or absolute, optional drive, with . and ..)
// into a clean absolute path such as \EFI\BOOT on a given drive.
//
STATIC
BOOLEAN
ResolveFull(CONST CHAR16 *In, UINTN *VolOut, CHAR16 *Out)
{
  CHAR16        Full[MAX_PATH * 2];
  CHAR16        *Comp[MAX_COMP];
  CHAR16        *P;
  CHAR16        *Start;
  CONST CHAR16  *Rest;
  UINTN         Vol;
  UINTN         Count = 0;
  UINTN         I;

  if (!ParseDrive(In, &Vol, &Rest)) {
    return FALSE;
  }

  Full[0] = L'\0';

  if (Rest[0] != L'\\' && Rest[0] != L'/') {
    StrCpyS(Full, MAX_PATH * 2, CwdOf(Vol));
    StrCatS(Full, MAX_PATH * 2, L"\\");
  }
  StrCatS(Full, MAX_PATH * 2, Rest);

  for (P = Full; *P != L'\0'; P++) {
    if (*P == L'/') {
      *P = L'\\';
    }
  }

  P = Full;
  while (*P != L'\0') {
    Start = P;
    while (*P != L'\0' && *P != L'\\') {
      P++;
    }
    if (*P != L'\0') {
      *P = L'\0';
      P++;
    }

    if (Start[0] == L'\0' || StrCmp(Start, L".") == 0) {
      continue;
    }
    if (StrCmp(Start, L"..") == 0) {
      if (Count > 0) {
        Count--;
      }
      continue;
    }
    if (Count < MAX_COMP) {
      Comp[Count++] = Start;
    }
  }

  Out[0] = L'\\';
  Out[1] = L'\0';

  for (I = 0; I < Count; I++) {
    if (StrLen(Out) + 1 + StrLen(Comp[I]) >= MAX_PATH) {
      break;
    }
    if (I > 0) {
      StrCatS(Out, MAX_PATH, L"\\");
    }
    StrCatS(Out, MAX_PATH, Comp[I]);
  }

  *VolOut = Vol;
  return TRUE;
}

STATIC
EFI_STATUS
OpenPath(CONST CHAR16 *Arg, UINT64 Mode, UINT64 Attr, EFI_FILE_PROTOCOL **File)
{
  CHAR16  Full[MAX_PATH];
  UINTN   Vol;

  if (gVolCount == 0) {
    return EFI_NOT_READY;
  }

  if (!ResolveFull(Arg, &Vol, Full)) {
    return EFI_NOT_FOUND;
  }

  return gVols[Vol].Root->Open(gVols[Vol].Root, File, Full, Mode, Attr);
}

STATIC
EFI_FILE_INFO *
GetFileInfo(EFI_FILE_PROTOCOL *File)
{
  UINTN          Size = 0;
  EFI_FILE_INFO  *Info = NULL;
  EFI_STATUS     Status;

  Status = File->GetInfo(File, &gEfiFileInfoGuid, &Size, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return NULL;
  }

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Size, (VOID **)&Info))) {
    return NULL;
  }

  if (EFI_ERROR(File->GetInfo(File, &gEfiFileInfoGuid, &Size, Info))) {
    gBS->FreePool(Info);
    return NULL;
  }

  return Info;
}

STATIC
BOOLEAN
IsDir(EFI_FILE_PROTOCOL *File)
{
  EFI_FILE_INFO  *Info;
  BOOLEAN        Result;

  Info = GetFileInfo(File);
  if (Info == NULL) {
    return FALSE;
  }

  Result = (BOOLEAN)((Info->Attribute & EFI_FILE_DIRECTORY) != 0);
  gBS->FreePool(Info);
  return Result;
}

STATIC
BOOLEAN
NeedVolume(VOID)
{
  if (gVolCount == 0) {
    Print(L"AIOS: No file system available.\r\n");
    return FALSE;
  }
  return TRUE;
}

STATIC
VOID
PrintSize(UINT64 Bytes)
{
  UINT64  Tenths;

  if (Bytes >= 1073741824ULL) {
    Tenths = ((Bytes / 1048576ULL) * 10) / 1024;
    Print(L"%u.%u GB", (UINT32)(Tenths / 10), (UINT32)(Tenths % 10));
  } else if (Bytes >= 1048576ULL) {
    Tenths = (Bytes * 10) / 1048576ULL;
    Print(L"%u.%u MB", (UINT32)(Tenths / 10), (UINT32)(Tenths % 10));
  } else {
    Print(L"%u KB", (UINT32)(Bytes / 1024));
  }
}

//
// ---------------------------------------------------------------
// "Did you mean?" for files
// ---------------------------------------------------------------
//
STATIC
BOOLEAN
SuggestFile(CONST CHAR16 *Arg, CHAR16 *Out, UINTN OutMax)
{
  CHAR16             DirPart[MAX_PATH];
  CHAR16             DirArg[MAX_PATH];
  CONST CHAR16       *Name;
  UINTN              Cut = StrLen(Arg);
  UINTN              NameLen;
  UINTN              Best = 99;
  UINTN              Limit;
  UINTN              D;
  CHAR16             BestName[MAX_PATH];
  EFI_FILE_PROTOCOL  *Dir;
  UINT64             Buf[128];
  EFI_FILE_INFO      *I = (EFI_FILE_INFO *)Buf;
  UINTN              Size;

  while (Cut > 0 && Arg[Cut - 1] != L'\\' && Arg[Cut - 1] != L'/' && Arg[Cut - 1] != L':') {
    Cut--;
  }

  Name = Arg + Cut;
  NameLen = StrLen(Name);
  if (NameLen == 0 || Cut >= MAX_PATH - 1) {
    return FALSE;
  }

  CopyMem(DirPart, Arg, Cut * sizeof(CHAR16));
  DirPart[Cut] = L'\0';

  if (Cut == 0) {
    StrCpyS(DirArg, MAX_PATH, L".");
  } else {
    StrCpyS(DirArg, MAX_PATH, DirPart);
  }

  if (EFI_ERROR(OpenPath(DirArg, EFI_FILE_MODE_READ, 0, &Dir))) {
    return FALSE;
  }

  Limit = (NameLen <= 2) ? 1 : 2;
  BestName[0] = L'\0';

  for (;;) {
    Size = sizeof(Buf);
    if (EFI_ERROR(Dir->Read(Dir, &Size, Buf)) || Size == 0) {
      break;
    }
    if (StrCmp(I->FileName, L".") == 0 || StrCmp(I->FileName, L"..") == 0) {
      continue;
    }
    D = EditDistance(Name, I->FileName);
    if (D < Best && D <= Limit) {
      Best = D;
      StrCpyS(BestName, MAX_PATH, I->FileName);
    }
  }

  Dir->Close(Dir);

  if (BestName[0] == L'\0') {
    return FALSE;
  }

  StrCpyS(Out, OutMax, DirPart);
  StrCatS(Out, OutMax, BestName);
  return TRUE;
}

STATIC
VOID
PrintSuggestion(CONST CHAR16 *Arg)
{
  CHAR16  Sug[MAX_PATH];

  if (SuggestFile(Arg, Sug, MAX_PATH)) {
    SetColor(FgAttr(EFI_YELLOW));
    Print(L"  Did you mean: %s ?\r\n", Sug);
    SetColor(gAttr);
  }
}

STATIC
VOID
NotFound(CONST CHAR16 *Arg)
{
  Print(L"File not found: %s\r\n", Arg);
  PrintSuggestion(Arg);
}

//
// ---------------------------------------------------------------
// Commands
// ---------------------------------------------------------------
//
STATIC
VOID
CmdCls(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  gST->ConOut->ClearScreen(gST->ConOut);
}

STATIC
VOID
CmdVer(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"AIOS version %s\r\n", AIOS_VERSION);
}

STATIC
VOID
CmdAbout(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"\r\nAIOS - A personal operating system project\r\n");
  Print(L"Modern, fast, and DOS-friendly.\r\n");
  Print(L"Currently running as a UEFI application, built with EDK II.\r\n\r\n");
}

STATIC
VOID
CmdEcho(CHAR16 *Args)
{
  Print(L"%s\r\n", Args);
}

STATIC
VOID
CmdTime(CHAR16 *Args)
{
  EFI_TIME  Time;

  UNREFERENCED_PARAMETER(Args);

  if (EFI_ERROR(gRT->GetTime(&Time, NULL))) {
    Print(L"AIOS: Could not read system time.\r\n");
    return;
  }

  Print(
    L"%02u:%02u:%02u\r\n",
    (UINT32)Time.Hour,
    (UINT32)Time.Minute,
    (UINT32)Time.Second
  );
}

STATIC
VOID
CmdDate(CHAR16 *Args)
{
  EFI_TIME  Time;

  UNREFERENCED_PARAMETER(Args);

  if (EFI_ERROR(gRT->GetTime(&Time, NULL))) {
    Print(L"AIOS: Could not read system date.\r\n");
    return;
  }

  Print(
    L"%04u-%02u-%02u\r\n",
    (UINT32)Time.Year,
    (UINT32)Time.Month,
    (UINT32)Time.Day
  );
}

STATIC
VOID
CmdEfi(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);

  Print(L"\r\nUEFI firmware information\r\n");
  Print(L"--------------------------\r\n");
  Print(L"Vendor:   %s\r\n", gST->FirmwareVendor);
  Print(L"Revision: %u.%u\r\n",
        (UINT32)(gST->FirmwareRevision >> 16),
        (UINT32)(gST->FirmwareRevision & 0xFFFF));
  Print(L"UEFI spec: %u.%u\r\n\r\n",
        (UINT32)(gST->Hdr.Revision >> 16),
        (UINT32)((gST->Hdr.Revision & 0xFFFF) / 10));
}

STATIC
VOID
CmdExit(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"Leaving AIOS...\r\n");
  gExit = TRUE;
}

STATIC
VOID
CmdReboot(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"Rebooting...\r\n");
  gRT->ResetSystem(EfiResetCold, EFI_SUCCESS, 0, NULL);
}

STATIC
VOID
CmdShutdown(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"Shutting down...\r\n");
  gRT->ResetSystem(EfiResetShutdown, EFI_SUCCESS, 0, NULL);
}

STATIC
VOID
CmdHistory(CHAR16 *Args)
{
  UINTN  I;

  UNREFERENCED_PARAMETER(Args);

  for (I = 0; I < gHistCount; I++) {
    Print(L"%3u  %s\r\n", (UINT32)(I + 1), gHistory[I]);
  }
}

//
// COLOR <bg><fg>  (DOS style hex digits, e.g. COLOR 0A = green on black)
//
STATIC
VOID
CmdColor(CHAR16 *Args)
{
  CHAR16  Arg[16];
  CHAR16  *C = Args;
  UINTN   Bg;
  UINTN   Fg;

  if (!NextToken(&C, Arg, 16)) {
    gAttr = (UINTN)(EFI_LIGHTGRAY | (EFI_BLACK << 4));
    SetColor(gAttr);
    gST->ConOut->ClearScreen(gST->ConOut);
    return;
  }

  if (StrLen(Arg) != 2) {
    Print(L"Usage: COLOR <bg><fg>   e.g. COLOR 0A (green on black)\r\n");
    Print(L"Background 0-7, foreground 0-F:\r\n");
    Print(L"0 Black  1 Blue   2 Green   3 Cyan   4 Red   5 Magenta  6 Brown  7 Gray\r\n");
    Print(L"8 DarkGray  9 LtBlue  A LtGreen  B LtCyan  C LtRed  D LtMagenta  E Yellow  F White\r\n");
    return;
  }

  Bg = HexDigit(Arg[0]);
  Fg = HexDigit(Arg[1]);

  if (Bg > 7 || Fg > 0xF) {
    Print(L"AIOS: Invalid color. Background must be 0-7, foreground 0-F.\r\n");
    return;
  }

  gAttr = (Fg | (Bg << 4));
  SetColor(gAttr);
  gST->ConOut->ClearScreen(gST->ConOut);
}

STATIC
VOID
CmdMem(CHAR16 *Args)
{
  UINTN                  MapSize = 0;
  UINTN                  MapKey;
  UINTN                  DescSize;
  UINT32                 DescVer;
  EFI_MEMORY_DESCRIPTOR  *Map = NULL;
  EFI_MEMORY_DESCRIPTOR  *D;
  EFI_STATUS             Status;
  UINT64                 Pages = 0;
  UINT64                 FreePages = 0;

  UNREFERENCED_PARAMETER(Args);

  Status = gBS->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer);
  if (Status != EFI_BUFFER_TOO_SMALL) {
    Print(L"AIOS: Could not query memory map.\r\n");
    return;
  }

  MapSize += 2 * DescSize;

  Status = gBS->AllocatePool(EfiBootServicesData, MapSize, (VOID **)&Map);
  if (EFI_ERROR(Status)) {
    Print(L"AIOS: Out of memory.\r\n");
    return;
  }

  Status = gBS->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
  if (EFI_ERROR(Status)) {
    gBS->FreePool(Map);
    Print(L"AIOS: Could not read memory map.\r\n");
    return;
  }

  for (D = Map;
       (UINT8 *)D < (UINT8 *)Map + MapSize;
       D = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)D + DescSize)) {
    Pages += D->NumberOfPages;
    if (D->Type == EfiConventionalMemory) {
      FreePages += D->NumberOfPages;
    }
  }

  gBS->FreePool(Map);

  Print(L"\r\nMemory\r\n");
  Print(L"------\r\n");
  Print(L"Free:         %lu MB\r\n", DivU64x32(FreePages, 256));
  Print(L"Total mapped: %lu MB\r\n\r\n", DivU64x32(Pages, 256));
}

STATIC
VOID
CmdDir(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             Full[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *Dir;
  EFI_STATUS         Status;
  UINT64             Buf[128];
  EFI_FILE_INFO      *I = (EFI_FILE_INFO *)Buf;
  UINTN              Size;
  UINTN              Vol;
  UINTN              Files = 0;
  UINTN              Dirs = 0;
  UINT64             Bytes = 0;

  if (!NeedVolume()) {
    return;
  }

  if (!NextToken(&C, Arg, MAX_PATH)) {
    StrCpyS(Arg, MAX_PATH, L".");
  }

  Status = OpenPath(Arg, EFI_FILE_MODE_READ, 0, &Dir);
  if (EFI_ERROR(Status)) {
    NotFound(Arg);
    return;
  }

  if (!IsDir(Dir)) {
    Print(L"Not a directory: %s\r\n", Arg);
    Dir->Close(Dir);
    return;
  }

  ResolveFull(Arg, &Vol, Full);
  Print(L"\r\n Directory of %c:%s\r\n\r\n", DriveLetter(Vol), Full);

  for (;;) {
    Size = sizeof(Buf);
    Status = Dir->Read(Dir, &Size, Buf);
    if (EFI_ERROR(Status) || Size == 0) {
      break;
    }

    Print(
      L"%04u-%02u-%02u  %02u:%02u   ",
      (UINT32)I->ModificationTime.Year,
      (UINT32)I->ModificationTime.Month,
      (UINT32)I->ModificationTime.Day,
      (UINT32)I->ModificationTime.Hour,
      (UINT32)I->ModificationTime.Minute
    );

    if ((I->Attribute & EFI_FILE_DIRECTORY) != 0) {
      Print(L"%12s  ", L"<DIR>");
      SetColor(FgAttr(EFI_LIGHTCYAN));
      Print(L"%s\r\n", I->FileName);
      SetColor(gAttr);
      Dirs++;
    } else {
      Print(L"%12lu  %s\r\n", I->FileSize, I->FileName);
      Files++;
      Bytes += I->FileSize;
    }
  }

  Dir->Close(Dir);

  Print(L"\r\n  %u file(s)   %lu bytes\r\n", (UINT32)Files, Bytes);
  Print(L"  %u dir(s)\r\n", (UINT32)Dirs);
}

STATIC
VOID
CmdCd(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             Full[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *F;
  EFI_STATUS         Status;
  UINTN              Vol;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"%c:%s\r\n", DriveLetter(gCur), gCwd);
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  if (!ResolveFull(Arg, &Vol, Full)) {
    Print(L"The system cannot find the drive specified.\r\n");
    return;
  }

  Status = gVols[Vol].Root->Open(gVols[Vol].Root, &F, Full, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(Status)) {
    Print(L"The system cannot find the path specified.\r\n");
    PrintSuggestion(Arg);
    return;
  }

  if (!IsDir(F)) {
    Print(L"The directory name is invalid.\r\n");
    F->Close(F);
    return;
  }

  F->Close(F);
  StrCpyS(CwdOf(Vol), MAX_PATH, Full);
}

STATIC
VOID
CmdType(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  CHAR16             Out[600];
  CHAR8              In[256];
  EFI_FILE_PROTOCOL  *F;
  EFI_STATUS         Status;
  UINTN              Size;
  UINTN              I;
  UINTN              O;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"Usage: TYPE <file>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(Arg, EFI_FILE_MODE_READ, 0, &F);
  if (EFI_ERROR(Status)) {
    NotFound(Arg);
    return;
  }

  if (IsDir(F)) {
    Print(L"Access denied: %s is a directory.\r\n", Arg);
    F->Close(F);
    return;
  }

  for (;;) {
    Size = sizeof(In);
    Status = F->Read(F, &Size, In);
    if (EFI_ERROR(Status) || Size == 0) {
      break;
    }

    O = 0;
    for (I = 0; I < Size; I++) {
      if (In[I] == '\n') {
        Out[O++] = L'\r';
        Out[O++] = L'\n';
      } else if (In[I] == '\t' || (In[I] >= ' ' && In[I] <= '~')) {
        Out[O++] = (CHAR16)In[I];
      }
    }
    Out[O] = L'\0';
    gST->ConOut->OutputString(gST->ConOut, Out);
  }

  F->Close(F);
  Print(L"\r\n");
}

STATIC
VOID
CmdMkdir(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *F;
  EFI_STATUS         Status;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"Usage: MKDIR <name>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(
             Arg,
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             EFI_FILE_DIRECTORY,
             &F
           );
  if (EFI_ERROR(Status)) {
    Print(L"Unable to create directory: %r\r\n", Status);
    return;
  }

  F->Close(F);
}

STATIC
VOID
CmdDel(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *F;
  EFI_STATUS         Status;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"Usage: DEL <file or empty directory>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(Arg, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &F);
  if (EFI_ERROR(Status)) {
    NotFound(Arg);
    return;
  }

  Status = F->Delete(F);
  if (Status == EFI_WARN_DELETE_FAILURE || EFI_ERROR(Status)) {
    Print(L"Could not delete %s (not empty or protected).\r\n", Arg);
  }
}

STATIC
VOID
CmdCopy(CHAR16 *Args)
{
  CHAR16             Src[MAX_PATH];
  CHAR16             Dst[MAX_PATH];
  CHAR16             FullSrc[MAX_PATH];
  CHAR16             FullDst[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *In;
  EFI_FILE_PROTOCOL  *OutFile;
  EFI_STATUS         Status;
  VOID               *Buf = NULL;
  UINTN              Size;
  UINTN              SrcVol;
  UINTN              DstVol;
  UINT64             Total = 0;

  if (!NextToken(&C, Src, MAX_PATH) || !NextToken(&C, Dst, MAX_PATH)) {
    Print(L"Usage: COPY <source> <destination>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  if (!ResolveFull(Src, &SrcVol, FullSrc) || !ResolveFull(Dst, &DstVol, FullDst)) {
    Print(L"The system cannot find the drive specified.\r\n");
    return;
  }

  if (SrcVol == DstVol && SameText(FullSrc, FullDst)) {
    Print(L"The file cannot be copied onto itself.\r\n");
    return;
  }

  Status = OpenPath(Src, EFI_FILE_MODE_READ, 0, &In);
  if (EFI_ERROR(Status)) {
    NotFound(Src);
    return;
  }

  if (IsDir(In)) {
    Print(L"Copying directories is not supported yet.\r\n");
    In->Close(In);
    return;
  }

  // Overwrite: remove any existing destination first.
  if (!EFI_ERROR(OpenPath(Dst, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &OutFile))) {
    OutFile->Delete(OutFile);
  }

  Status = OpenPath(
             Dst,
             EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
             0,
             &OutFile
           );
  if (EFI_ERROR(Status)) {
    Print(L"Unable to create %s: %r\r\n", Dst, Status);
    In->Close(In);
    return;
  }

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, 4096, &Buf))) {
    Print(L"AIOS: Out of memory.\r\n");
    In->Close(In);
    OutFile->Close(OutFile);
    return;
  }

  for (;;) {
    Size = 4096;
    Status = In->Read(In, &Size, Buf);
    if (EFI_ERROR(Status) || Size == 0) {
      break;
    }

    Status = OutFile->Write(OutFile, &Size, Buf);
    if (EFI_ERROR(Status)) {
      Print(L"Write error: %r\r\n", Status);
      break;
    }
    Total += Size;
  }

  gBS->FreePool(Buf);
  In->Close(In);
  OutFile->Close(OutFile);

  if (!EFI_ERROR(Status)) {
    Print(L"        1 file(s) copied (%lu bytes).\r\n", Total);
  }
}

STATIC
VOID
CmdPwd(CHAR16 *Args)
{
  UNREFERENCED_PARAMETER(Args);
  Print(L"%c:%s\r\n", DriveLetter(gCur), gCwd);
}

STATIC
VOID
CmdTouch(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *File;
  EFI_STATUS         Status;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"Usage: TOUCH <file>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(
    Arg,
    EFI_FILE_MODE_READ |
    EFI_FILE_MODE_WRITE |
    EFI_FILE_MODE_CREATE,
    0,
    &File
  );

  if (EFI_ERROR(Status)) {
    Print(L"Could not create/open %s: %r\r\n", Arg, Status);
    return;
  }

  if (IsDir(File)) {
    Print(L"%s is a directory, not a file.\r\n", Arg);
    File->Close(File);
    return;
  }

  File->Close(File);
  Print(L"File ready: %s\r\n", Arg);
}

STATIC
VOID
CmdInfo(CHAR16 *Args)
{
  CHAR16             Arg[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  EFI_STATUS         Status;

  if (!NextToken(&C, Arg, MAX_PATH)) {
    Print(L"Usage: INFO <file-or-directory>\r\n");
    return;
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(Arg, EFI_FILE_MODE_READ, 0, &File);
  if (EFI_ERROR(Status)) {
    NotFound(Arg);
    return;
  }

  Info = GetFileInfo(File);
  File->Close(File);

  if (Info == NULL) {
    Print(L"Could not read file information.\r\n");
    return;
  }

  Print(L"\r\nFile information\r\n");
  Print(L"----------------\r\n");
  Print(L"Name:       %s\r\n", Info->FileName);
  Print(L"Size:       %lu bytes\r\n", Info->FileSize);
  Print(L"Created:    %04u-%02u-%02u\r\n",
        (UINT32)Info->CreateTime.Year,
        (UINT32)Info->CreateTime.Month,
        (UINT32)Info->CreateTime.Day);
  Print(L"Modified:   %04u-%02u-%02u\r\n",
        (UINT32)Info->ModificationTime.Year,
        (UINT32)Info->ModificationTime.Month,
        (UINT32)Info->ModificationTime.Day);
  Print(L"Type:       %s\r\n",
        (Info->Attribute & EFI_FILE_DIRECTORY)
        ? L"Directory" : L"File");
  Print(L"Read-only:  %s\r\n\r\n",
        (Info->Attribute & EFI_FILE_READ_ONLY)
        ? L"Yes" : L"No");

  gBS->FreePool(Info);
}

//
// REN <old> <new>   (new name only, no path)
//
STATIC
VOID
CmdRen(CHAR16 *Args)
{
  CHAR16             Old[MAX_PATH];
  CHAR16             New[MAX_PATH];
  CHAR16             *C = Args;
  EFI_FILE_PROTOCOL  *File;
  EFI_FILE_INFO      *Info;
  EFI_FILE_INFO      *NewInfo = NULL;
  EFI_STATUS         Status;
  UINTN              NewSize;
  UINTN              I;

  if (!NextToken(&C, Old, MAX_PATH) || !NextToken(&C, New, MAX_PATH)) {
    Print(L"Usage: REN <old name> <new name>\r\n");
    return;
  }

  for (I = 0; New[I] != L'\0'; I++) {
    if (New[I] == L'\\' || New[I] == L'/' || New[I] == L':') {
      Print(L"The new name must not contain a path. Use just a file name.\r\n");
      return;
    }
  }

  if (!NeedVolume()) {
    return;
  }

  Status = OpenPath(Old, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &File);
  if (EFI_ERROR(Status)) {
    NotFound(Old);
    return;
  }

  Info = GetFileInfo(File);
  if (Info == NULL) {
    File->Close(File);
    Print(L"Could not read file information.\r\n");
    return;
  }

  NewSize = SIZE_OF_EFI_FILE_INFO + (StrLen(New) + 1) * sizeof(CHAR16);

  if (EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, NewSize, (VOID **)&NewInfo))) {
    gBS->FreePool(Info);
    File->Close(File);
    Print(L"AIOS: Out of memory.\r\n");
    return;
  }

  CopyMem(NewInfo, Info, SIZE_OF_EFI_FILE_INFO);
  NewInfo->Size = NewSize;
  StrCpyS(NewInfo->FileName, StrLen(New) + 1, New);

  Status = File->SetInfo(File, &gEfiFileInfoGuid, NewSize, NewInfo);
  if (EFI_ERROR(Status)) {
    Print(L"Could not rename: %r\r\n", Status);
  }

  gBS->FreePool(NewInfo);
  gBS->FreePool(Info);
  File->Close(File);
}

//
// VOL - list drive letters
//
STATIC
VOID
CmdVol(CHAR16 *Args)
{
  UINTN                 I;
  UINTN                 Size;
  EFI_FILE_SYSTEM_INFO  *Info;

  UNREFERENCED_PARAMETER(Args);

  if (gVolCount == 0) {
    Print(L"No drives found.\r\n");
    return;
  }

  Print(L"\r\nDrives\r\n------\r\n");

  for (I = 0; I < gVolCount; I++) {
    Info = NULL;
    Size = 0;

    Print(L"  %c:%c ", DriveLetter(I), (I == gCur) ? L'*' : L' ');

    if (gVols[I].Root->GetInfo(gVols[I].Root, &gEfiFileSystemInfoGuid, &Size, NULL) == EFI_BUFFER_TOO_SMALL &&
        !EFI_ERROR(gBS->AllocatePool(EfiBootServicesData, Size, (VOID **)&Info))) {
      if (!EFI_ERROR(gVols[I].Root->GetInfo(gVols[I].Root, &gEfiFileSystemInfoGuid, &Size, Info))) {
        Print(L"%-12s ", Info->VolumeLabel[0] ? Info->VolumeLabel : L"(no label)");
        Print(L"size ");
        PrintSize(Info->VolumeSize);
        Print(L", free ");
        PrintSize(Info->FreeSpace);
        if (Info->ReadOnly) {
          Print(L"  [read-only]");
        }
      }
      gBS->FreePool(Info);
    }

    if (I == 0) {
      Print(L"  (boot drive)");
    }
    Print(L"\r\n");
  }

  Print(L"\r\n  * = current drive.  Type a drive letter, e.g. D:, to switch.\r\n");
}

//
// DISKS - list physical disks and partitions (HDD / SSD / USB / NVMe)
//
STATIC
VOID
CmdDisks(CHAR16 *Args)
{
  EFI_HANDLE             *Handles = NULL;
  UINTN                  Count = 0;
  UINTN                  I;
  UINTN                  Disk = 0;
  UINTN                  Shown = 0;
  EFI_BLOCK_IO_PROTOCOL  *Bio;

  UNREFERENCED_PARAMETER(Args);

  if (EFI_ERROR(gBS->LocateHandleBuffer(ByProtocol, &gEfiBlockIoProtocolGuid, NULL, &Count, &Handles))) {
    Print(L"No block devices found.\r\n");
    return;
  }

  Print(L"\r\nStorage devices\r\n---------------\r\n");

  for (I = 0; I < Count; I++) {
    if (EFI_ERROR(gBS->HandleProtocol(Handles[I], &gEfiBlockIoProtocolGuid, (VOID **)&Bio))) {
      continue;
    }
    if (Bio->Media == NULL || !Bio->Media->MediaPresent || Bio->Media->BlockSize == 0) {
      continue;
    }

    if (Bio->Media->LogicalPartition) {
      Print(L"    partition  ");
    } else {
      Print(L"  Disk %u      ", (UINT32)Disk);
      Disk++;
    }

    PrintSize((Bio->Media->LastBlock + 1) * (UINT64)Bio->Media->BlockSize);

    if (!Bio->Media->LogicalPartition) {
      Print(Bio->Media->RemovableMedia ? L"  removable" : L"  fixed");
    }
    if (Bio->Media->ReadOnly) {
      Print(L"  read-only");
    }
    Print(L"\r\n");
    Shown++;
  }

  gBS->FreePool(Handles);

  if (Shown == 0) {
    Print(L"  (nothing to show)\r\n");
  }
  Print(L"\r\n");
}

//
// ---------------------------------------------------------------
// Running programs (.EFI)
// ---------------------------------------------------------------
//
STATIC
EFI_STATUS
RunEfi(CONST CHAR16 *PathArg, CHAR16 *Args)
{
  CHAR16                      Full[MAX_PATH];
  UINTN                       Vol;
  EFI_DEVICE_PATH_PROTOCOL    *Dp;
  EFI_HANDLE                  Image = NULL;
  EFI_LOADED_IMAGE_PROTOCOL   *Li = NULL;
  EFI_STATUS                  Status;
  UINTN                       ExitSize = 0;
  CHAR16                      *ExitData = NULL;

  if (!ResolveFull(PathArg, &Vol, Full)) {
    return EFI_NOT_FOUND;
  }

  Dp = FileDevicePath(gVols[Vol].Handle, Full);
  if (Dp == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->LoadImage(FALSE, gImageHandle, Dp, NULL, 0, &Image);
  gBS->FreePool(Dp);
  if (EFI_ERROR(Status)) {
    return Status;
  }

  if (!EFI_ERROR(gBS->HandleProtocol(Image, &gEfiLoadedImageProtocolGuid, (VOID **)&Li))) {
    Li->LoadOptions = Args;
    Li->LoadOptionsSize = (UINT32)((StrLen(Args) + 1) * sizeof(CHAR16));
  }

  Status = gBS->StartImage(Image, &ExitSize, &ExitData);
  if (ExitData != NULL) {
    gBS->FreePool(ExitData);
  }

  // Take the screen back.
  gBS->SetWatchdogTimer(0, 0, 0, NULL);
  SetColor(gAttr);
  return Status;
}

STATIC
BOOLEAN
FileExists(CONST CHAR16 *Path)
{
  EFI_FILE_PROTOCOL  *F;
  BOOLEAN            Result;

  if (EFI_ERROR(OpenPath(Path, EFI_FILE_MODE_READ, 0, &F))) {
    return FALSE;
  }

  Result = (BOOLEAN)!IsDir(F);
  F->Close(F);
  return Result;
}

STATIC
BOOLEAN
HasExtension(CONST CHAR16 *Name, CONST CHAR16 *Ext)
{
  UINTN  Ln = StrLen(Name);
  UINTN  Le = StrLen(Ext);

  return (BOOLEAN)(Ln >= Le && SameText(Name + Ln - Le, Ext));
}

//
// Returns TRUE if Name was a program (it ran, or we explained why not).
//
STATIC
BOOLEAN
TryRunProgram(CONST CHAR16 *Name, CHAR16 *Args)
{
  CHAR16        Path[MAX_PATH];
  CHAR16        Alt[MAX_PATH];
  EFI_STATUS    Status;
  UINTN         I;
  CONST CHAR16  *Foreign[] = { L".EXE", L".COM", L".BAT" };

  if (gVolCount == 0) {
    return FALSE;
  }

  StrCpyS(Path, MAX_PATH, Name);
  if (!HasExtension(Name, L".EFI")) {
    StrCatS(Path, MAX_PATH, L".EFI");
  }

  if (FileExists(Path)) {
    Status = RunEfi(Path, Args);
    if (EFI_ERROR(Status)) {
      Print(L"\r\nProgram ended with status: %r\r\n", Status);
    }
    return TRUE;
  }

  for (I = 0; I < 3; I++) {
    StrCpyS(Alt, MAX_PATH, Name);
    if (!HasExtension(Name, Foreign[I])) {
      StrCatS(Alt, MAX_PATH, Foreign[I]);
    }
    if (FileExists(Alt)) {
      Print(L"%s is a DOS/Windows program. AIOS can only run .EFI programs so far.\r\n", Alt);
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
CmdRun(CHAR16 *Args)
{
  CHAR16      Name[MAX_PATH];
  CHAR16      Path[MAX_PATH];
  CHAR16      *C = Args;
  EFI_STATUS  Status;

  if (!NextToken(&C, Name, MAX_PATH)) {
    Print(L"Usage: RUN <program.efi> [arguments]\r\n");
    return;
  }

  while (*C == L' ' || *C == L'\t') {
    C++;
  }

  if (!NeedVolume()) {
    return;
  }

  StrCpyS(Path, MAX_PATH, Name);
  if (!HasExtension(Name, L".EFI") && !FileExists(Path)) {
    StrCatS(Path, MAX_PATH, L".EFI");
  }

  if (!FileExists(Path)) {
    NotFound(Name);
    return;
  }

  Status = RunEfi(Path, C);
  if (EFI_ERROR(Status)) {
    Print(L"\r\nProgram ended with status: %r\r\n", Status);
  }
}

#include "gui.inc"
#include "dos.inc"

//
// ---------------------------------------------------------------
// Command table  (add new commands here!)
// ---------------------------------------------------------------
//
STATIC CONST COMMAND  gCommands[] = {
  { L"HELP",     CmdHelp,     L"HELP",                 L"Show this command list"        },
  { L"CLEAR",    CmdCls,      L"CLEAR, CLS, CLR",      L"Clear the screen"              },
  { L"CLS",      CmdCls,      NULL,                    NULL                             },
  { L"CLR",      CmdCls,      NULL,                    NULL                             },
  { L"VER",      CmdVer,      L"VER, VERSION",         L"Show the AIOS version"         },
  { L"VERSION",  CmdVer,      NULL,                    NULL                             },
  { L"ABOUT",    CmdAbout,    L"ABOUT",                L"About AIOS"                    },
  { L"ECHO",     CmdEcho,     L"ECHO <text>",          L"Print a message"               },
  { L"TIME",     CmdTime,     L"TIME",                 L"Show the system time"          },
  { L"DATE",     CmdDate,     L"DATE",                 L"Show the system date"          },
  { L"DIR",      CmdDir,      L"DIR, LS [path]",       L"List files and folders"        },
  { L"LS",       CmdDir,      NULL,                    NULL                             },
  { L"CD",       CmdCd,       L"CD [path]",            L"Change or show the directory"  },
  { L"CHDIR",    CmdCd,       NULL,                    NULL                             },
  { L"PWD",      CmdPwd,      L"PWD",                  L"Show current directory"        },
  { L"TYPE",     CmdType,     L"TYPE, CAT <file>",     L"Show a text file"              },
  { L"CAT",      CmdType,     NULL,                    NULL                             },
  { L"MKDIR",    CmdMkdir,    L"MKDIR, MD <name>",     L"Create a directory"            },
  { L"MD",       CmdMkdir,    NULL,                    NULL                             },
  { L"TOUCH",    CmdTouch,    L"TOUCH <file>",         L"Create an empty file"          },
  { L"DEL",      CmdDel,      L"DEL, RM <name>",       L"Delete a file or empty folder" },
  { L"RM",       CmdDel,      NULL,                    NULL                             },
  { L"RMDIR",    CmdDel,      NULL,                    NULL                             },
  { L"RD",       CmdDel,      NULL,                    NULL                             },
  { L"COPY",     CmdCopy,     L"COPY, CP <src> <dst>", L"Copy a file (drives allowed)"  },
  { L"CP",       CmdCopy,     NULL,                    NULL                             },
  { L"REN",      CmdRen,      L"REN <old> <new>",      L"Rename a file or folder"       },
  { L"RENAME",   CmdRen,      NULL,                    NULL                             },
  { L"INFO",     CmdInfo,     L"INFO <path>",          L"Show file information"         },
  { L"VOL",      CmdVol,      L"VOL",                  L"List drives (C:, D:, ...)"     },
  { L"DISKS",    CmdDisks,    L"DISKS",                L"List physical disks (HDD/SSD)" },
  { L"RUN",      CmdRun,      L"RUN <prog> [args]",    L"Start an .EFI program"         },
  { L"DOS",      CmdDos,      L"DOS <image> [image2]", L"Run DOS from a disk image (F12 exits)" },
  { L"GUI",      CmdGui,      L"GUI [1|2] [nops2]",    L"Start the desktop (F10 exits)" },
  { L"COLOR",    CmdColor,    L"COLOR [bg fg]",        L"Set colors, e.g. COLOR 0A"     },
  { L"MEM",      CmdMem,      L"MEM",                  L"Show memory information"       },
  { L"HISTORY",  CmdHistory,  L"HISTORY",              L"Show command history"          },
  { L"EFI",      CmdEfi,      L"EFI",                  L"Show UEFI firmware info"       },
  { L"REBOOT",   CmdReboot,   L"REBOOT",               L"Restart the computer"          },
  { L"SHUTDOWN", CmdShutdown, L"SHUTDOWN",             L"Power off the computer"        },
  { L"EXIT",     CmdExit,     L"EXIT",                 L"Return to UEFI firmware"       }
};

#define COMMAND_COUNT  (sizeof(gCommands) / sizeof(gCommands[0]))

STATIC
VOID
CmdHelp(CHAR16 *Args)
{
  UINTN  I;

  UNREFERENCED_PARAMETER(Args);

  Print(L"\r\nAvailable AIOS commands:\r\n");
  for (I = 0; I < COMMAND_COUNT; I++) {
    if (gCommands[I].Usage != NULL) {
      Print(L"  %-22s %s\r\n", gCommands[I].Usage, gCommands[I].Help);
    }
  }
  Print(L"\r\nDrives: type C:, D:, ... to switch.  Program: type its name (e.g. HELLO).\r\n");
  Print(L"Desktop: type GUI (F1 = start menu, F10 = back to this shell).\r\n");
  Print(L"Keys: Tab = complete (again to cycle), Up/Down = history, Left/Right/Home/End = edit,\r\n");
  Print(L"      Esc = clear line.\r\n");
}

//
// ---------------------------------------------------------------
// "Did you mean?" for commands
// ---------------------------------------------------------------
//
STATIC
CONST COMMAND *
SuggestCommand(CONST CHAR16 *Typed)
{
  CONST COMMAND  *Best = NULL;
  UINTN          BestDist = 99;
  UINTN          Limit;
  UINTN          I;
  UINTN          D;
  UINTN          Len = StrLen(Typed);

  Limit = (Len <= 2) ? 1 : 2;

  for (I = 0; I < COMMAND_COUNT; I++) {
    D = EditDistance(Typed, gCommands[I].Name);

    // A typed prefix of a longer command counts as a close match.
    if (Len >= 2 && PrefixMatch(gCommands[I].Name, Typed) && D > 1) {
      D = 1;
    }

    if (D < BestDist && D <= Limit) {
      BestDist = D;
      Best = &gCommands[I];
    }
  }

  return Best;
}

STATIC
BOOLEAN
AskYesNo(VOID)
{
  EFI_INPUT_KEY  Key;

  for (;;) {
    if (EFI_ERROR(NextKey(&Key))) {
      return FALSE;
    }

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN || Key.UnicodeChar == L'y' || Key.UnicodeChar == L'Y') {
      Print(L"Y\r\n");
      return TRUE;
    }
    if (Key.UnicodeChar == L'n' || Key.UnicodeChar == L'N' || Key.ScanCode == SCAN_ESC) {
      Print(L"N\r\n");
      return FALSE;
    }
  }
}

//
// ---------------------------------------------------------------
// Tab completion
// ---------------------------------------------------------------
//
STATIC
BOOLEAN
CompleteCommand(CONST CHAR16 *Prefix, UINTN N, CHAR16 *Out, UINTN OutMax)
{
  UINTN    I;
  UINTN    Seen = 0;
  UINTN    K;
  BOOLEAN  Lower = (BOOLEAN)(Prefix[0] == L'\0' || (Prefix[0] >= L'a' && Prefix[0] <= L'z'));

  for (I = 0; I < COMMAND_COUNT; I++) {
    if (!PrefixMatch(gCommands[I].Name, Prefix)) {
      continue;
    }
    if (Seen == N) {
      StrCpyS(Out, OutMax, gCommands[I].Name);
      if (Lower) {
        for (K = 0; Out[K] != L'\0'; K++) {
          Out[K] = Lo(Out[K]);
        }
      }
      return TRUE;
    }
    Seen++;
  }

  return FALSE;
}

STATIC
BOOLEAN
CompleteFile(CONST CHAR16 *Prefix, UINTN N, CHAR16 *Out, UINTN OutMax)
{
  CHAR16             DirPart[MAX_PATH];
  CHAR16             DirArg[MAX_PATH];
  CONST CHAR16       *NamePart;
  UINTN              Cut = StrLen(Prefix);
  UINTN              Seen = 0;
  BOOLEAN            Found = FALSE;
  EFI_FILE_PROTOCOL  *Dir;
  UINT64             Buf[128];
  EFI_FILE_INFO      *I = (EFI_FILE_INFO *)Buf;
  UINTN              Size;

  if (gVolCount == 0) {
    return FALSE;
  }

  while (Cut > 0 && Prefix[Cut - 1] != L'\\' && Prefix[Cut - 1] != L'/' && Prefix[Cut - 1] != L':') {
    Cut--;
  }

  if (Cut >= MAX_PATH - 1) {
    return FALSE;
  }

  NamePart = Prefix + Cut;
  CopyMem(DirPart, Prefix, Cut * sizeof(CHAR16));
  DirPart[Cut] = L'\0';

  if (Cut == 0) {
    StrCpyS(DirArg, MAX_PATH, L".");
  } else {
    StrCpyS(DirArg, MAX_PATH, DirPart);
  }

  if (EFI_ERROR(OpenPath(DirArg, EFI_FILE_MODE_READ, 0, &Dir))) {
    return FALSE;
  }

  for (;;) {
    Size = sizeof(Buf);
    if (EFI_ERROR(Dir->Read(Dir, &Size, Buf)) || Size == 0) {
      break;
    }
    if (StrCmp(I->FileName, L".") == 0 || StrCmp(I->FileName, L"..") == 0 || HasSpace(I->FileName)) {
      continue;
    }
    if (!PrefixMatch(I->FileName, NamePart)) {
      continue;
    }
    if (Seen == N) {
      StrCpyS(Out, OutMax, DirPart);
      StrCatS(Out, OutMax, I->FileName);
      if ((I->Attribute & EFI_FILE_DIRECTORY) != 0) {
        StrCatS(Out, OutMax, L"\\");
      }
      Found = TRUE;
      break;
    }
    Seen++;
  }

  Dir->Close(Dir);
  return Found;
}

//
// ---------------------------------------------------------------
// Line editor with history and Tab completion
// ---------------------------------------------------------------
//
STATIC
VOID
AddHistory(CONST CHAR16 *Line)
{
  UINTN  I;

  if (Line[0] == L'\0') {
    return;
  }

  if (gHistCount > 0 && StrCmp(gHistory[gHistCount - 1], Line) == 0) {
    return;
  }

  if (gHistCount == HISTORY_MAX) {
    for (I = 1; I < HISTORY_MAX; I++) {
      StrCpyS(gHistory[I - 1], INPUT_SIZE, gHistory[I]);
    }
    gHistCount--;
  }

  StrCpyS(gHistory[gHistCount], INPUT_SIZE, Line);
  gHistCount++;
}

STATIC
VOID
Redraw(UINTN Col, UINTN Row, CHAR16 *Buf, UINTN Len, UINTN Cursor, UINTN PrevLen)
{
  UINTN  I;

  gST->ConOut->SetCursorPosition(gST->ConOut, Col, Row);
  gST->ConOut->OutputString(gST->ConOut, Buf);

  for (I = Len; I < PrevLen; I++) {
    gST->ConOut->OutputString(gST->ConOut, L" ");
  }

  gST->ConOut->SetCursorPosition(gST->ConOut, Col + Cursor, Row);
}

//
// Replaces the word that starts at Start with Str and moves the cursor to the end.
//
STATIC
VOID
ApplyCompletion(CHAR16 *Buf, UINTN Size, UINTN Start, CONST CHAR16 *Str, UINTN MaxLen, UINTN *Len, UINTN *Cur)
{
  Buf[Start] = L'\0';
  StrCatS(Buf, Size, Str);

  if (StrLen(Buf) > MaxLen) {
    Buf[MaxLen] = L'\0';
  }

  *Len = StrLen(Buf);
  *Cur = *Len;
}

STATIC
VOID
ReadCommandLine(CHAR16 *Buf, UINTN Size)
{
  UINTN          Len = 0;
  UINTN          Cur = 0;
  UINTN          PrevLen;
  UINTN          HistPos = gHistCount;
  UINTN          Cols = 80;
  UINTN          Rows = 25;
  UINTN          MaxLen;
  UINTN          StartCol;
  UINTN          StartRow;
  EFI_INPUT_KEY  Key;
  CHAR16         Draft[INPUT_SIZE];

  BOOLEAN        TabActive = FALSE;
  BOOLEAN        TabCmd = FALSE;
  UINTN          TabStart = 0;
  UINTN          TabIdx = 0;
  UINTN          K;
  CHAR16         TabOrig[INPUT_SIZE];
  CHAR16         Match[MAX_PATH];

  Buf[0] = L'\0';
  Draft[0] = L'\0';
  TabOrig[0] = L'\0';

  gST->ConOut->QueryMode(gST->ConOut, (UINTN)gST->ConOut->Mode->Mode, &Cols, &Rows);
  StartCol = (UINTN)gST->ConOut->Mode->CursorColumn;
  StartRow = (UINTN)gST->ConOut->Mode->CursorRow;

  // Keep the edit line on a single screen row so cursor math stays simple.
  MaxLen = Size - 1;
  if (Cols > StartCol + 1 && (Cols - StartCol - 1) < MaxLen) {
    MaxLen = Cols - StartCol - 1;
  }

  for (;;) {
    if (EFI_ERROR(NextKey(&Key))) {
      Buf[0] = L'\0';
      return;
    }

    PrevLen = Len;

    if (Key.UnicodeChar != CHAR_TAB) {
      TabActive = FALSE;
    }

    if (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
      Print(L"\r\n");
      break;
    }

    if (Key.UnicodeChar == CHAR_TAB) {
      if (Cur == Len) {
        if (!TabActive) {
          TabStart = Len;
          while (TabStart > 0 && Buf[TabStart - 1] != L' ') {
            TabStart--;
          }
          StrCpyS(TabOrig, INPUT_SIZE, Buf + TabStart);

          TabCmd = TRUE;
          for (K = 0; K < TabStart; K++) {
            if (Buf[K] != L' ') {
              TabCmd = FALSE;
            }
          }
          for (K = 0; TabOrig[K] != L'\0'; K++) {
            if (TabOrig[K] == L'\\' || TabOrig[K] == L'/' || TabOrig[K] == L':') {
              TabCmd = FALSE;
            }
          }

          TabIdx = 0;
          TabActive = TRUE;
        }

        if (TabCmd ? CompleteCommand(TabOrig, TabIdx, Match, MAX_PATH)
                   : CompleteFile(TabOrig, TabIdx, Match, MAX_PATH)) {
          ApplyCompletion(Buf, Size, TabStart, Match, MaxLen, &Len, &Cur);
          TabIdx++;
        } else if (TabIdx > 0) {
          ApplyCompletion(Buf, Size, TabStart, TabOrig, MaxLen, &Len, &Cur);
          TabIdx = 0;
        } else {
          TabActive = FALSE;
        }
      }
    } else if (Key.UnicodeChar == CHAR_BACKSPACE) {
      if (Cur > 0) {
        CopyMem(&Buf[Cur - 1], &Buf[Cur], (Len - Cur + 1) * sizeof(CHAR16));
        Cur--;
        Len--;
      }
    } else if (Key.UnicodeChar != 0) {
      if (Key.UnicodeChar >= L' ' && Key.UnicodeChar <= L'~' && Len < MaxLen) {
        CopyMem(&Buf[Cur + 1], &Buf[Cur], (Len - Cur + 1) * sizeof(CHAR16));
        Buf[Cur] = Key.UnicodeChar;
        Cur++;
        Len++;
      }
    } else {
      switch (Key.ScanCode) {
        case SCAN_LEFT:
          if (Cur > 0) {
            Cur--;
          }
          break;

        case SCAN_RIGHT:
          if (Cur < Len) {
            Cur++;
          }
          break;

        case SCAN_HOME:
          Cur = 0;
          break;

        case SCAN_END:
          Cur = Len;
          break;

        case SCAN_DELETE:
          if (Cur < Len) {
            CopyMem(&Buf[Cur], &Buf[Cur + 1], (Len - Cur) * sizeof(CHAR16));
            Len--;
          }
          break;

        case SCAN_ESC:
          Buf[0] = L'\0';
          Len = 0;
          Cur = 0;
          break;

        case SCAN_UP:
          if (HistPos > 0) {
            if (HistPos == gHistCount) {
              StrCpyS(Draft, INPUT_SIZE, Buf);
            }
            HistPos--;
            StrCpyS(Buf, Size, gHistory[HistPos]);
            Len = StrLen(Buf);
            if (Len > MaxLen) {
              Buf[MaxLen] = L'\0';
              Len = MaxLen;
            }
            Cur = Len;
          }
          break;

        case SCAN_DOWN:
          if (HistPos < gHistCount) {
            HistPos++;
            if (HistPos == gHistCount) {
              StrCpyS(Buf, Size, Draft);
            } else {
              StrCpyS(Buf, Size, gHistory[HistPos]);
            }
            Len = StrLen(Buf);
            if (Len > MaxLen) {
              Buf[MaxLen] = L'\0';
              Len = MaxLen;
            }
            Cur = Len;
          }
          break;

        default:
          break;
      }
    }

    Redraw(StartCol, StartRow, Buf, Len, Cur, PrevLen);
  }
}

//
// ---------------------------------------------------------------
// Command dispatcher
// ---------------------------------------------------------------
//
STATIC
VOID
RunCommand(CHAR16 *Line)
{
  CHAR16         Command[INPUT_SIZE];
  CHAR16         Name[INPUT_SIZE];
  CHAR16         DotDot[] = L"..";
  CHAR16         RootDir[] = L"\\";
  CHAR16         *Argument;
  CONST COMMAND  *Guess;
  UINTN          Length = 0;
  UINTN          Index;

  while (*Line == L' ' || *Line == L'\t') {
    Line++;
  }

  if (*Line == L'\0') {
    return;
  }

  while (Line[Length] != L'\0' &&
         Line[Length] != L' ' &&
         Line[Length] != L'\t' &&
         Length < INPUT_SIZE - 1) {
    Name[Length] = Line[Length];
    Command[Length] = Up(Line[Length]);
    Length++;
  }
  Name[Length] = L'\0';
  Command[Length] = L'\0';

  Argument = Line + Length;
  while (*Argument == L' ' || *Argument == L'\t') {
    Argument++;
  }

  // Drive switch: "D:"
  if (Length == 2 && Command[1] == L':' && Command[0] >= L'A' && Command[0] <= L'Z') {
    if (Command[0] < L'C' || !SwitchVolume((UINTN)(Command[0] - L'C'))) {
      Print(L"The system cannot find the drive specified.\r\n");
    }
    return;
  }

  // DOS-style shortcuts: "CD.." and "CD\"
  if (StrCmp(Command, L"CD..") == 0) {
    StrCpyS(Command, INPUT_SIZE, L"CD");
    Argument = DotDot;
  } else if (StrCmp(Command, L"CD\\") == 0) {
    StrCpyS(Command, INPUT_SIZE, L"CD");
    Argument = RootDir;
  }

  for (Index = 0; Index < COMMAND_COUNT; Index++) {
    if (StrCmp(Command, gCommands[Index].Name) == 0) {
      gCommands[Index].Fn(Argument);
      return;
    }
  }

  // Not a built-in command: maybe it is a program.
  if (TryRunProgram(Name, Argument)) {
    return;
  }

  // Maybe a typo.
  Guess = SuggestCommand(Command);
  if (Guess != NULL) {
    SetColor(FgAttr(EFI_YELLOW));
    Print(L"Unknown command '%s'. Did you mean '%s'? [Y/n] ", Name, Guess->Name);
    SetColor(gAttr);
    if (AskYesNo()) {
      Guess->Fn(Argument);
    }
    return;
  }

  Print(L"Unknown command: %s\r\n", Name);
  Print(L"Type HELP to see available commands.\r\n");
}

STATIC
VOID
PrintPrompt(VOID)
{
  Print(L"\r\n");
  SetColor(FgAttr(EFI_LIGHTGREEN));
  Print(L"AIOS ");
  SetColor(FgAttr(EFI_YELLOW));
  Print(L"%c:%s", (gVolCount > 0) ? DriveLetter(gCur) : L'?', gCwd);
  SetColor(FgAttr(EFI_LIGHTGREEN));
  Print(L"> ");
  SetColor(gAttr);
}

STATIC
VOID
PrintBanner(VOID)
{
  gST->ConOut->ClearScreen(gST->ConOut);

  SetColor(FgAttr(EFI_LIGHTCYAN));
  Print(L"====================================\r\n");
  Print(L"           Welcome to AIOS\r\n");
  Print(L"====================================\r\n");
  SetColor(gAttr);
  Print(L"AIOS shell version %s\r\n", AIOS_VERSION);
  Print(L"Type HELP to see available commands. Press Tab to complete.\r\n");
  Print(L"%u drive(s) found. Type VOL to list them, DISKS for physical disks.\r\n", (UINT32)gVolCount);

  if (gVolCount == 0) {
    SetColor(FgAttr(EFI_LIGHTRED));
    Print(L"Warning: no file system found.\r\n");
    SetColor(gAttr);
  }
}

EFI_STATUS
EFIAPI
UefiMain(
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  CHAR16  Input[INPUT_SIZE];

  UNREFERENCED_PARAMETER(SystemTable);

  // UEFI resets the machine after 5 minutes unless the watchdog is disabled.
  gBS->SetWatchdogTimer(0, 0, 0, NULL);

  InitVolumes(ImageHandle);
  PrintBanner();

  while (!gExit) {
    PrintPrompt();
    ReadCommandLine(Input, INPUT_SIZE);
    AddHistory(Input);
    RunCommand(Input);
  }

  gAttr = (UINTN)(EFI_LIGHTGRAY | (EFI_BLACK << 4));
  SetColor(gAttr);
  gST->ConOut->ClearScreen(gST->ConOut);

  return EFI_SUCCESS;
}
