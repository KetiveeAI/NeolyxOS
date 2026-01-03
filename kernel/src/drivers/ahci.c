/*
 * NeolyxOS AHCI/SATA Driver Implementation
 * 
 * AHCI controller driver for SATA disk access.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/ahci.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ I/O Port Functions ============ */

static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* ============ PCI Access ============ */

#define PCI_CONFIG_ADDR 0xCF8
#define PCI_CONFIG_DATA 0xCFC

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1 << 31) | (bus << 16) | (slot << 11) | (func << 8) | (offset & 0xFC);
    outl(PCI_CONFIG_ADDR, addr);
    outl(PCI_CONFIG_DATA, val);
}

/* ============ AHCI State ============ */

static hba_mem_t *ahci_base = NULL;
static hba_port_t *ahci_ports[AHCI_MAX_PORTS];
static int ahci_initialized = 0;

/* Per-port command structures */
static hba_cmd_header_t *cmd_headers[AHCI_MAX_PORTS];
static hba_cmd_tbl_t *cmd_tables[AHCI_MAX_PORTS];

/* ============ Helpers ============ */

static void *ahci_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void *ahci_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

static void serial_hex(uint32_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ Port Detection ============ */

static int ahci_port_type(hba_port_t *port) {
    uint32_t ssts = port->ssts;
    uint8_t det = ssts & 0xF;
    uint8_t ipm = (ssts >> 8) & 0xF;
    
    if (det != 3) return 0;     /* Device not present */
    if (ipm != 1) return 0;     /* Not active */
    
    switch (port->sig) {
        case SATA_SIG_ATA:
            return 1;  /* SATA */
        case SATA_SIG_ATAPI:
            return 2;  /* ATAPI */
        default:
            return 0;
    }
}

static void ahci_port_rebase(int port_num) {
    hba_port_t *port = ahci_ports[port_num];
    
    serial_puts("[AHCI] Rebasing port ");
    serial_putc('0' + port_num);
    serial_puts("...\n");
    
    /* Stop command engine */
    port->cmd &= ~HBA_PORT_CMD_ST;
    port->cmd &= ~HBA_PORT_CMD_FRE;
    
    /* Wait for engine to stop with timeout */
    int timeout = 100000;
    while ((port->cmd & (HBA_PORT_CMD_FR | HBA_PORT_CMD_CR)) && timeout--) {
        __asm__ volatile("pause");
    }
    if (timeout <= 0) {
        serial_puts("[AHCI] Port stop timeout\n");
    }
    
    /* Allocate command list (1KB aligned, 1KB size) */
    uint64_t clb = pmm_alloc_page();
    if (!clb) {
        serial_puts("[AHCI] CLB alloc failed\n");
        return;
    }
    ahci_memset((void *)clb, 0, PAGE_SIZE);
    port->clb = (uint32_t)clb;
    port->clbu = clb >> 32;
    
    /* Allocate FIS buffer (256B aligned, 256B size) */
    uint64_t fb = pmm_alloc_page();
    if (!fb) {
        serial_puts("[AHCI] FB alloc failed\n");
        return;
    }
    ahci_memset((void *)fb, 0, PAGE_SIZE);
    port->fb = (uint32_t)fb;
    port->fbu = fb >> 32;
    
    /* Command headers */
    cmd_headers[port_num] = (hba_cmd_header_t *)clb;
    
    /* Allocate command table for slot 0 */
    uint64_t ctba = pmm_alloc_page();
    if (!ctba) {
        serial_puts("[AHCI] CTBA alloc failed\n");
        return;
    }
    ahci_memset((void *)ctba, 0, PAGE_SIZE);
    cmd_headers[port_num][0].ctba = (uint32_t)ctba;
    cmd_headers[port_num][0].ctbau = ctba >> 32;
    cmd_tables[port_num] = (hba_cmd_tbl_t *)ctba;
    
    /* Start command engine */
    timeout = 100000;
    while ((port->cmd & HBA_PORT_CMD_CR) && timeout--) {
        __asm__ volatile("pause");
    }
    port->cmd |= HBA_PORT_CMD_FRE;
    port->cmd |= HBA_PORT_CMD_ST;
    
    serial_puts("[AHCI] Port ");
    serial_putc('0' + port_num);
    serial_puts(" rebase complete\n");
}

/* ============ Command Execution ============ */

static int ahci_find_slot(hba_port_t *port) {
    uint32_t slots = port->sact | port->ci;
    for (int i = 0; i < 32; i++) {
        if (!(slots & (1 << i))) return i;
    }
    return -1;
}

static int ahci_issue_command(int port_num, uint64_t lba, uint32_t count, 
                              void *buffer, int write) {
    hba_port_t *port = ahci_ports[port_num];
    
    port->is = (uint32_t)-1;  /* Clear interrupts */
    
    int slot = ahci_find_slot(port);
    if (slot < 0) return -1;
    
    hba_cmd_header_t *hdr = &cmd_headers[port_num][slot];
    hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
    hdr->write = write ? 1 : 0;
    hdr->prdtl = 1;
    
    hba_cmd_tbl_t *tbl = cmd_tables[port_num];
    ahci_memset(tbl, 0, sizeof(hba_cmd_tbl_t));
    
    /* Setup PRDT */
    tbl->prdt[0].dba = (uint32_t)(uint64_t)buffer;
    tbl->prdt[0].dbau = (uint64_t)buffer >> 32;
    tbl->prdt[0].dbc = (count * AHCI_SECTOR_SIZE) - 1;
    tbl->prdt[0].interrupt = 1;
    
    /* Setup FIS */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    ahci_memset(fis, 0, sizeof(fis_reg_h2d_t));
    
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
    fis->device = 1 << 6;  /* LBA mode */
    
    fis->lba0 = (uint8_t)lba;
    fis->lba1 = (uint8_t)(lba >> 8);
    fis->lba2 = (uint8_t)(lba >> 16);
    fis->lba3 = (uint8_t)(lba >> 24);
    fis->lba4 = (uint8_t)(lba >> 32);
    fis->lba5 = (uint8_t)(lba >> 40);
    
    fis->count_low = count & 0xFF;
    fis->count_high = (count >> 8) & 0xFF;
    
    /* Wait for port ready */
    int timeout = 100000;
    while ((port->tfd & 0x88) && timeout--) {
        __asm__ volatile("pause");
    }
    if (timeout <= 0) return -1;
    
    /* Issue command */
    port->ci = 1 << slot;
    
    /* Wait for completion */
    timeout = 1000000;
    while (timeout--) {
        if (!(port->ci & (1 << slot))) break;
        if (port->is & (1 << 30)) return -1;  /* Error */
        __asm__ volatile("pause");
    }
    
    if (port->is & (1 << 30)) return -1;
    
    return 0;
}

/* ============ Public API ============ */

int ahci_init(void) {
    serial_puts("[AHCI] Scanning for controllers...\n");
    
    /* Scan PCI for AHCI controller (class 01, subclass 06) */
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            uint32_t vendor = pci_read(bus, slot, 0, 0);
            if ((vendor & 0xFFFF) == 0xFFFF) continue;
            
            uint32_t class_info = pci_read(bus, slot, 0, 8);
            uint8_t base_class = (class_info >> 24) & 0xFF;
            uint8_t sub_class = (class_info >> 16) & 0xFF;
            
            if (base_class == 0x01 && sub_class == 0x06) {
                /* Found AHCI controller */
                uint32_t bar5 = pci_read(bus, slot, 0, 0x24);
                ahci_base = (hba_mem_t *)(uint64_t)(bar5 & ~0xF);
                
                serial_puts("[AHCI] Found controller at ");
                serial_hex((uint32_t)(uint64_t)ahci_base);
                serial_puts("\n");
                
                /* Enable bus mastering */
                uint32_t cmd = pci_read(bus, slot, 0, 4);
                cmd |= (1 << 2) | (1 << 1);
                pci_write(bus, slot, 0, 4, cmd);
                
                goto found;
            }
        }
    }
    
    serial_puts("[AHCI] No controller found\n");
    return -1;
    
found:
    /* Initialize ports */
    uint32_t pi = ahci_base->pi;
    for (int i = 0; i < AHCI_MAX_PORTS; i++) {
        if (pi & (1 << i)) {
            ahci_ports[i] = (hba_port_t *)((uint64_t)ahci_base + 0x100 + i * 0x80);
            
            int type = ahci_port_type(ahci_ports[i]);
            if (type == 1) {
                serial_puts("[AHCI] Port ");
                serial_putc('0' + i);
                serial_puts(": SATA drive detected\n");
                ahci_port_rebase(i);
            } else if (type == 2) {
                serial_puts("[AHCI] Port ");
                serial_putc('0' + i);
                serial_puts(": ATAPI drive detected\n");
            }
        }
    }
    
    ahci_initialized = 1;
    serial_puts("[AHCI] Ready\n");
    return 0;
}

int ahci_probe_drives(ahci_drive_t *drives, int max) {
    if (!ahci_initialized) return 0;
    
    int count = 0;
    uint32_t pi = ahci_base->pi;
    
    for (int i = 0; i < AHCI_MAX_PORTS && count < max; i++) {
        if (!(pi & (1 << i))) continue;
        
        int type = ahci_port_type(ahci_ports[i]);
        if (type == 1) {
            drives[count].port = i;
            drives[count].type = type;
            drives[count].present = 1;
            
            /* Get drive info */
            ahci_identify(i, &drives[count]);
            count++;
        }
    }
    
    return count;
}

int ahci_read(int port, uint64_t lba, uint32_t count, void *buffer) {
    if (!ahci_initialized || port >= AHCI_MAX_PORTS) return -1;
    if (!ahci_ports[port]) return -1;
    
    return ahci_issue_command(port, lba, count, buffer, 0);
}

int ahci_write(int port, uint64_t lba, uint32_t count, const void *buffer) {
    if (!ahci_initialized || port >= AHCI_MAX_PORTS) return -1;
    if (!ahci_ports[port]) return -1;
    
    return ahci_issue_command(port, lba, count, (void *)buffer, 1);
}

int ahci_identify(int port, ahci_drive_t *info) {
    if (!ahci_initialized) return -1;
    if (!cmd_headers[port] || !cmd_tables[port]) {
        serial_puts("[AHCI] Port not initialized\n");
        return -1;
    }
    
    /* Use PMM for DMA buffer (physical address required) */
    uint64_t ident_phys = pmm_alloc_page();
    if (!ident_phys) {
        serial_puts("[AHCI] Failed to allocate identify buffer\n");
        return -1;
    }
    uint16_t *ident = (uint16_t *)ident_phys;
    ahci_memset(ident, 0, 512);
    
    hba_port_t *p = ahci_ports[port];
    
    /* Clear pending interrupts */
    p->is = (uint32_t)-1;
    
    hba_cmd_header_t *hdr = &cmd_headers[port][0];
    hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
    hdr->write = 0;
    hdr->prdtl = 1;
    
    hba_cmd_tbl_t *tbl = cmd_tables[port];
    ahci_memset(tbl, 0, sizeof(hba_cmd_tbl_t));
    
    /* Setup PRDT with physical address */
    tbl->prdt[0].dba = (uint32_t)ident_phys;
    tbl->prdt[0].dbau = (uint32_t)(ident_phys >> 32);
    tbl->prdt[0].dbc = 511;
    tbl->prdt[0].interrupt = 1;
    
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    ahci_memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_IDENTIFY;
    
    /* Issue command */
    p->ci = 1;
    
    /* Wait for completion with timeout */
    int timeout = 500000;
    while ((p->ci & 1) && timeout--) {
        __asm__ volatile("pause");
    }
    
    if (timeout <= 0) {
        serial_puts("[AHCI] IDENTIFY timeout\n");
        pmm_free_page(ident_phys);
        return -1;
    }
    
    /* Check for errors */
    if (p->is & (1 << 30)) {
        serial_puts("[AHCI] IDENTIFY error\n");
        pmm_free_page(ident_phys);
        return -1;
    }
    
    /* Parse identify data */
    info->sectors = *(uint64_t *)&ident[100];  /* LBA48 capacity */
    if (info->sectors == 0) {
        info->sectors = *(uint32_t *)&ident[60];  /* LBA28 capacity */
    }
    
    /* Debug: print sector count */
    serial_puts("[AHCI] Sectors: ");
    serial_hex((uint32_t)info->sectors);
    serial_puts("\n");
    
    /* Model string (words 27-46, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        info->model[i*2] = ident[27+i] >> 8;
        info->model[i*2+1] = ident[27+i] & 0xFF;
    }
    info->model[40] = '\0';
    
    /* Trim trailing spaces from model */
    for (int i = 39; i >= 0 && info->model[i] == ' '; i--) {
        info->model[i] = '\0';
    }
    
    /* Serial number (words 10-19, byte-swapped) */
    for (int i = 0; i < 10; i++) {
        info->serial[i*2] = ident[10+i] >> 8;
        info->serial[i*2+1] = ident[10+i] & 0xFF;
    }
    info->serial[20] = '\0';
    
    /* Debug: print model */
    serial_puts("[AHCI] Model: ");
    serial_puts(info->model);
    serial_puts("\n");
    
    pmm_free_page(ident_phys);
    return 0;
}

int ahci_flush(int port) {
    if (!ahci_initialized || port >= AHCI_MAX_PORTS) return -1;
    if (!ahci_ports[port]) return -1;
    if (!cmd_headers[port] || !cmd_tables[port]) return -1;
    
    hba_port_t *p = ahci_ports[port];
    
    /* Clear pending interrupts */
    p->is = (uint32_t)-1;
    
    int slot = ahci_find_slot(p);
    if (slot < 0) return -1;
    
    hba_cmd_header_t *hdr = &cmd_headers[port][slot];
    hdr->cfl = sizeof(fis_reg_h2d_t) / 4;
    hdr->write = 0;
    hdr->prdtl = 0;  /* No data transfer */
    
    hba_cmd_tbl_t *tbl = cmd_tables[port];
    ahci_memset(tbl, 0, sizeof(hba_cmd_tbl_t));
    
    /* Setup FIS for FLUSH CACHE EXTENDED command */
    fis_reg_h2d_t *fis = (fis_reg_h2d_t *)tbl->cfis;
    ahci_memset(fis, 0, sizeof(fis_reg_h2d_t));
    fis->fis_type = FIS_TYPE_REG_H2D;
    fis->c = 1;
    fis->command = ATA_CMD_FLUSH_CACHE_EX;  /* 0xEA - 48-bit flush */
    fis->device = 1 << 6;  /* LBA mode */
    
    /* Wait for port ready */
    int timeout = 100000;
    while ((p->tfd & 0x88) && timeout--) {
        __asm__ volatile("pause");
    }
    if (timeout <= 0) {
        serial_puts("[AHCI] Flush timeout waiting for port\n");
        return -1;
    }
    
    /* Issue command */
    p->ci = 1 << slot;
    
    /* Wait for completion - flush can take longer than normal commands */
    timeout = 5000000;  /* 5 second timeout */
    while (timeout--) {
        if (!(p->ci & (1 << slot))) break;
        if (p->is & (1 << 30)) {
            serial_puts("[AHCI] Flush command error\n");
            return -1;
        }
        __asm__ volatile("pause");
    }
    
    if (timeout <= 0) {
        serial_puts("[AHCI] Flush timeout\n");
        return -1;
    }
    
    return 0;
}
