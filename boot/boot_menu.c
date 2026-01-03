/*
 * NeolyxOS Boot Menu - OS Selection with F2 Trigger
 * 
 * Scans for installed operating systems and provides selection UI
 * Auto-skips menu if only NeolyxOS is detected (unless F2 pressed)
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <efi.h>
#include <efilib.h>
#include "boot_menu.h"
#include "graphics.h"

/* ============ Constants ============ */

#define F2_SCANCODE      0x0C
#define UP_SCANCODE      0x01
#define DOWN_SCANCODE    0x02
#define ENTER_SCANCODE   0x0D
#define ESC_SCANCODE     0x17

#define MENU_TIMEOUT_DEFAULT  5  /* seconds */

/* Colors for menu */
#define MENU_BG_COLOR       0x00101020
#define MENU_TITLE_COLOR    0x0000B4D8
#define MENU_ITEM_COLOR     0x00FFFFFF
#define MENU_SELECT_BG      0x00304060
#define MENU_SELECT_FG      0x0000D4F8

/* ============ Internal State ============ */

static EFI_GRAPHICS_OUTPUT_PROTOCOL *gMenuGOP = NULL;

/* ============ Helper Functions ============ */

static void draw_menu_text(UINT32 x, UINT32 y, const CHAR16 *text, UINT32 color) {
    /* Use graphics primitives from graphics.c */
    /* For now, we'll use text mode fallback in actual implementation */
    (void)x; (void)y; (void)text; (void)color;
}

static void menu_fill_rect(UINT32 x, UINT32 y, UINT32 w, UINT32 h, UINT32 color) {
    if (!gMenuGOP) return;
    
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL pixel;
    pixel.Blue  = (color >> 0) & 0xFF;
    pixel.Green = (color >> 8) & 0xFF;
    pixel.Red   = (color >> 16) & 0xFF;
    pixel.Reserved = 0;
    
    uefi_call_wrapper(gMenuGOP->Blt, 10, gMenuGOP, &pixel,
        EfiBltVideoFill, 0, 0, x, y, w, h, 0);
}

/* ============ F2 Key Detection ============ */

BOOLEAN boot_menu_check_f2(EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    UINTN index;
    
    /* Set a short timeout to check for keypress */
    UINT64 timeout_ns = 500 * 1000 * 10;  /* 500ms in 100ns units */
    
    /* Create timer event */
    EFI_EVENT timer_event;
    status = uefi_call_wrapper(ST->BootServices->CreateEvent, 5,
        EVT_TIMER, TPL_APPLICATION, NULL, NULL, &timer_event);
    if (EFI_ERROR(status)) return FALSE;
    
    /* Set timer */
    status = uefi_call_wrapper(ST->BootServices->SetTimer, 3,
        timer_event, TimerRelative, timeout_ns);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(ST->BootServices->CloseEvent, 1, timer_event);
        return FALSE;
    }
    
    /* Wait for key or timeout */
    EFI_EVENT events[2];
    events[0] = ST->ConIn->WaitForKey;
    events[1] = timer_event;
    
    status = uefi_call_wrapper(ST->BootServices->WaitForEvent, 3,
        2, events, &index);
    
    uefi_call_wrapper(ST->BootServices->CloseEvent, 1, timer_event);
    
    if (EFI_ERROR(status) || index == 1) {
        /* Timeout - no key pressed */
        return FALSE;
    }
    
    /* Read the key */
    status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
    if (EFI_ERROR(status)) return FALSE;
    
    /* Check for F2 */
    if (key.ScanCode == F2_SCANCODE) {
        return TRUE;
    }
    
    return FALSE;
}

/* Fast boot version with 200ms timeout for installed systems */
BOOLEAN boot_menu_check_f2_quick(EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    UINTN index;
    
    /* 200ms timeout for fast boot on installed systems */
    UINT64 timeout_ns = 200 * 1000 * 10;  /* 200ms in 100ns units */
    
    /* Create timer event */
    EFI_EVENT timer_event;
    status = uefi_call_wrapper(ST->BootServices->CreateEvent, 5,
        EVT_TIMER, TPL_APPLICATION, NULL, NULL, &timer_event);
    if (EFI_ERROR(status)) return FALSE;
    
    /* Set timer */
    status = uefi_call_wrapper(ST->BootServices->SetTimer, 3,
        timer_event, TimerRelative, timeout_ns);
    if (EFI_ERROR(status)) {
        uefi_call_wrapper(ST->BootServices->CloseEvent, 1, timer_event);
        return FALSE;
    }
    
    /* Wait for key or timeout */
    EFI_EVENT events[2];
    events[0] = ST->ConIn->WaitForKey;
    events[1] = timer_event;
    
    status = uefi_call_wrapper(ST->BootServices->WaitForEvent, 3,
        2, events, &index);
    
    uefi_call_wrapper(ST->BootServices->CloseEvent, 1, timer_event);
    
    if (EFI_ERROR(status) || index == 1) {
        /* Timeout - no key pressed */
        return FALSE;
    }
    
    /* Read the key */
    status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
    if (EFI_ERROR(status)) return FALSE;
    
    /* Check for F2 */
    if (key.ScanCode == F2_SCANCODE) {
        return TRUE;
    }
    
    return FALSE;
}

/* ============ Boot Option Scanning ============ */

EFI_STATUS boot_menu_init(BootMenu *menu) {
    if (!menu) return EFI_INVALID_PARAMETER;
    
    /* Clear menu structure */
    SetMem(menu, sizeof(BootMenu), 0);
    menu->count = 0;
    menu->selected = 0;
    menu->menu_triggered = FALSE;
    menu->timeout_seconds = MENU_TIMEOUT_DEFAULT;
    
    return EFI_SUCCESS;
}

EFI_STATUS boot_menu_scan(BootMenu *menu, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE *root;
    UINTN handle_count;
    EFI_HANDLE *handles = NULL;
    
    if (!menu) return EFI_INVALID_PARAMETER;
    
    /* Add NeolyxOS as first option */
    StrCpy(menu->options[0].name, L"NeolyxOS");
    StrCpy(menu->options[0].path, L"\\EFI\\BOOT\\kernel.bin");
    menu->options[0].type = BOOT_TYPE_NEOLYX;
    menu->options[0].is_neolyx = TRUE;
    menu->count = 1;
    
    /* Get all filesystem handles */
    status = uefi_call_wrapper(ST->BootServices->LocateHandleBuffer, 5,
        ByProtocol, &gEfiSimpleFileSystemProtocolGuid, NULL, 
        &handle_count, &handles);
    
    if (EFI_ERROR(status)) {
        return EFI_SUCCESS;  /* Just NeolyxOS available */
    }
    
    /* Scan each filesystem for boot options */
    for (UINTN i = 0; i < handle_count && menu->count < MAX_BOOT_OPTIONS; i++) {
        status = uefi_call_wrapper(ST->BootServices->HandleProtocol, 3,
            handles[i], &gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
        if (EFI_ERROR(status)) continue;
        
        status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
        if (EFI_ERROR(status)) continue;
        
        /* Check for Windows Boot Manager */
        EFI_FILE *windows_file;
        status = uefi_call_wrapper(root->Open, 5, root, &windows_file,
            L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi", EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(windows_file->Close, 1, windows_file);
            
            UINTN idx = menu->count++;
            StrCpy(menu->options[idx].name, L"Windows Boot Manager");
            StrCpy(menu->options[idx].path, L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
            menu->options[idx].type = BOOT_TYPE_WINDOWS;
            menu->options[idx].is_neolyx = FALSE;
            menu->options[idx].device = handles[i];
        }
        
        /* Check for Linux (GRUB) */
        EFI_FILE *grub_file;
        status = uefi_call_wrapper(root->Open, 5, root, &grub_file,
            L"\\EFI\\ubuntu\\grubx64.efi", EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(grub_file->Close, 1, grub_file);
            
            UINTN idx = menu->count++;
            StrCpy(menu->options[idx].name, L"Ubuntu Linux");
            StrCpy(menu->options[idx].path, L"\\EFI\\ubuntu\\grubx64.efi");
            menu->options[idx].type = BOOT_TYPE_LINUX;
            menu->options[idx].is_neolyx = FALSE;
            menu->options[idx].device = handles[i];
        }
        
        /* Check for Fedora */
        status = uefi_call_wrapper(root->Open, 5, root, &grub_file,
            L"\\EFI\\fedora\\grubx64.efi", EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            uefi_call_wrapper(grub_file->Close, 1, grub_file);
            
            UINTN idx = menu->count++;
            StrCpy(menu->options[idx].name, L"Fedora Linux");
            StrCpy(menu->options[idx].path, L"\\EFI\\fedora\\grubx64.efi");
            menu->options[idx].type = BOOT_TYPE_LINUX;
            menu->options[idx].is_neolyx = FALSE;
            menu->options[idx].device = handles[i];
        }
        
        uefi_call_wrapper(root->Close, 1, root);
    }
    
    if (handles) {
        uefi_call_wrapper(ST->BootServices->FreePool, 1, handles);
    }
    
    return EFI_SUCCESS;
}

/* ============ Menu Display ============ */

BOOLEAN boot_menu_should_show(BootMenu *menu) {
    if (!menu) return FALSE;
    
    /* Always show if F2 was pressed */
    if (menu->menu_triggered) return TRUE;
    
    /* Show if multiple boot options exist */
    if (menu->count > 1) return TRUE;
    
    /* Only NeolyxOS - skip menu */
    return FALSE;
}

void boot_menu_draw(BootMenu *menu) {
    if (!gMenuGOP || !menu) return;
    
    UINT32 width = gMenuGOP->Mode->Info->HorizontalResolution;
    UINT32 height = gMenuGOP->Mode->Info->VerticalResolution;
    
    /* Clear screen */
    menu_fill_rect(0, 0, width, height, MENU_BG_COLOR);
    
    /* Draw title bar */
    menu_fill_rect(0, 0, width, 50, 0x00202040);
    
    /* Menu box dimensions */
    UINT32 box_w = 400;
    UINT32 box_h = 60 + menu->count * 40;
    UINT32 box_x = (width - box_w) / 2;
    UINT32 box_y = (height - box_h) / 2;
    
    /* Draw menu box background */
    menu_fill_rect(box_x, box_y, box_w, box_h, 0x00202040);
    
    /* Draw border */
    menu_fill_rect(box_x, box_y, box_w, 3, MENU_TITLE_COLOR);
    menu_fill_rect(box_x, box_y + box_h - 3, box_w, 3, MENU_TITLE_COLOR);
    menu_fill_rect(box_x, box_y, 3, box_h, MENU_TITLE_COLOR);
    menu_fill_rect(box_x + box_w - 3, box_y, 3, box_h, MENU_TITLE_COLOR);
    
    /* Draw menu items */
    for (UINTN i = 0; i < menu->count; i++) {
        UINT32 item_y = box_y + 40 + i * 40;
        
        /* Highlight selected item */
        if (i == menu->selected) {
            menu_fill_rect(box_x + 10, item_y, box_w - 20, 32, MENU_SELECT_BG);
        }
    }
}

/* ============ Menu Input Handling ============ */

EFI_STATUS boot_menu_run(BootMenu *menu, EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    EFI_INPUT_KEY key;
    UINTN index;
    
    if (!menu || !ST) return EFI_INVALID_PARAMETER;
    
    /* Get GOP for drawing */
    status = uefi_call_wrapper(ST->BootServices->LocateProtocol, 3,
        &gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gMenuGOP);
    
    /* Draw initial menu */
    boot_menu_draw(menu);
    
    /* Menu must be displayed using text mode if no GOP */
    if (!gMenuGOP) {
        Print(L"\n\n");
        Print(L"  NeolyxOS Boot Menu\n");
        Print(L"  ==================\n\n");
        
        for (UINTN i = 0; i < menu->count; i++) {
            if (i == menu->selected) {
                Print(L"  > %s\n", menu->options[i].name);
            } else {
                Print(L"    %s\n", menu->options[i].name);
            }
        }
        
        Print(L"\n  [UP/DOWN] Select  [ENTER] Boot  [ESC] Default\n");
    }
    
    /* Input loop */
    while (1) {
        /* Wait for key */
        uefi_call_wrapper(ST->BootServices->WaitForEvent, 3,
            1, &ST->ConIn->WaitForKey, &index);
        
        status = uefi_call_wrapper(ST->ConIn->ReadKeyStroke, 2, ST->ConIn, &key);
        if (EFI_ERROR(status)) continue;
        
        switch (key.ScanCode) {
            case UP_SCANCODE:
                if (menu->selected > 0) {
                    menu->selected--;
                    boot_menu_draw(menu);
                }
                break;
                
            case DOWN_SCANCODE:
                if (menu->selected < menu->count - 1) {
                    menu->selected++;
                    boot_menu_draw(menu);
                }
                break;
                
            case ESC_SCANCODE:
                /* ESC - boot default (NeolyxOS) */
                menu->selected = 0;
                return EFI_SUCCESS;
        }
        
        /* Check for Enter (UnicodeChar for Enter is carriage return) */
        if (key.UnicodeChar == CHAR_CARRIAGE_RETURN) {
            return EFI_SUCCESS;
        }
    }
}

/* ============ Boot Selected Option ============ */

EFI_STATUS boot_menu_launch(BootMenu *menu, EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *ST) {
    EFI_STATUS status;
    
    if (!menu || menu->selected >= menu->count) {
        return EFI_INVALID_PARAMETER;
    }
    
    BootOption *opt = &menu->options[menu->selected];
    
    /* If it's NeolyxOS, return success - caller will load kernel normally */
    if (opt->is_neolyx) {
        Print(L"    Booting NeolyxOS...\n");
        return EFI_SUCCESS;
    }
    
    /* Chain-load another EFI application */
    Print(L"    Chain-loading %s...\n", opt->name);
    
    /* Load the EFI image */
    EFI_DEVICE_PATH *device_path = FileDevicePath(opt->device, opt->path);
    if (!device_path) {
        Print(L"    Error: Failed to create device path\n");
        return EFI_NOT_FOUND;
    }
    
    EFI_HANDLE loaded_image_handle;
    status = uefi_call_wrapper(ST->BootServices->LoadImage, 6,
        FALSE, ImageHandle, device_path, NULL, 0, &loaded_image_handle);
    
    FreePool(device_path);
    
    if (EFI_ERROR(status)) {
        Print(L"    Error: Failed to load image: 0x%lx\n", status);
        return status;
    }
    
    /* Start the loaded image */
    status = uefi_call_wrapper(ST->BootServices->StartImage, 3,
        loaded_image_handle, NULL, NULL);
    
    /* If we get here, the loaded OS returned (unusual) */
    return status;
}
