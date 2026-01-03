#include "efi.h"
#include "efilib.h"

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Neolyx OS Boot Entry\r\n");
    return 0;
} 