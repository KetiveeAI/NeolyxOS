/*
 * NeolyxOS - Desktop image handed over by the bootloader
 *
 * The desktop shell is a userspace ELF stored on the boot volume as
 * \EFI\BOOT\desktop.elf. The UEFI loader reads it into memory below 4 GB
 * and outside the user window, then passes it in NeolyxBootInfo:
 *   reserved[1] = physical address, reserved[2] = size,
 *   reserved[3] = BOOT_DESKTOP_MAGIC.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_BOOT_IMAGE_H
#define NEOLYX_BOOT_IMAGE_H

#include <stdint.h>

#define BOOT_DESKTOP_MAGIC      0x4B534544ULL   /* "DESK", must match boot/main.c */
#define BOOT_DESKTOP_MAX_SIZE   (64ULL * 1024 * 1024)

/*
 * Validate the desktop image described by the boot info fields and reserve
 * its pages in the PMM. Call once, right after pmm_init(). The pages stay
 * reserved for the life of the system: the installer copies the image to
 * the target disk and the desktop may be restarted from it.
 * Returns 0 if a valid image is available, negative otherwise (logged).
 */
int boot_desktop_image_init(uint64_t phys, uint64_t size, uint64_t magic);

/*
 * Get the validated desktop image. Returns 0 and fills both outputs, or -1
 * if the bootloader did not provide a usable image.
 */
int boot_desktop_image_get(const void **data, uint64_t *size);

#endif /* NEOLYX_BOOT_IMAGE_H */
