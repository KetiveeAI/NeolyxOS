/*
 * NeolyxOS UEFI Bootloader v8.0 - Production Ready
 * 
 * Professional bootloader with:
 * - Secure ELF validation
 * - Proper boot info handoff to kernel
 * - Memory map passing
 * - Framebuffer info passing
 * - Robust error handling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <efi.h>
#include <efilib.h>
#include "graphics.h"
#include "boot_menu.h"

/* ============ Version ============ */
#define BOOTLOADER_VERSION L"8.0"
#define BOOTLOADER_NAME    L"NeolyxOS UEFI Firmware"

/* ============ ELF Structures ============ */

#define ELF_MAGIC 0x464C457F

typedef struct {
    UINT8  e_ident[16];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

#define PT_LOAD      1
#define ELF_CLASS64  2
#define ELF_MACHINE_X86_64 0x3E

/* ============ Boot Info Structure (passed to kernel) ============ */

typedef struct {
    UINT32 magic;                /* 0x4E454F58 "NEOX" */
    UINT32 version;
    
    /* Memory map */
    UINT64 memory_map_addr;
    UINT64 memory_map_size;
    UINT64 memory_map_desc_size;
    UINT32 memory_map_desc_version;
    
    /* Framebuffer */
    UINT64 framebuffer_addr;
    UINT32 framebuffer_width;
    UINT32 framebuffer_height;
    UINT32 framebuffer_pitch;
    UINT32 framebuffer_bpp;
    
    /* Kernel info */
    UINT64 kernel_physical_base;
    UINT64 kernel_size;
    
    /* ACPI */
    UINT64 acpi_rsdp;
    
    /* Reserved for future use */
    UINT64 reserved[8];
} NeolyxBootInfo;

#define NEOLYX_BOOT_MAGIC   0x4E454F58
#define NEOLYX_BOOT_VERSION 1

/* ============ Global Variables ============ */

static EFI_HANDLE gNeoImageHandle;
static EFI_SYSTEM_TABLE *gNeoST;
static EFI_BOOT_SERVICES *gNeoBS;
static BOOLEAN gGraphicsMode = FALSE;
static EFI_GRAPHICS_OUTPUT_PROTOCOL *gGOP = NULL;

/* Boot info to pass to kernel */
static NeolyxBootInfo gBootInfo;

/* ============ Kernel Entry Type (with boot info) ============ */

typedef void (*KernelEntry)(NeolyxBootInfo *boot_info);

/* ============ Helper Functions ============ */

static void update_progress(CHAR16 *message, UINT32 percent) {
    if (gGraphicsMode) {
        UINT32 width = graphics_get_width();
        UINT32 height = graphics_get_height();
        UINT32 bar_x = (width - 300) / 2;
        UINT32 bar_y = (height / 2) + 90;
        graphics_draw_progress_bar(bar_x, bar_y, 300, 16, percent,
                                   COLOR_DARK_GRAY, COLOR_NEOLYX_CYAN);
    }
    Print(L"    [%3d%%] %s\n", percent, message);
}

static void show_error(CHAR16 *error) {
    if (gGraphicsMode) {
        UINT32 width = graphics_get_width();
        UINT32 height = graphics_get_height();
        graphics_fill_rect((width - 400) / 2, (height / 2) + 150, 400, 30, COLOR_RED);
    }
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_LIGHTRED);
    Print(L"\n    ✗ ERROR: %s\n", error);
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_WHITE);
}

static void show_fatal_error(CHAR16 *error, EFI_STATUS status) {
    show_error(error);
    Print(L"    Status code: 0x%lx\n", status);
    Print(L"\n    System cannot continue. Please restart.\n");
    while (1) { __asm__ volatile("hlt"); }
}

/* ============ ELF Validation ============ */

static EFI_STATUS validate_elf(void *buffer, UINTN size, Elf64_Ehdr **out_header) {
    if (size < sizeof(Elf64_Ehdr)) {
        return EFI_COMPROMISED_DATA;
    }
    
    Elf64_Ehdr *elf = (Elf64_Ehdr*)buffer;
    
    /* Check magic */
    if (*(UINT32*)elf->e_ident != ELF_MAGIC) {
        return EFI_COMPROMISED_DATA;
    }
    
    /* Check 64-bit */
    if (elf->e_ident[4] != ELF_CLASS64) {
        return EFI_UNSUPPORTED;
    }
    
    /* Check x86-64 */
    if (elf->e_machine != ELF_MACHINE_X86_64) {
        return EFI_UNSUPPORTED;
    }
    
    /* Validate program header offset and size */
    if (elf->e_phoff == 0 || elf->e_phnum == 0) {
        return EFI_COMPROMISED_DATA;
    }
    
    UINT64 phdr_end = elf->e_phoff + ((UINT64)elf->e_phnum * elf->e_phentsize);
    if (phdr_end > size) {
        return EFI_COMPROMISED_DATA;
    }
    
    /* Validate entry point is reasonable */
    if (elf->e_entry == 0 || elf->e_entry < 0x100000) {
        return EFI_COMPROMISED_DATA;
    }
    
    *out_header = elf;
    return EFI_SUCCESS;
}

/* ============ Load ELF Segments ============ */

static EFI_STATUS load_elf_segments(void *buffer, UINTN size, Elf64_Ehdr *elf) {
    Elf64_Phdr *phdr = (Elf64_Phdr*)((UINT8*)buffer + elf->e_phoff);
    EFI_STATUS status;
    
    for (UINT16 i = 0; i < elf->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) continue;
        if (phdr[i].p_memsz == 0) continue;
        
        /* Validate segment bounds */
        if (phdr[i].p_offset + phdr[i].p_filesz > size) {
            show_error(L"ELF segment extends beyond file");
            return EFI_COMPROMISED_DATA;
        }
        
        /* Validate virtual address is in reasonable range */
        if (phdr[i].p_vaddr < 0x100000 || phdr[i].p_vaddr > 0xFFFFFFFF) {
            show_error(L"ELF segment has invalid virtual address");
            return EFI_COMPROMISED_DATA;
        }
        
        /* Allocate pages for this segment */
        UINTN pages = (phdr[i].p_memsz + 4095) / 4096;
        EFI_PHYSICAL_ADDRESS segment_addr = phdr[i].p_vaddr;
        
        status = uefi_call_wrapper(gNeoBS->AllocatePages, 4,
            AllocateAddress, EfiLoaderData, pages, &segment_addr);
        
        if (EFI_ERROR(status)) {
            /* Try any address if specific fails */
            segment_addr = 0;
            status = uefi_call_wrapper(gNeoBS->AllocatePages, 4,
                AllocateAnyPages, EfiLoaderData, pages, &segment_addr);
            if (EFI_ERROR(status)) {
                show_error(L"Failed to allocate memory for kernel segment");
                return status;
            }
        }
        
        /* Copy segment data */
        if (phdr[i].p_filesz > 0) {
            CopyMem((void*)segment_addr, 
                   (UINT8*)buffer + phdr[i].p_offset, 
                   phdr[i].p_filesz);
        }
        
        /* Zero remaining memory */
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            SetMem((void*)(segment_addr + phdr[i].p_filesz),
                  phdr[i].p_memsz - phdr[i].p_filesz, 0);
        }
    }
    
    return EFI_SUCCESS;
}

/* ============ ACPI Discovery ============ */

static UINT64 find_acpi_rsdp(void) {
    EFI_GUID acpi20_guid = ACPI_20_TABLE_GUID;
    EFI_GUID acpi10_guid = ACPI_TABLE_GUID;
    
    for (UINTN i = 0; i < gNeoST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &gNeoST->ConfigurationTable[i];
        
        /* Prefer ACPI 2.0+ */
        if (CompareMem(&table->VendorGuid, &acpi20_guid, sizeof(EFI_GUID)) == 0) {
            return (UINT64)table->VendorTable;
        }
    }
    
    /* Fallback to ACPI 1.0 */
    for (UINTN i = 0; i < gNeoST->NumberOfTableEntries; i++) {
        EFI_CONFIGURATION_TABLE *table = &gNeoST->ConfigurationTable[i];
        if (CompareMem(&table->VendorGuid, &acpi10_guid, sizeof(EFI_GUID)) == 0) {
            return (UINT64)table->VendorTable;
        }
    }
    
    return 0;
}

/* ============ Installation Marker Check ============ */

/* Magic value to indicate OS is installed: "INST" in little-endian */
#define NEOLYX_INSTALLED_MAGIC 0x54534E49

static BOOLEAN check_installed_marker(void) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE *root, *marker_file;
    
    /* Get protocols */
    status = uefi_call_wrapper(gNeoBS->HandleProtocol, 3,
        gNeoImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&loaded_image);
    if (EFI_ERROR(status)) {
        return FALSE;
    }
    
    status = uefi_call_wrapper(gNeoBS->HandleProtocol, 3,
        loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
    if (EFI_ERROR(status)) {
        return FALSE;
    }
    
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        return FALSE;
    }
    
    /* Try to open the marker file */
    status = uefi_call_wrapper(root->Open, 5, root, &marker_file,
        L"\\EFI\\BOOT\\neolyx.installed", EFI_FILE_MODE_READ, 0);
    
    if (!EFI_ERROR(status)) {
        /* File exists - OS is installed */
        uefi_call_wrapper(marker_file->Close, 1, marker_file);
        uefi_call_wrapper(root->Close, 1, root);
        return TRUE;
    }
    
    uefi_call_wrapper(root->Close, 1, root);
    return FALSE;
}

/* ============ Kernel Loading ============ */

static EFI_STATUS load_kernel(UINT64 *entry_point, UINTN *kernel_size_out) {
    EFI_STATUS status;
    EFI_LOADED_IMAGE *loaded_image;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    EFI_FILE *root, *kernel_file;
    
    update_progress(L"Initializing filesystem...", 15);
    
    /* Get protocols */
    status = uefi_call_wrapper(gNeoBS->HandleProtocol, 3,
        gNeoImageHandle, &gEfiLoadedImageProtocolGuid, (VOID**)&loaded_image);
    if (EFI_ERROR(status)) {
        show_error(L"Failed to get Loaded Image Protocol");
        return status;
    }
    
    status = uefi_call_wrapper(gNeoBS->HandleProtocol, 3,
        loaded_image->DeviceHandle, &gEfiSimpleFileSystemProtocolGuid, (VOID**)&fs);
    if (EFI_ERROR(status)) {
        show_error(L"Failed to get File System Protocol");
        return status;
    }
    
    status = uefi_call_wrapper(fs->OpenVolume, 2, fs, &root);
    if (EFI_ERROR(status)) {
        show_error(L"Failed to open root volume");
        return status;
    }
    
    update_progress(L"Searching for kernel...", 25);
    
    /* Search for kernel */
    CHAR16 *kernel_paths[] = {
        L"\\EFI\\BOOT\\kernel.bin",
        L"\\kernel.bin", 
        L"\\boot\\kernel.elf",
        L"\\System\\kernel.bin",
        NULL
    };
    
    kernel_file = NULL;
    for (int i = 0; kernel_paths[i]; i++) {
        status = uefi_call_wrapper(root->Open, 5, root, &kernel_file,
            kernel_paths[i], EFI_FILE_MODE_READ, 0);
        if (!EFI_ERROR(status)) {
            Print(L"    Found: %s\n", kernel_paths[i]);
            break;
        }
        kernel_file = NULL;
    }
    
    if (!kernel_file) {
        show_error(L"Kernel not found!");
        Print(L"\n    Expected at: \\EFI\\BOOT\\kernel.bin\n");
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_NOT_FOUND;
    }
    
    update_progress(L"Reading kernel file...", 35);
    
    /* Get file size */
    UINT8 info_buffer[512];
    UINTN info_size = sizeof(info_buffer);
    status = uefi_call_wrapper(kernel_file->GetInfo, 4,
        kernel_file, &gEfiFileInfoGuid, &info_size, info_buffer);
    if (EFI_ERROR(status)) {
        show_error(L"Failed to get kernel file info");
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }
    
    EFI_FILE_INFO *file_info = (EFI_FILE_INFO*)info_buffer;
    UINTN kernel_size = file_info->FileSize;
    
    if (kernel_size == 0 || kernel_size > 64 * 1024 * 1024) {
        show_error(L"Invalid kernel size");
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return EFI_COMPROMISED_DATA;
    }
    
    Print(L"    Kernel size: %lu bytes\n", kernel_size);
    *kernel_size_out = kernel_size;
    
    update_progress(L"Allocating kernel memory...", 45);
    
    /* Allocate kernel buffer */
    VOID *kernel_buffer;
    status = uefi_call_wrapper(gNeoBS->AllocatePool, 3,
        EfiLoaderData, kernel_size, &kernel_buffer);
    if (EFI_ERROR(status)) {
        show_error(L"Failed to allocate kernel memory");
        uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
        uefi_call_wrapper(root->Close, 1, root);
        return status;
    }
    
    update_progress(L"Loading kernel...", 55);
    
    /* Read kernel */
    UINTN read_size = kernel_size;
    status = uefi_call_wrapper(kernel_file->Read, 3,
        kernel_file, &read_size, kernel_buffer);
    
    uefi_call_wrapper(kernel_file->Close, 1, kernel_file);
    uefi_call_wrapper(root->Close, 1, root);
    
    if (EFI_ERROR(status) || read_size != kernel_size) {
        show_error(L"Failed to read kernel file");
        uefi_call_wrapper(gNeoBS->FreePool, 1, kernel_buffer);
        return EFI_DEVICE_ERROR;
    }
    
    update_progress(L"Validating kernel...", 65);
    
    /* Check kernel format */
    UINT32 *magic = (UINT32*)kernel_buffer;
    
    if (*magic == ELF_MAGIC) {
        /* ELF kernel */
        Elf64_Ehdr *elf;
        status = validate_elf(kernel_buffer, kernel_size, &elf);
        if (EFI_ERROR(status)) {
            show_error(L"Invalid or unsupported ELF format");
            uefi_call_wrapper(gNeoBS->FreePool, 1, kernel_buffer);
            return status;
        }
        
        update_progress(L"Loading ELF segments...", 75);
        
        status = load_elf_segments(kernel_buffer, kernel_size, elf);
        if (EFI_ERROR(status)) {
            uefi_call_wrapper(gNeoBS->FreePool, 1, kernel_buffer);
            return status;
        }
        
        *entry_point = elf->e_entry;
        gBootInfo.kernel_physical_base = 0x100000;
        
        Print(L"    ELF entry point: 0x%lx\n", *entry_point);
    } else {
        /* Raw binary - load at 1MB */
        update_progress(L"Loading raw binary...", 75);
        
        EFI_PHYSICAL_ADDRESS load_addr = 0x100000;
        UINTN pages = (kernel_size + 4095) / 4096;
        
        status = uefi_call_wrapper(gNeoBS->AllocatePages, 4,
            AllocateAddress, EfiLoaderData, pages, &load_addr);
        if (EFI_ERROR(status)) {
            show_error(L"Failed to allocate memory at 1MB");
            uefi_call_wrapper(gNeoBS->FreePool, 1, kernel_buffer);
            return status;
        }
        
        CopyMem((void*)0x100000, kernel_buffer, kernel_size);
        *entry_point = 0x100000;
        gBootInfo.kernel_physical_base = 0x100000;
        
        Print(L"    Raw binary at: 0x100000\n");
    }
    
    gBootInfo.kernel_size = kernel_size;
    uefi_call_wrapper(gNeoBS->FreePool, 1, kernel_buffer);
    
    return EFI_SUCCESS;
}

/* ============ Exit Boot Services & Jump ============ */

static void jump_to_kernel(UINT64 entry_point) {
    EFI_STATUS status;
    UINTN map_size = 0;
    UINTN map_key;
    UINTN desc_size;
    UINT32 desc_version;
    EFI_MEMORY_DESCRIPTOR *memory_map = NULL;
    
    update_progress(L"Getting memory map...", 85);
    
    /* Get memory map size */
    status = uefi_call_wrapper(gNeoBS->GetMemoryMap, 5,
        &map_size, NULL, &map_key, &desc_size, &desc_version);
    
    /* Allocate with extra space */
    map_size += 4 * desc_size;
    status = uefi_call_wrapper(gNeoBS->AllocatePool, 3,
        EfiLoaderData, map_size, (VOID**)&memory_map);
    if (EFI_ERROR(status)) {
        show_fatal_error(L"Failed to allocate memory map", status);
    }
    
    /* Get actual memory map */
    status = uefi_call_wrapper(gNeoBS->GetMemoryMap, 5,
        &map_size, memory_map, &map_key, &desc_size, &desc_version);
    if (EFI_ERROR(status)) {
        show_fatal_error(L"Failed to get memory map", status);
    }
    
    /* Populate boot info */
    gBootInfo.magic = NEOLYX_BOOT_MAGIC;
    gBootInfo.version = NEOLYX_BOOT_VERSION;
    gBootInfo.memory_map_addr = (UINT64)memory_map;
    gBootInfo.memory_map_size = map_size;
    gBootInfo.memory_map_desc_size = desc_size;
    gBootInfo.memory_map_desc_version = desc_version;
    gBootInfo.acpi_rsdp = find_acpi_rsdp();
    
    /* Framebuffer info */
    if (gGOP) {
        gBootInfo.framebuffer_addr = gGOP->Mode->FrameBufferBase;
        gBootInfo.framebuffer_width = gGOP->Mode->Info->HorizontalResolution;
        gBootInfo.framebuffer_height = gGOP->Mode->Info->VerticalResolution;
        gBootInfo.framebuffer_pitch = gGOP->Mode->Info->PixelsPerScanLine * 4;
        gBootInfo.framebuffer_bpp = 32;
    }
    
    /* Check if NeolyxOS is already installed (Stage 4 per roadmap.pdf) */
    /* Pass installation status to kernel via reserved[0] */
    if (check_installed_marker()) {
        gBootInfo.reserved[0] = NEOLYX_INSTALLED_MAGIC;  /* "INST" = installed */
        Print(L"    [BOOT] Found neolyx.installed - Normal Boot\n");
    } else {
        gBootInfo.reserved[0] = 0;  /* Not installed - run installer */
        Print(L"    [BOOT] No neolyx.installed - Installer Mode\n");
    }
    
    update_progress(L"Exiting boot services...", 90);
    
    /* Note: No more Print() after ExitBootServices! */
    
    /* Exit boot services - with proper retry */
    status = uefi_call_wrapper(gNeoBS->ExitBootServices, 2, gNeoImageHandle, map_key);
    
    if (EFI_ERROR(status)) {
        /* Memory map changed, retry once */
        map_size = 0;
        uefi_call_wrapper(gNeoBS->GetMemoryMap, 5,
            &map_size, NULL, &map_key, &desc_size, &desc_version);
        map_size += 4 * desc_size;
        
        /* Must re-allocate with new size */
        uefi_call_wrapper(gNeoBS->FreePool, 1, memory_map);
        uefi_call_wrapper(gNeoBS->AllocatePool, 3,
            EfiLoaderData, map_size, (VOID**)&memory_map);
        uefi_call_wrapper(gNeoBS->GetMemoryMap, 5,
            &map_size, memory_map, &map_key, &desc_size, &desc_version);
        
        /* Update boot info */
        gBootInfo.memory_map_addr = (UINT64)memory_map;
        gBootInfo.memory_map_size = map_size;
        
        status = uefi_call_wrapper(gNeoBS->ExitBootServices, 2, gNeoImageHandle, map_key);
        
        if (EFI_ERROR(status)) {
            /* Fatal - cannot continue */
            while (1) { __asm__ volatile("cli; hlt"); }
        }
    }
    
    /* ================================================================
     * POINT OF NO RETURN
     * Boot services are now unavailable.
     * No more Print(), AllocatePool(), or any UEFI services.
     * ================================================================ */
    
    /* Jump to kernel with boot info */
    KernelEntry kernel = (KernelEntry)entry_point;
    kernel(&gBootInfo);
    
    /* Should never return */
    while (1) { __asm__ volatile("cli; hlt"); }
}

/* ============ Boot Sequences ============ */

static void graphical_boot_sequence(void) {
    UINT32 width = graphics_get_width();
    UINT32 height = graphics_get_height();
    
    graphics_clear(COLOR_DARK_BG);
    graphics_draw_logo();
    
    UINT32 text_x = (width - 200) / 2;
    UINT32 text_y = (height / 2) + 30;
    graphics_fill_rect(text_x, text_y, 200, 20, COLOR_CYAN);
    
    /* Initial animation */
    UINT32 bar_x = (width - 300) / 2;
    UINT32 bar_y = text_y + 60;
    for (UINT32 p = 0; p <= 10; p++) {
        graphics_draw_progress_bar(bar_x, bar_y, 300, 16, p,
                                   COLOR_DARK_GRAY, COLOR_NEOLYX_CYAN);
        uefi_call_wrapper(gNeoST->BootServices->Stall, 1, 30000);
    }
}

static void text_boot_sequence(void) {
    uefi_call_wrapper(gNeoST->ConOut->ClearScreen, 1, gNeoST->ConOut);
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_CYAN);
    
    Print(L"\n\n");
    Print(L"    NeolyxOS UEFI Firmware v%s\n", BOOTLOADER_VERSION);
    Print(L"    Copyright (c) 2025 KetiveeAI\n\n");
    
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_WHITE);
}

/* ============ Main Entry Point ============ */

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
    EFI_STATUS status;
    UINT64 kernel_entry = 0;
    UINTN kernel_size = 0;
    
    /* Initialize globals (FIXED: using correct variable names) */
    gNeoImageHandle = ImageHandle;
    gNeoST = SystemTable;
    gNeoBS = SystemTable->BootServices;
    
    /* Initialize gnu-efi */
    InitializeLib(ImageHandle, SystemTable);
    
    /* Clear boot info */
    SetMem(&gBootInfo, sizeof(gBootInfo), 0);
    
    /* Try graphics */
    status = graphics_init(SystemTable);
    gGraphicsMode = !EFI_ERROR(status);
    
    if (gGraphicsMode) {
        /* Save GOP pointer for boot info */
        uefi_call_wrapper(gNeoBS->LocateProtocol, 3,
            &gEfiGraphicsOutputProtocolGuid, NULL, (VOID**)&gGOP);
        graphical_boot_sequence();
    } else {
        text_boot_sequence();
    }
    
    Print(L"\n");
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_LIGHTGREEN);
    Print(L"    ✓ %s v%s\n\n", BOOTLOADER_NAME, BOOTLOADER_VERSION);
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_WHITE);
    
    /* ============ Boot Menu Logic ============ */
    {
        BootMenu boot_menu;
        boot_menu_init(&boot_menu);
        
        /* Check if OS is installed for fast boot path */
        BOOLEAN is_installed = check_installed_marker();
        
        if (is_installed) {
            /* Fast boot: 200ms F2 window for installed systems */
            Print(L"    NeolyxOS detected - Fast boot (F2 for menu)...\n");
            boot_menu.menu_triggered = boot_menu_check_f2_quick(gNeoST);
        } else {
            /* Fresh install: 500ms F2 window */
            Print(L"    Press F2 for boot menu...\n");
            boot_menu.menu_triggered = boot_menu_check_f2(gNeoST);
        }
        
        /* Scan for other boot options */
        boot_menu_scan(&boot_menu, ImageHandle, gNeoST);
        
        /* Show menu if needed */
        if (boot_menu_should_show(&boot_menu)) {
            Print(L"    Boot menu activated\n");
            boot_menu_run(&boot_menu, gNeoST);
            
            /* If user selected non-NeolyxOS option, chain-load it */
            if (!boot_menu.options[boot_menu.selected].is_neolyx) {
                status = boot_menu_launch(&boot_menu, ImageHandle, gNeoST);
                /* If chain-load returns, there was an error */
                if (EFI_ERROR(status)) {
                    Print(L"    Chain-load failed, continuing with NeolyxOS...\n");
                }
            }
        } else {
            if (is_installed) {
                Print(L"    Direct boot to NeolyxOS...\n");
            } else {
                Print(L"    No other OS detected, booting NeolyxOS directly\n");
            }
        }
    }
    
    /* Load kernel */
    status = load_kernel(&kernel_entry, &kernel_size);
    if (EFI_ERROR(status)) {
        show_error(L"Kernel loading failed!");
        Print(L"\n    Press any key to restart...\n");
        
        UINTN index;
        EFI_INPUT_KEY key;
        uefi_call_wrapper(gNeoBS->WaitForEvent, 3, 1, &gNeoST->ConIn->WaitForKey, &index);
        uefi_call_wrapper(gNeoST->ConIn->ReadKeyStroke, 2, gNeoST->ConIn, &key);
        
        /* Attempt to restart */
        uefi_call_wrapper(gNeoST->RuntimeServices->ResetSystem, 4,
            EfiResetCold, EFI_SUCCESS, 0, NULL);
        
        while (1) { __asm__ volatile("hlt"); }
    }
    
    update_progress(L"Kernel ready...", 80);
    
    if (gGraphicsMode) {
        UINT32 w = graphics_get_width();
        UINT32 h = graphics_get_height();
        graphics_fill_rect((w - 250) / 2, (h / 2) + 120, 250, 30, COLOR_GREEN);
    }
    
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_LIGHTGREEN);
    Print(L"\n    ✓ Kernel loaded: %lu bytes\n", kernel_size);
    Print(L"    ✓ Entry point: 0x%lx\n", kernel_entry);
    Print(L"    ✓ Boot info at: 0x%lx\n\n", (UINT64)&gBootInfo);
    uefi_call_wrapper(gNeoST->ConOut->SetAttribute, 2, gNeoST->ConOut, EFI_WHITE);
    
    /* Brief delay */
    uefi_call_wrapper(gNeoBS->Stall, 1, 500000);
    
    /* Jump to kernel - no return */
    jump_to_kernel(kernel_entry);
    
    /* Never reached */
    return EFI_SUCCESS;
}