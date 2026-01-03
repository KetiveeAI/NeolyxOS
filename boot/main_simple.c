#include "../include/stddef.h"
#include "efi.h"
#include "efilib.h"

// Simple UEFI types
typedef unsigned long long EFI_STATUS;
typedef void* EFI_HANDLE;

// Simple UEFI structures
typedef struct {
    void* BootServices;
} EFI_SYSTEM_TABLE;

// Simple function declarations
void InitializeLib(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
void Print(const char* str);

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    InitializeLib(ImageHandle, SystemTable);
    Print(L"Neolyx OS Simple Bootloader\r\n");
    Print(L"Initializing...\r\n");
    
    // Simple boot process
    Print(L"Loading kernel...\r\n");
    Print(L"Starting Neolyx OS...\r\n");
    
    // Infinite loop to keep bootloader running
    while (1) {
        // Keep the bootloader alive
    }
    
    return 0;
} 