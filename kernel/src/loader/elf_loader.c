/*
 * NeolyxOS ELF Loader Implementation
 * 
 * Loads ELF binaries into RAM
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "elf_loader.h"
#include "../drivers/serial.h"

/* ============ Memory Functions (from kernel) ============ */
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* ============ Memory Copy ============ */
static void mem_copy(void *dest, const void *src, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
}

static void mem_set(void *dest, uint8_t val, uint64_t n) {
    uint8_t *d = (uint8_t*)dest;
    while (n--) *d++ = val;
}

/* ============ ELF Validation ============ */

int elf_validate(const void *data, uint64_t size) {
    if (!data || size < sizeof(Elf64_Ehdr)) {
        return ELF_ERR_CORRUPT;
    }
    
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr*)data;
    
    /* Check magic */
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        serial_puts("[ELF] Invalid magic number\r\n");
        return ELF_ERR_NOT_ELF;
    }
    
    /* Check 64-bit */
    if (ehdr->e_ident[4] != ELF_CLASS_64) {
        serial_puts("[ELF] Not 64-bit ELF\r\n");
        return ELF_ERR_NOT_64BIT;
    }
    
    /* Check little-endian */
    if (ehdr->e_ident[5] != ELF_DATA_LE) {
        serial_puts("[ELF] Not little-endian\r\n");
        return ELF_ERR_NOT_EXEC;
    }
    
    /* Check executable or shared object */
    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        serial_puts("[ELF] Not executable\r\n");
        return ELF_ERR_NOT_EXEC;
    }
    
    /* Check x86-64 */
    if (ehdr->e_machine != ELF_MACHINE_X86_64) {
        serial_puts("[ELF] Wrong architecture\r\n");
        return ELF_ERR_WRONG_ARCH;
    }
    
    /* Check program header offset and count */
    if (ehdr->e_phoff == 0 || ehdr->e_phnum == 0) {
        serial_puts("[ELF] No program headers\r\n");
        return ELF_ERR_CORRUPT;
    }
    
    /* Check bounds */
    uint64_t phdr_end = ehdr->e_phoff + (uint64_t)ehdr->e_phnum * ehdr->e_phentsize;
    if (phdr_end > size) {
        serial_puts("[ELF] Program headers extend past file\r\n");
        return ELF_ERR_CORRUPT;
    }
    
    return ELF_SUCCESS;
}

/* ============ Get Entry Point ============ */

uint64_t elf_get_entry(const void *data) {
    if (!data) return 0;
    
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr*)data;
    
    /* Quick magic check */
    if (*(uint32_t*)ehdr->e_ident != ELF_MAGIC) {
        return 0;
    }
    
    return ehdr->e_entry;
}

/* ============ Get Segment ============ */

int elf_get_segment(const void *data, int index, Elf64_Phdr *out_phdr) {
    if (!data || !out_phdr || index < 0) {
        return ELF_ERR_CORRUPT;
    }
    
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr*)data;
    
    if (index >= ehdr->e_phnum) {
        return ELF_ERR_SEGMENT;
    }
    
    const Elf64_Phdr *phdr = (const Elf64_Phdr*)((uint8_t*)data + ehdr->e_phoff);
    *out_phdr = phdr[index];
    
    return ELF_SUCCESS;
}

/* ============ Load Single Segment ============ */

uint64_t elf_load_segment(const void *data, Elf64_Phdr *phdr, uint64_t dest) {
    if (!data || !phdr) return 0;
    
    /* Only load PT_LOAD segments */
    if (phdr->p_type != PT_LOAD) {
        return 0;
    }
    
    /* Determine destination address */
    uint64_t load_addr = dest ? dest : phdr->p_vaddr;
    
    /* Validate address is reasonable */
    if (load_addr < 0x100000 || load_addr > 0xFFFFFFFFFFFF) {
        serial_puts("[ELF] Invalid load address\r\n");
        return 0;
    }
    
    serial_puts("[ELF] Loading segment to 0x");
    /* Print load address in hex */
    char hex[17];
    uint64_t val = load_addr;
    for (int i = 15; i >= 0; i--) {
        hex[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    hex[16] = '\0';
    serial_puts(hex);
    serial_puts("\r\n");
    
    /* Copy file data to memory */
    if (phdr->p_filesz > 0) {
        const void *src = (uint8_t*)data + phdr->p_offset;
        mem_copy((void*)load_addr, src, phdr->p_filesz);
    }
    
    /* Zero remaining bytes (BSS) */
    if (phdr->p_memsz > phdr->p_filesz) {
        uint64_t bss_start = load_addr + phdr->p_filesz;
        uint64_t bss_size = phdr->p_memsz - phdr->p_filesz;
        mem_set((void*)bss_start, 0, bss_size);
    }
    
    return load_addr;
}

/* ============ Load Full ELF ============ */

int elf_load(const void *data, uint64_t size, loaded_image_t *image) {
    if (!data || !image) {
        return ELF_ERR_CORRUPT;
    }
    
    serial_puts("[ELF] Loading ELF binary...\r\n");
    
    /* Validate */
    int result = elf_validate(data, size);
    if (result != ELF_SUCCESS) {
        return result;
    }
    
    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr*)data;
    
    /* Initialize image */
    image->entry_point = ehdr->e_entry;
    image->base_address = 0xFFFFFFFFFFFFFFFF;
    image->image_size = 0;
    image->segment_count = 0;
    image->interp = 0;
    image->dynamic_section = 0;
    
    /* Count loadable segments */
    int load_count = 0;
    const Elf64_Phdr *phdr = (const Elf64_Phdr*)((uint8_t*)data + ehdr->e_phoff);
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            load_count++;
        }
    }
    
    if (load_count == 0) {
        serial_puts("[ELF] No loadable segments\r\n");
        return ELF_ERR_SEGMENT;
    }
    
    /* Allocate segment array */
    image->segments = (loaded_segment_t*)kmalloc(load_count * sizeof(loaded_segment_t));
    if (!image->segments) {
        serial_puts("[ELF] Failed to allocate segment array\r\n");
        return ELF_ERR_NO_MEMORY;
    }
    
    /* Load each segment */
    uint64_t max_addr = 0;
    int seg_idx = 0;
    
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t addr = elf_load_segment(data, (Elf64_Phdr*)&phdr[i], 0);
            if (addr == 0) {
                serial_puts("[ELF] Failed to load segment\r\n");
                kfree(image->segments);
                return ELF_ERR_SEGMENT;
            }
            
            image->segments[seg_idx].vaddr = phdr[i].p_vaddr;
            image->segments[seg_idx].paddr = addr;
            image->segments[seg_idx].size = phdr[i].p_memsz;
            image->segments[seg_idx].flags = phdr[i].p_flags;
            seg_idx++;
            
            /* Track bounds */
            if (phdr[i].p_vaddr < image->base_address) {
                image->base_address = phdr[i].p_vaddr;
            }
            if (phdr[i].p_vaddr + phdr[i].p_memsz > max_addr) {
                max_addr = phdr[i].p_vaddr + phdr[i].p_memsz;
            }
        }
        else if (phdr[i].p_type == PT_INTERP) {
            /* Dynamic linker path */
            image->interp = (char*)((uint8_t*)data + phdr[i].p_offset);
        }
        else if (phdr[i].p_type == PT_DYNAMIC) {
            image->dynamic_section = phdr[i].p_vaddr;
        }
    }
    
    image->segment_count = seg_idx;
    image->image_size = max_addr - image->base_address;
    
    serial_puts("[ELF] Loaded ");
    char buf[8];
    buf[0] = '0' + seg_idx;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" segments, entry at 0x");
    
    /* Print entry in hex */
    char hex[17];
    uint64_t val = image->entry_point;
    for (int i = 15; i >= 0; i--) {
        hex[i] = "0123456789ABCDEF"[val & 0xF];
        val >>= 4;
    }
    hex[16] = '\0';
    serial_puts(hex);
    serial_puts("\r\n");
    
    return ELF_SUCCESS;
}

/* ============ Unload ELF ============ */

void elf_unload(loaded_image_t *image) {
    if (!image) return;
    
    if (image->segments) {
        kfree(image->segments);
        image->segments = 0;
    }
    
    image->entry_point = 0;
    image->base_address = 0;
    image->image_size = 0;
    image->segment_count = 0;
}
