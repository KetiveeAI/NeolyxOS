#include "../include/stddef.h"
#include "efi.h"
#include "efilib.h"

static EFI_SYSTEM_TABLE *gST = NULL;

void InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    gST = SystemTable;
    (void)ImageHandle;
}

void Print(const CHAR16* str) {
    if (gST && gST->ConOut && gST->ConOut->OutputString) {
        gST->ConOut->OutputString(gST->ConOut, (CHAR16*)str);
    }
} 