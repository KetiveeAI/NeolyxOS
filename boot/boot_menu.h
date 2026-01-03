/*
 * NeolyxOS Boot Menu - OS Selection with F2 Trigger
 * 
 * Scans for installed operating systems and provides selection UI
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_BOOT_MENU_H
#define NEOLYX_BOOT_MENU_H

#include <efi.h>
#include <efilib.h>

/* Maximum number of boot options */
#define MAX_BOOT_OPTIONS 16

/* Boot option types */
#define BOOT_TYPE_NEOLYX   0
#define BOOT_TYPE_WINDOWS  1
#define BOOT_TYPE_LINUX    2
#define BOOT_TYPE_MACOS    3
#define BOOT_TYPE_OTHER    4

/* Boot option structure */
typedef struct {
    CHAR16 name[64];          /* Display name */
    CHAR16 path[256];         /* EFI file path */
    UINT8 type;               /* OS type */
    BOOLEAN is_neolyx;        /* Is this NeolyxOS? */
    EFI_HANDLE device;        /* Device handle */
} BootOption;

/* Boot menu state */
typedef struct {
    BootOption options[MAX_BOOT_OPTIONS];
    UINTN count;
    UINTN selected;
    BOOLEAN menu_triggered;   /* F2 was pressed */
    UINT32 timeout_seconds;   /* Auto-boot timeout */
} BootMenu;

/* ============ Public API ============ */

/* Initialize boot menu */
EFI_STATUS boot_menu_init(BootMenu *menu);

/* Check if F2 is pressed (call early in boot) */
BOOLEAN boot_menu_check_f2(EFI_SYSTEM_TABLE *ST);

/* Quick F2 check with 200ms timeout for fast boot on installed systems */
BOOLEAN boot_menu_check_f2_quick(EFI_SYSTEM_TABLE *ST);

/* Scan for boot options */
EFI_STATUS boot_menu_scan(BootMenu *menu, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

/* Draw boot menu (graphics mode) */
void boot_menu_draw(BootMenu *menu);

/* Handle input and selection */
EFI_STATUS boot_menu_run(BootMenu *menu, EFI_SYSTEM_TABLE *ST);

/* Chain-load selected option */
EFI_STATUS boot_menu_launch(BootMenu *menu, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST);

/* Should we show the menu? */
BOOLEAN boot_menu_should_show(BootMenu *menu);

#endif /* NEOLYX_BOOT_MENU_H */
