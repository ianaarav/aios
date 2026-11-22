/*
 * HELLO - a tiny sample program for AIOS.
 * Build it, copy HELLO.efi next to AIOS, then type:  hello your name
 */

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/LoadedImage.h>

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *Image = NULL;
  CHAR16                     *Args = NULL;

  if (!EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&Image))) {
    if (Image->LoadOptionsSize >= sizeof (CHAR16)) {
      Args = (CHAR16 *)Image->LoadOptions;
    }
  }

  Print (L"\r\nHello from a program running on AIOS!\r\n");
  if (Args != NULL && Args[0] != L'\0') {
    Print (L"You gave me these arguments: %s\r\n", Args);
  } else {
    Print (L"(no arguments - try: hello your name)\r\n");
  }

  return EFI_SUCCESS;
}
