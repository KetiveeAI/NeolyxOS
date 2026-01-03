#ifndef EFILIB_H
#define EFILIB_H

#include "efi.h"

#define EFI_ERROR(Status) (((long long)(Status)) < 0)

void InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
void Print(const CHAR16* str);

#endif // EFILIB_H 