/*
 * NeolyxOS - Desktop image handed over by the bootloader
 *
 * See include/core/boot_image.h. The image used to be compiled into the
 * kernel (fs/desktop_elf_data.h), which made kernel.bin grow with the
 * desktop. The raw kernel must fit in the free memory at 1 MB; on OVMF
 * that ends at 8 MB, where ACPI NVS begins, so the desktop now lives on
 * the boot volume and is loaded by the UEFI loader.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <core/boot_image.h>
#include <mm/addrspace.h>

extern void serial_puts(const char *s);
extern void serial_puthex(uint64_t v);
extern void pmm_reserve_region(uint64_t start, uint64_t size);

static const void *g_desktop_image = 0;
static uint64_t g_desktop_image_size = 0;

#define ELF_MAGIC_LE 0x464C457FU    /* "\x7FELF" */

int boot_desktop_image_init(uint64_t phys, uint64_t size, uint64_t magic) {
    g_desktop_image = 0;
    g_desktop_image_size = 0;

    if (magic != BOOT_DESKTOP_MAGIC) {
        serial_puts("[BOOTIMG] ERROR: bootloader passed no desktop image "
                    "(is \\EFI\\BOOT\\desktop.elf on the boot volume?)\n");
        return -1;
    }
    if (phys == 0 || (phys & 0xFFFULL) != 0 || size < 64 || size > BOOT_DESKTOP_MAX_SIZE) {
        serial_puts("[BOOTIMG] ERROR: desktop image has invalid address or size\n");
        return -1;
    }
    uint64_t end = (phys + size + 0xFFFULL) & ~0xFFFULL;
    if (end < phys || end > 0x100000000ULL) {
        serial_puts("[BOOTIMG] ERROR: desktop image is not below 4 GB\n");
        return -1;
    }
    /* The user window is remapped per process; the kernel could not rely on
     * reading the image there once a process address space is active. */
    if (phys < USER_WINDOW_END && end > USER_WINDOW_BASE) {
        serial_puts("[BOOTIMG] ERROR: desktop image overlaps the user window\n");
        return -1;
    }
    if (*(const volatile uint32_t *)(uintptr_t)phys != ELF_MAGIC_LE) {
        serial_puts("[BOOTIMG] ERROR: desktop image is not an ELF file\n");
        return -1;
    }

    /* The loader allocated these as EfiLoaderData, which pmm_init() treats
     * as free after ExitBootServices: keep them out of the allocator. */
    pmm_reserve_region(phys, end - phys);

    g_desktop_image = (const void *)(uintptr_t)phys;
    g_desktop_image_size = size;
    serial_puts("[BOOTIMG] Desktop image reserved: ");
    serial_puthex(phys);
    serial_puts(" - ");
    serial_puthex(end);
    serial_puts("\n");
    return 0;
}

int boot_desktop_image_get(const void **data, uint64_t *size) {
    if (!data || !size) return -1;
    if (!g_desktop_image || g_desktop_image_size == 0) return -1;
    *data = g_desktop_image;
    *size = g_desktop_image_size;
    return 0;
}
