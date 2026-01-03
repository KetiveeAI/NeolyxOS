/*
 * NeolyxOS Device Tree (FDT) Parser Implementation
 * 
 * Parses DTB passed by firmware (UEFI/U-Boot)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "arch/aarch64/fdt.h"
#include <stdint.h>

/* External serial debug */
extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ Helpers ============ */

/* Big-endian to native */
static inline uint32_t fdt32_to_cpu(uint32_t val) {
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >> 8) |
           ((val & 0x0000FF00) << 8) |
           ((val & 0x000000FF) << 24);
}

static inline uint64_t fdt64_to_cpu(uint64_t val) {
    return ((uint64_t)fdt32_to_cpu(val & 0xFFFFFFFF) << 32) |
           fdt32_to_cpu(val >> 32);
}

static int nx_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static int nx_strncmp(const char *a, const char *b, int n) {
    while (n-- > 0 && *a && *b && *a == *b) { a++; b++; }
    return (n < 0) ? 0 : (*a - *b);
}

static void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static int nx_strlen(const char *s) {
    int len = 0;
    while (*s++) len++;
    return len;
}

static void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 0;
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

/* ============ FDT Access ============ */

int fdt_validate(void *fdt_addr) {
    if (!fdt_addr) return -1;
    
    fdt_header_t *hdr = (fdt_header_t*)fdt_addr;
    uint32_t magic = fdt32_to_cpu(hdr->magic);
    
    if (magic != FDT_MAGIC) {
        serial_puts("[FDT] Invalid magic: ");
        serial_hex64(magic);
        serial_puts("\n");
        return -2;
    }
    
    uint32_t version = fdt32_to_cpu(hdr->version);
    if (version < 16) {
        serial_puts("[FDT] Unsupported version\n");
        return -3;
    }
    
    return 0;
}

uint32_t fdt_totalsize(void *fdt_addr) {
    fdt_header_t *hdr = (fdt_header_t*)fdt_addr;
    return fdt32_to_cpu(hdr->totalsize);
}

/* Get string from strings block */
static const char *fdt_get_string(void *fdt_addr, uint32_t offset) {
    fdt_header_t *hdr = (fdt_header_t*)fdt_addr;
    uint32_t strings_off = fdt32_to_cpu(hdr->off_dt_strings);
    return (const char*)((uint8_t*)fdt_addr + strings_off + offset);
}

/* ============ Node Traversal ============ */

int fdt_find_node(void *fdt_addr, const char *path) {
    fdt_header_t *hdr = (fdt_header_t*)fdt_addr;
    uint32_t struct_off = fdt32_to_cpu(hdr->off_dt_struct);
    uint8_t *p = (uint8_t*)fdt_addr + struct_off;
    
    int depth = 0;
    int target_depth = 0;
    const char *target = path;
    int matched = 0;
    
    /* Skip leading / */
    if (*path == '/') {
        path++;
        if (*path == '\0') {
            /* Root node, return first node offset */
            return struct_off;
        }
    }
    
    while (1) {
        uint32_t token = fdt32_to_cpu(*(uint32_t*)p);
        p += 4;
        
        switch (token) {
            case FDT_BEGIN_NODE: {
                const char *name = (const char*)p;
                int namelen = nx_strlen(name);
                
                /* Align to 4 bytes */
                p += namelen + 1;
                p = (uint8_t*)(((uintptr_t)p + 3) & ~3);
                
                depth++;
                
                /* Check if this matches current path component */
                if (depth == target_depth + 1) {
                    const char *comp = path;
                    int complen = 0;
                    while (comp[complen] && comp[complen] != '/') complen++;
                    
                    if (nx_strncmp(name, comp, complen) == 0) {
                        path += complen;
                        if (*path == '/') path++;
                        target_depth = depth;
                        
                        if (*path == '\0') {
                            /* Found it! Return offset */
                            return (p - (uint8_t*)fdt_addr);
                        }
                    }
                }
                break;
            }
            
            case FDT_END_NODE:
                depth--;
                if (depth < 0) return -1;
                break;
                
            case FDT_PROP: {
                uint32_t len = fdt32_to_cpu(*(uint32_t*)p);
                p += 4;
                p += 4;  /* nameoff */
                p += len;
                p = (uint8_t*)(((uintptr_t)p + 3) & ~3);
                break;
            }
            
            case FDT_NOP:
                break;
                
            case FDT_END:
                return -1;  /* Not found */
                
            default:
                return -1;  /* Invalid token */
        }
    }
}

const void *fdt_get_property(void *fdt_addr, int node_offset, 
                             const char *name, int *len) {
    uint8_t *p = (uint8_t*)fdt_addr + node_offset;
    
    while (1) {
        uint32_t token = fdt32_to_cpu(*(uint32_t*)p);
        p += 4;
        
        switch (token) {
            case FDT_BEGIN_NODE: {
                /* Skip node name */
                int namelen = nx_strlen((const char*)p);
                p += namelen + 1;
                p = (uint8_t*)(((uintptr_t)p + 3) & ~3);
                break;
            }
            
            case FDT_PROP: {
                uint32_t proplen = fdt32_to_cpu(*(uint32_t*)p);
                p += 4;
                uint32_t nameoff = fdt32_to_cpu(*(uint32_t*)p);
                p += 4;
                
                const char *propname = fdt_get_string(fdt_addr, nameoff);
                
                if (nx_strcmp(propname, name) == 0) {
                    if (len) *len = proplen;
                    return p;
                }
                
                p += proplen;
                p = (uint8_t*)(((uintptr_t)p + 3) & ~3);
                break;
            }
            
            case FDT_END_NODE:
            case FDT_END:
                return NULL;
                
            case FDT_NOP:
                break;
                
            default:
                return NULL;
        }
    }
}

/* ============ Parse Functions ============ */

static void parse_memory(void *fdt_addr, fdt_info_t *info) {
    int off = fdt_find_node(fdt_addr, "/memory");
    if (off < 0) return;
    
    int len;
    const uint8_t *reg = fdt_get_property(fdt_addr, off, "reg", &len);
    if (!reg) return;
    
    /* Parse address/size pairs */
    int cells = len / 8;  /* Assuming #address-cells=2, #size-cells=2 */
    for (int i = 0; i < cells && info->mem_region_count < FDT_MAX_MEM_REGIONS; i += 2) {
        fdt_mem_region_t *region = &info->mem_regions[info->mem_region_count];
        region->base = fdt64_to_cpu(*(uint64_t*)(reg + i * 8));
        region->size = fdt64_to_cpu(*(uint64_t*)(reg + (i + 1) * 8));
        info->total_memory += region->size;
        info->mem_region_count++;
    }
}

static void parse_cpus(void *fdt_addr, fdt_info_t *info) {
    /* Simplified: just count CPUs */
    int off = fdt_find_node(fdt_addr, "/cpus");
    if (off < 0) return;
    
    /* Would need to iterate children */
    info->cpu_count = 1;  /* Default */
}

static void parse_chosen(void *fdt_addr, fdt_info_t *info) {
    int off = fdt_find_node(fdt_addr, "/chosen");
    if (off < 0) return;
    
    int len;
    const char *bootargs = fdt_get_property(fdt_addr, off, "bootargs", &len);
    if (bootargs) {
        nx_strcpy(info->bootargs, bootargs, 256);
    }
}

static void parse_uart(void *fdt_addr, fdt_info_t *info) {
    /* Try common UART compatible strings */
    /* For QEMU virt, it's at 0x09000000 */
    info->uart_base = 0x09000000;  /* Default for QEMU virt */
    info->uart_irq = 33;
}

static void parse_gic(void *fdt_addr, fdt_info_t *info) {
    /* GIC defaults for QEMU virt */
    info->gic_dist_base = 0x08000000;
    info->gic_cpu_base = 0x08010000;
    info->gic_redist_base = 0x080A0000;
    info->gic_version = 3;
}

/* ============ Main Parse Function ============ */

int fdt_parse(void *fdt_addr, fdt_info_t *info) {
    if (!fdt_addr || !info) return -1;
    
    serial_puts("[FDT] Parsing device tree at ");
    serial_hex64((uint64_t)fdt_addr);
    serial_puts("\n");
    
    /* Validate */
    int ret = fdt_validate(fdt_addr);
    if (ret < 0) return ret;
    
    /* Clear output */
    nx_memset(info, 0, sizeof(*info));
    
    /* Parse sections */
    parse_memory(fdt_addr, info);
    parse_cpus(fdt_addr, info);
    parse_chosen(fdt_addr, info);
    parse_uart(fdt_addr, info);
    parse_gic(fdt_addr, info);
    
    /* Get model/compatible from root */
    int len;
    const char *model = fdt_get_property(fdt_addr, 0, "model", &len);
    if (model) nx_strcpy(info->model, model, 64);
    
    const char *compat = fdt_get_property(fdt_addr, 0, "compatible", &len);
    if (compat) nx_strcpy(info->compatible, compat, 64);
    
    info->valid = 1;
    
    serial_puts("[FDT] Parse complete\n");
    return 0;
}

void fdt_debug_print(const fdt_info_t *info) {
    if (!info || !info->valid) {
        serial_puts("[FDT] No valid info\n");
        return;
    }
    
    serial_puts("\n=== Device Tree Info ===\n");
    
    serial_puts("Model: ");
    serial_puts(info->model);
    serial_puts("\n");
    
    serial_puts("Compatible: ");
    serial_puts(info->compatible);
    serial_puts("\n");
    
    serial_puts("\nMemory regions: ");
    serial_dec(info->mem_region_count);
    serial_puts("\n");
    
    for (int i = 0; i < info->mem_region_count; i++) {
        serial_puts("  Region ");
        serial_dec(i);
        serial_puts(": ");
        serial_hex64(info->mem_regions[i].base);
        serial_puts(" - ");
        serial_hex64(info->mem_regions[i].base + info->mem_regions[i].size);
        serial_puts(" (");
        serial_dec(info->mem_regions[i].size / (1024 * 1024));
        serial_puts(" MB)\n");
    }
    
    serial_puts("\nTotal memory: ");
    serial_dec(info->total_memory / (1024 * 1024));
    serial_puts(" MB\n");
    
    serial_puts("\nCPUs: ");
    serial_dec(info->cpu_count);
    serial_puts("\n");
    
    serial_puts("\nUART: ");
    serial_hex64(info->uart_base);
    serial_puts(" IRQ ");
    serial_dec(info->uart_irq);
    serial_puts("\n");
    
    serial_puts("\nGIC v");
    serial_dec(info->gic_version);
    serial_puts(": dist=");
    serial_hex64(info->gic_dist_base);
    serial_puts("\n");
    
    if (info->bootargs[0]) {
        serial_puts("\nBoot args: ");
        serial_puts(info->bootargs);
        serial_puts("\n");
    }
    
    serial_puts("========================\n\n");
}
