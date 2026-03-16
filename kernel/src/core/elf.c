/*
 * NeolyxOS ELF Loader Implementation
 * 
 * Loads ELF64 executables into memory for process_exec.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/elf.h"
#include "mm/pmm.h"
#include "mm/kheap.h"
#include "fs/vfs.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Memory functions */
extern void *memcpy(void *dest, const void *src, uint64_t n);
extern void *memset(void *s, int c, uint64_t n);

/* Page size */
#define PAGE_SIZE 4096

/* Limits */
#ifndef UINT64_MAX
#define UINT64_MAX 0xFFFFFFFFFFFFFFFFULL
#endif

/* ============ Helpers ============ */

static void serial_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void elf_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ ELF Validation ============ */

int elf_validate(const void *data, uint64_t size) {
    if (!data || size < sizeof(elf64_ehdr_t)) {
        serial_puts("[ELF] Invalid: data too small\n");
        return -1;
    }
    
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)data;
    
    /* Check magic number */
    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC) {
        serial_puts("[ELF] Invalid: bad magic number\n");
        return -1;
    }
    
    /* Check class (must be 64-bit) */
    if (ehdr->e_ident[4] != ELFCLASS64) {
        serial_puts("[ELF] Invalid: not 64-bit\n");
        return -1;
    }
    
    /* Check endianness (must be little endian for x86_64) */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        serial_puts("[ELF] Invalid: not little-endian\n");
        return -1;
    }
    
    /* Check machine type */
    if (ehdr->e_machine != EM_X86_64) {
        serial_puts("[ELF] Invalid: not x86_64\n");
        return -1;
    }
    
    /* Check type (must be executable or shared) */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        serial_puts("[ELF] Invalid: not executable\n");
        return -1;
    }
    
    /* Check program headers exist */
    if (ehdr->e_phnum == 0) {
        serial_puts("[ELF] Invalid: no program headers\n");
        return -1;
    }
    
    /* Check program header size */
    if (ehdr->e_phoff + (ehdr->e_phnum * ehdr->e_phentsize) > size) {
        serial_puts("[ELF] Invalid: program headers out of bounds\n");
        return -1;
    }
    
    return 0;
}

/* ============ ELF Info ============ */

int elf_get_info(const void *data, elf_info_t *info) {
    if (!data || !info) return -1;
    
    memset(info, 0, sizeof(elf_info_t));
    
    if (elf_validate(data, 0x1000000) != 0) {  /* Assume max 16MB */
        return -1;
    }
    
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)data;
    info->entry_point = ehdr->e_entry;
    info->is_dynamic = (ehdr->e_type == ET_DYN);
    
    /* Find load address range */
    uint64_t min_addr = UINT64_MAX;
    uint64_t max_addr = 0;
    
    const elf64_phdr_t *phdr = (const elf64_phdr_t *)((uint8_t *)data + ehdr->e_phoff);
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            if (phdr[i].p_vaddr < min_addr) {
                min_addr = phdr[i].p_vaddr;
            }
            uint64_t end = phdr[i].p_vaddr + phdr[i].p_memsz;
            if (end > max_addr) {
                max_addr = end;
            }
        } else if (phdr[i].p_type == PT_INTERP) {
            /* Copy interpreter path */
            const char *interp = (const char *)((uint8_t *)data + phdr[i].p_offset);
            elf_strcpy(info->interp, interp, 256);
            info->is_dynamic = 1;
        }
    }
    
    info->base_addr = min_addr;
    info->top_addr = max_addr;
    info->brk = (max_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    info->is_valid = 1;
    
    return 0;
}

/* ============ ELF Loading ============ */

int elf_load(const void *data, elf_info_t *info) {
    if (!data || !info) return -1;
    
    /* Get info first */
    if (elf_get_info(data, info) != 0) {
        serial_puts("[ELF] elf_get_info FAILED!\n");
        return -1;
    }
    
    serial_puts("[ELF] Loading executable...\n");
    serial_puts("[ELF] Entry point: ");
    serial_hex64(info->entry_point);
    serial_puts("\n");
    
    const elf64_ehdr_t *ehdr = (const elf64_ehdr_t *)data;
    const elf64_phdr_t *phdr = (const elf64_phdr_t *)((uint8_t *)data + ehdr->e_phoff);
    
    serial_puts("[ELF] Program headers: ");
    serial_hex64(ehdr->e_phnum);
    serial_puts(", phoff: ");
    serial_hex64(ehdr->e_phoff);
    serial_puts("\n");
    
    /* Load each PT_LOAD segment */
    int loaded_count = 0;
    for (int i = 0; i < ehdr->e_phnum; i++) {
        serial_puts("[ELF] Segment ");
        serial_hex64(i);
        serial_puts(": type=");
        serial_hex64(phdr[i].p_type);
        serial_puts(", vaddr=");
        serial_hex64(phdr[i].p_vaddr);
        serial_puts("\n");
        
        if (phdr[i].p_type != PT_LOAD) continue;
        
        loaded_count++;
        uint64_t vaddr = phdr[i].p_vaddr;
        uint64_t filesz = phdr[i].p_filesz;
        uint64_t memsz = phdr[i].p_memsz;
        uint64_t offset = phdr[i].p_offset;
        
        serial_puts("[ELF] Loading segment at ");
        serial_hex64(vaddr);
        serial_puts(" (");
        serial_hex64(memsz);
        serial_puts(" bytes)\n");
        
        /* Calculate page-aligned bounds */
        uint64_t vaddr_aligned = ALIGN_DOWN(vaddr);
        uint64_t end_addr = vaddr + memsz;
        uint64_t end_aligned = ALIGN_UP(end_addr);
        uint64_t num_pages = (end_aligned - vaddr_aligned) / PAGE_SIZE;
        
        /* Validate userspace address range (must be in designated userspace region)
         * NeolyxOS userspace region: 0x1000000 - 0x10000000 (16MB - 256MB)
         * This prevents userspace from loading at kernel addresses */
        if (vaddr < 0x1000000 || end_addr > 0x10000000) {
            serial_puts("[ELF] ERROR: segment outside userspace range\n");
            return -1;
        }
        
        /* Reserve these physical pages with PMM (pages are identity-mapped)
         * This marks them as "in use" so no other allocation can use them.
         * For proper security, we'd allocate fresh pages and map to vaddr,
         * but NeolyxOS uses identity mapping for simplicity. */
        pmm_reserve_region(vaddr_aligned, num_pages * PAGE_SIZE);
        
        /* Copy file contents to memory at vaddr */
        const uint8_t *src = (const uint8_t *)data + offset;
        uint8_t *dst = (uint8_t *)vaddr;
        
        serial_puts("[ELF] Copying ");
        serial_hex64(filesz);
        serial_puts(" bytes to 0x");
        serial_hex64((uint64_t)dst);
        serial_puts("...\n");
        
        /* Use memcpy for efficiency */
        memcpy(dst, src, filesz);
        
        /* Verify first bytes were copied */
        serial_puts("[ELF] First 4 bytes at dest: ");
        serial_hex64(*(uint32_t*)dst);
        serial_puts("\n");
        
        /* Zero BSS region (memsz > filesz) - security requirement */
        if (memsz > filesz) {
            memset(dst + filesz, 0, memsz - filesz);
        }
        
        serial_puts("[ELF] Segment loaded, ");
        serial_hex64(num_pages);
        serial_puts(" pages reserved\n");
        
        /* Update base/top address tracking */
        if (vaddr < info->base_addr || info->base_addr == 0) {
            info->base_addr = vaddr;
        }
        if (end_addr > info->top_addr) {
            info->top_addr = end_addr;
        }
    }
    
    /* Set up program break */
    info->brk = (info->top_addr + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
    
    serial_puts("[ELF] Load complete\n");
    serial_puts("[ELF] Adjusted entry: ");
    serial_hex64(info->entry_point);
    serial_puts("\n");
    
    return 0;
}

/* ============ File Loading ============ */

int elf_load_file(const char *path, elf_info_t *info) {
    if (!path || !info) return -1;
    
    serial_puts("[ELF] Loading file: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Open file via VFS */
    serial_puts("[ELF] DEBUG: Calling vfs_open...\n");
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    serial_puts("[ELF] DEBUG: vfs_open returned\n");
    if (!file) {
        serial_puts("[ELF] Failed to open file\n");
        return -1;
    }
    
    /* Get file size */
    serial_puts("[ELF] DEBUG: Getting file size...\n");
    uint64_t size = file->node ? file->node->size : 0;
    serial_puts("[ELF] DEBUG: File size obtained\n");
    if (size < sizeof(elf64_ehdr_t)) {
        serial_puts("[ELF] File too small\n");
        vfs_close(file);
        return -1;
    }
    
    /* Allocate buffer for file */
    serial_puts("[ELF] DEBUG: Calling kmalloc...\n");
    void *buffer = kmalloc(size);
    serial_puts("[ELF] DEBUG: kmalloc returned\n");
    if (!buffer) {
        serial_puts("[ELF] Failed to allocate buffer\n");
        vfs_close(file);
        return -1;
    }
    
    /* Read file */
    serial_puts("[ELF] DEBUG: Calling vfs_read...\n");
    int64_t bytes_read = vfs_read(file, buffer, size);
    serial_puts("[ELF] DEBUG: vfs_read returned\n");
    if (bytes_read != (int64_t)size) {
        serial_puts("[ELF] Failed to read file\n");
        kfree(buffer);
        vfs_close(file);
        return -1;
    }
    
    serial_puts("[ELF] DEBUG: Calling vfs_close...\n");
    vfs_close(file);
    serial_puts("[ELF] DEBUG: vfs_close returned\n");
    
    /* Load ELF from buffer */
    serial_puts("[ELF] DEBUG: Calling elf_load...\n");
    int result = elf_load(buffer, info);
    serial_puts("[ELF] DEBUG: elf_load returned\n");
    
    /* Free buffer (segments already copied) */
    kfree(buffer);
    
    return result;
}
