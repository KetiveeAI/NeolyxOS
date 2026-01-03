/*
 * NeolyxOS NVMe Driver
 * 
 * Production-ready NVMe storage driver
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include "nvme.h"
#include "../serial.h"

/* ============ Controller Storage ============ */
#define MAX_NVME_CONTROLLERS 4

static nvme_controller_t controllers[MAX_NVME_CONTROLLERS];
static int controller_count = 0;

/* ============ Queue Memory ============ */
/* Aligned buffers for queues */
static nvme_sqe_t admin_sq_buffer[NVME_QUEUE_ENTRIES] __attribute__((aligned(4096)));
static nvme_cqe_t admin_cq_buffer[NVME_QUEUE_ENTRIES] __attribute__((aligned(4096)));
static nvme_sqe_t io_sq_buffer[NVME_QUEUE_ENTRIES] __attribute__((aligned(4096)));
static nvme_cqe_t io_cq_buffer[NVME_QUEUE_ENTRIES] __attribute__((aligned(4096)));
static uint8_t identify_buffer[4096] __attribute__((aligned(4096)));

/* ============ PRP List Support ============ */
/* 
 * NVMe Physical Region Page (PRP) handling:
 * - PRP1 only: transfer ≤ 4KB (single page)
 * - PRP1 + PRP2: transfer ≤ 8KB (two pages)
 * - PRP1 + PRP list: transfer > 8KB (PRP2 points to list of page addresses)
 * 
 * PRP list entries must be 8-byte aligned, list itself must be page-aligned.
 * One 4KB page can hold 512 PRP entries = 2MB max transfer.
 */
#define NVME_PAGE_SIZE     4096
#define NVME_MAX_TRANSFER  (512 * 1024)   /* 512KB max for now */
#define NVME_PRP_ENTRIES   (NVME_MAX_TRANSFER / NVME_PAGE_SIZE)

/* Multi-page DMA buffer - 128KB for I/O operations (32 pages) */
#define NVME_DATA_PAGES    32
static uint8_t nvme_data_buffer[NVME_DATA_PAGES * NVME_PAGE_SIZE] __attribute__((aligned(4096)));

/* PRP list page - holds up to 512 physical page addresses */
static uint64_t nvme_prp_list[512] __attribute__((aligned(4096)));

/* ============ Helper Functions ============ */

static inline void mmio_write32(volatile uint32_t *addr, uint32_t value) {
    *addr = value;
    __asm__ volatile("" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t *addr) {
    uint32_t value = *addr;
    __asm__ volatile("" ::: "memory");
    return value;
}

static inline void mmio_write64(volatile uint64_t *addr, uint64_t value) {
    volatile uint32_t *low = (volatile uint32_t*)addr;
    volatile uint32_t *high = (volatile uint32_t*)((uint8_t*)addr + 4);
    *low = (uint32_t)value;
    *high = (uint32_t)(value >> 32);
    __asm__ volatile("" ::: "memory");
}

static inline uint64_t mmio_read64(volatile uint64_t *addr) {
    volatile uint32_t *low = (volatile uint32_t*)addr;
    volatile uint32_t *high = (volatile uint32_t*)((uint8_t*)addr + 4);
    return (uint64_t)*low | ((uint64_t)*high << 32);
}

static void delay_us(uint32_t us) {
    for (volatile uint32_t i = 0; i < us * 100; i++) {
        __asm__ volatile("nop");
    }
}

/* ============ PRP Setup ============ */

/**
 * nvme_setup_prp - Set up PRP entries for a transfer
 * 
 * @cmd: Command to configure (sets prp1, prp2)
 * @buffer: Physical address of the data buffer (must be page-aligned)
 * @length: Transfer length in bytes
 * 
 * Rules:
 * - length <= 4096: PRP1 only
 * - length <= 8192: PRP1 + PRP2 (two pages)
 * - length > 8192:  PRP1 + PRP2 points to PRP list
 * 
 * Returns: 0 on success, -1 on error
 */
static int nvme_setup_prp(nvme_sqe_t *cmd, uint64_t buffer, uint32_t length) {
    if (!cmd || length == 0) return -1;
    
    /* Limit to our buffer size */
    if (length > NVME_DATA_PAGES * NVME_PAGE_SIZE) {
        length = NVME_DATA_PAGES * NVME_PAGE_SIZE;
    }
    
    /* PRP1 always points to first page */
    cmd->prp1 = buffer;
    
    if (length <= NVME_PAGE_SIZE) {
        /* Case 1: Single page - PRP1 only */
        cmd->prp2 = 0;
    } 
    else if (length <= 2 * NVME_PAGE_SIZE) {
        /* Case 2: Two pages - PRP1 + PRP2 */
        cmd->prp2 = buffer + NVME_PAGE_SIZE;
    } 
    else {
        /* Case 3: More than 2 pages - PRP1 + PRP list */
        /* PRP2 points to our pre-allocated PRP list page */
        cmd->prp2 = (uint64_t)(uintptr_t)nvme_prp_list;
        
        /* Fill PRP list with page addresses starting from page 2 */
        uint32_t pages = (length + NVME_PAGE_SIZE - 1) / NVME_PAGE_SIZE;
        if (pages > NVME_DATA_PAGES) pages = NVME_DATA_PAGES;
        
        for (uint32_t i = 1; i < pages; i++) {
            nvme_prp_list[i - 1] = buffer + (i * NVME_PAGE_SIZE);
        }
    }
    
    return 0;
}

/* ============ Interrupt Support ============ */

/* PCI Capability IDs */
#define PCI_CAP_MSI     0x05
#define PCI_CAP_MSIX    0x11

/* External IDT functions */
extern void idt_register_handler(uint8_t vector, void (*handler)(void*));
extern void pic_unmask_irq(uint8_t irq);
extern void pic_send_eoi(uint8_t irq);

/* Currently active controller for IRQ handler (single controller for now) */
static nvme_controller_t *irq_controller = 0;

/**
 * nvme_find_msix_cap - Find MSI-X capability in PCI config space
 * 
 * Returns offset of MSI-X capability, or 0 if not found
 */
static uint8_t nvme_find_msix_cap(pci_device_t *dev) {
    /* Check if capabilities list exists */
    uint16_t status = pci_read_config16(dev->bus, dev->device, dev->function, 0x06);
    if (!(status & 0x10)) {
        return 0;  /* No capabilities list */
    }
    
    /* Get capabilities pointer */
    uint8_t cap_ptr = pci_read_config8(dev->bus, dev->device, dev->function, 0x34);
    cap_ptr &= 0xFC;  /* Must be dword aligned */
    
    /* Walk capabilities list */
    int count = 0;
    while (cap_ptr && count < 48) {  /* Max 48 caps to prevent infinite loop */
        uint8_t cap_id = pci_read_config8(dev->bus, dev->device, dev->function, cap_ptr);
        
        if (cap_id == PCI_CAP_MSIX) {
            return cap_ptr;
        }
        
        cap_ptr = pci_read_config8(dev->bus, dev->device, dev->function, cap_ptr + 1);
        cap_ptr &= 0xFC;
        count++;
    }
    
    return 0;
}

/**
 * nvme_setup_msix - Configure MSI-X for a controller
 * 
 * Returns 0 on success, negative on error
 */
static int nvme_setup_msix(nvme_controller_t *ctrl) {
    if (!ctrl->msix_cap_offset) {
        serial_puts("[NVMe] MSI-X not available\r\n");
        return -1;
    }
    
    pci_device_t *dev = ctrl->pci_dev;
    uint8_t cap = ctrl->msix_cap_offset;
    
    /* Read Message Control register */
    uint16_t msg_ctrl = pci_read_config16(dev->bus, dev->device, dev->function, cap + 2);
    ctrl->msix_table_size = (msg_ctrl & 0x7FF) + 1;
    
    /* Get Table BIR and offset */
    uint32_t table_offset = pci_read_config32(dev->bus, dev->device, dev->function, cap + 4);
    uint8_t table_bir = table_offset & 0x7;
    table_offset &= 0xFFFFFFF8;
    
    if (table_bir >= 6 || dev->bars[table_bir].type != PCI_BAR_MEM) {
        serial_puts("[NVMe] Invalid MSI-X table BAR\r\n");
        return -2;
    }
    
    ctrl->msix_table = (volatile uint32_t*)(uintptr_t)(dev->bars[table_bir].base + table_offset);
    
    serial_puts("[NVMe] MSI-X: ");
    char buf[4];
    buf[0] = '0' + (ctrl->msix_table_size / 10);
    buf[1] = '0' + (ctrl->msix_table_size % 10);
    buf[2] = '\0';
    if (ctrl->msix_table_size < 10) { buf[0] = '0' + ctrl->msix_table_size; buf[1] = '\0'; }
    serial_puts(buf);
    serial_puts(" vectors available\r\n");
    
    /* For now, we'll use polling mode but record MSI-X capability */
    /* Full MSI-X setup requires APIC configuration which isn't available yet */
    ctrl->int_mode = NVME_INT_MODE_POLL;
    
    return 0;
}

/**
 * nvme_io_irq_handler - IRQ handler for I/O completion
 * 
 * Called when an NVMe I/O completion interrupt fires.
 * Sets the pending flag for the completion polling loop to pick up.
 */
static void nvme_io_irq_handler(void *ctx) {
    (void)ctx;
    
    if (irq_controller) {
        irq_controller->io_cq_pending = 1;
    }
    
    /* Send EOI (for legacy PIC mode) */
    /* IRQ line from PCI interrupt_line register */
    if (irq_controller && irq_controller->pci_dev) {
        uint8_t irq = irq_controller->pci_dev->interrupt_line;
        if (irq < 16) {
            pic_send_eoi(irq);
        }
    }
}

/**
 * nvme_setup_interrupts - Configure interrupt handling for controller
 * 
 * Attempts MSI-X first, falls back to legacy INTx or polling
 */
static int nvme_setup_interrupts(nvme_controller_t *ctrl) {
    ctrl->int_mode = NVME_INT_MODE_POLL;
    ctrl->io_cq_pending = 0;
    
    /* Try to find MSI-X capability */
    ctrl->msix_cap_offset = nvme_find_msix_cap(ctrl->pci_dev);
    
    if (ctrl->msix_cap_offset) {
        /* MSI-X found - set up for future use */
        nvme_setup_msix(ctrl);
        /* Note: Full MSI-X requires APIC, staying in poll mode for now */
    }
    
    /* Try legacy INTx if PIC is available */
    if (ctrl->pci_dev->interrupt_line && ctrl->pci_dev->interrupt_line < 16) {
        uint8_t irq = ctrl->pci_dev->interrupt_line;
        uint8_t vector = 32 + irq;  /* PIC remaps IRQ0-15 to vectors 32-47 */
        
        /* Register handler */
        idt_register_handler(vector, nvme_io_irq_handler);
        pic_unmask_irq(irq);
        
        ctrl->int_mode = NVME_INT_MODE_INTX;
        ctrl->irq_vector = vector;
        irq_controller = ctrl;  /* Set active controller for IRQ */
        
        serial_puts("[NVMe] Using legacy IRQ ");
        char irq_buf[4];
        irq_buf[0] = '0' + irq;
        irq_buf[1] = '\0';
        serial_puts(irq_buf);
        serial_puts("\r\n");
    } else {
        serial_puts("[NVMe] Using polling mode\r\n");
    }
    
    return 0;
}

/**
 * nvme_poll_completion - Check for completion with optional interrupt assist
 * 
 * Uses interrupt flag to avoid busy-waiting when interrupts are enabled.
 */
static int nvme_poll_completion(nvme_controller_t *ctrl, nvme_queue_t *queue, 
                                 uint16_t cmd_id, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    
    while (elapsed < timeout_ms) {
        /* Check for pending interrupt flag first */
        if (ctrl->int_mode != NVME_INT_MODE_POLL && ctrl->io_cq_pending) {
            ctrl->io_cq_pending = 0;  /* Clear flag */
        }
        
        nvme_cqe_t *cqe = &queue->cq[queue->cq_head];
        
        /* Check phase bit */
        if (((cqe->status & 1) == queue->cq_phase)) {
            if (cqe->command_id == cmd_id) {
                /* Command completed */
                uint16_t status = (cqe->status >> 1) & 0x7FF;
                
                /* Update head */
                queue->cq_head = (queue->cq_head + 1) % queue->entries;
                if (queue->cq_head == 0) {
                    queue->cq_phase ^= 1;
                }
                
                return status == 0 ? 0 : -1;
            }
        }
        
        delay_us(1000);
        elapsed++;
    }
    
    return -2; /* Timeout */
}

/* ============ Queue Management ============ */

static void nvme_ring_sq_doorbell(nvme_controller_t *ctrl, nvme_queue_t *queue, int qid) {
    uint32_t *doorbell = (uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_SQ0TDBL + 
                                     (qid * 2 * ctrl->doorbell_stride));
    mmio_write32(doorbell, queue->sq_tail);
}

static uint16_t nvme_submit_command(nvme_controller_t *ctrl, nvme_queue_t *queue, 
                                    int qid, nvme_sqe_t *cmd) {
    uint16_t tail = queue->sq_tail;
    
    /* Copy command to queue */
    queue->sq[tail] = *cmd;
    
    /* Update tail */
    tail = (tail + 1) % queue->entries;
    queue->sq_tail = tail;
    
    /* Ring doorbell */
    nvme_ring_sq_doorbell(ctrl, queue, qid);
    
    return cmd->command_id;
}

static int nvme_wait_completion(nvme_queue_t *queue, uint16_t cmd_id, uint32_t timeout_ms) {
    uint32_t elapsed = 0;
    
    while (elapsed < timeout_ms) {
        nvme_cqe_t *cqe = &queue->cq[queue->cq_head];
        
        /* Check phase bit */
        if (((cqe->status & 1) == queue->cq_phase)) {
            if (cqe->command_id == cmd_id) {
                /* Command completed */
                uint16_t status = (cqe->status >> 1) & 0x7FF;
                
                /* Update head */
                queue->cq_head = (queue->cq_head + 1) % queue->entries;
                if (queue->cq_head == 0) {
                    queue->cq_phase ^= 1;
                }
                
                return status == 0 ? 0 : -1;
            }
        }
        
        delay_us(1000);
        elapsed++;
    }
    
    return -2; /* Timeout */
}

/* ============ Controller Initialization ============ */

static int nvme_disable_controller(nvme_controller_t *ctrl) {
    uint32_t cc = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_CC));
    cc &= ~NVME_CC_EN;
    mmio_write32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_CC), cc);
    
    /* Wait for not ready */
    for (int i = 0; i < 1000; i++) {
        uint32_t csts = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_CSTS));
        if (!(csts & NVME_CSTS_RDY)) {
            return 0;
        }
        delay_us(1000);
    }
    
    return -1;
}

static int nvme_enable_controller(nvme_controller_t *ctrl) {
    uint32_t cc = NVME_CC_EN | NVME_CC_CSS_NVM | NVME_CC_MPS_4K |
                  NVME_CC_IOSQES | NVME_CC_IOCQES;
    
    mmio_write32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_CC), cc);
    
    /* Wait for ready */
    for (int i = 0; i < 10000; i++) {
        uint32_t csts = mmio_read32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_CSTS));
        if (csts & NVME_CSTS_CFS) {
            return -2; /* Fatal error */
        }
        if (csts & NVME_CSTS_RDY) {
            return 0;
        }
        delay_us(1000);
    }
    
    return -1;
}

static int nvme_setup_admin_queue(nvme_controller_t *ctrl) {
    nvme_queue_t *aq = &ctrl->admin_queue;
    
    /* Set up queue structures */
    aq->sq = admin_sq_buffer;
    aq->cq = admin_cq_buffer;
    aq->entries = NVME_QUEUE_ENTRIES;
    aq->sq_head = 0;
    aq->sq_tail = 0;
    aq->cq_head = 0;
    aq->cq_phase = 1;
    
    /* Clear buffers */
    for (int i = 0; i < NVME_QUEUE_ENTRIES; i++) {
        admin_cq_buffer[i].status = 0;
    }
    
    /* Configure admin queue in controller */
    uint32_t aqa = (NVME_QUEUE_ENTRIES - 1) | ((NVME_QUEUE_ENTRIES - 1) << 16);
    mmio_write32((volatile uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_AQA), aqa);
    mmio_write64((volatile uint64_t*)((uint8_t*)ctrl->regs + NVME_REG_ASQ), 
                 (uint64_t)(uintptr_t)aq->sq);
    mmio_write64((volatile uint64_t*)((uint8_t*)ctrl->regs + NVME_REG_ACQ), 
                 (uint64_t)(uintptr_t)aq->cq);
    
    return 0;
}

/* Static command ID counter */
static uint16_t next_cmd_id = 2;

static uint16_t nvme_get_cmd_id(void) {
    uint16_t id = next_cmd_id++;
    if (next_cmd_id == 0) next_cmd_id = 2;  /* Avoid 0 and 1 (used by init) */
    return id;
}

static int nvme_identify_controller(nvme_controller_t *ctrl) {
    nvme_sqe_t cmd = {0};
    
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.command_id = 1;
    cmd.nsid = 0;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buffer;
    cmd.cdw10 = 1; /* CNS = 1 (identify controller) */
    
    nvme_submit_command(ctrl, &ctrl->admin_queue, 0, &cmd);
    
    if (nvme_wait_completion(&ctrl->admin_queue, 1, 5000) != 0) {
        serial_puts("[NVMe] Identify controller failed\r\n");
        return -1;
    }
    
    /* Parse identify data */
    /* Serial number at offset 4, 20 bytes */
    for (int i = 0; i < 20; i++) {
        ctrl->serial[i] = identify_buffer[4 + i];
    }
    
    /* Model number at offset 24, 40 bytes */
    for (int i = 0; i < 40; i++) {
        ctrl->model[i] = identify_buffer[24 + i];
    }
    
    /* Firmware at offset 64, 8 bytes */
    for (int i = 0; i < 8; i++) {
        ctrl->firmware[i] = identify_buffer[64 + i];
    }
    
    serial_puts("[NVMe] Model: ");
    serial_puts(ctrl->model);
    serial_puts("\r\n");
    
    return 0;
}

/* ============ I/O Queue Creation ============ */

static int nvme_create_io_cq(nvme_controller_t *ctrl, int qid) {
    nvme_sqe_t cmd = {0};
    uint16_t cmd_id = nvme_get_cmd_id();
    
    cmd.opcode = NVME_ADMIN_CREATE_CQ;
    cmd.command_id = cmd_id;
    cmd.prp1 = (uint64_t)(uintptr_t)io_cq_buffer;
    /* CDW10: QSIZE (bits 31:16) | QID (bits 15:0) */
    cmd.cdw10 = ((NVME_QUEUE_ENTRIES - 1) << 16) | qid;
    /* CDW11: IV (bits 31:16) | IEN (bit 1) | PC (bit 0) */
    cmd.cdw11 = 0x01;  /* Physically Contiguous, no interrupts */
    
    nvme_submit_command(ctrl, &ctrl->admin_queue, 0, &cmd);
    
    if (nvme_wait_completion(&ctrl->admin_queue, cmd_id, 5000) != 0) {
        serial_puts("[NVMe] Create I/O CQ failed\r\n");
        return -1;
    }
    
    serial_puts("[NVMe] I/O CQ created\r\n");
    return 0;
}

static int nvme_create_io_sq(nvme_controller_t *ctrl, int qid, int cqid) {
    nvme_sqe_t cmd = {0};
    uint16_t cmd_id = nvme_get_cmd_id();
    
    cmd.opcode = NVME_ADMIN_CREATE_SQ;
    cmd.command_id = cmd_id;
    cmd.prp1 = (uint64_t)(uintptr_t)io_sq_buffer;
    /* CDW10: QSIZE (bits 31:16) | QID (bits 15:0) */
    cmd.cdw10 = ((NVME_QUEUE_ENTRIES - 1) << 16) | qid;
    /* CDW11: CQID (bits 31:16) | QPRIO (bits 2:1) | PC (bit 0) */
    cmd.cdw11 = (cqid << 16) | 0x01;  /* Physically Contiguous, medium priority */
    
    nvme_submit_command(ctrl, &ctrl->admin_queue, 0, &cmd);
    
    if (nvme_wait_completion(&ctrl->admin_queue, cmd_id, 5000) != 0) {
        serial_puts("[NVMe] Create I/O SQ failed\r\n");
        return -1;
    }
    
    serial_puts("[NVMe] I/O SQ created\r\n");
    return 0;
}

static int nvme_setup_io_queue(nvme_controller_t *ctrl) {
    nvme_queue_t *ioq = &ctrl->io_queue;
    
    /* Initialize I/O queue structures */
    ioq->sq = io_sq_buffer;
    ioq->cq = io_cq_buffer;
    ioq->entries = NVME_QUEUE_ENTRIES;
    ioq->sq_head = 0;
    ioq->sq_tail = 0;
    ioq->cq_head = 0;
    ioq->cq_phase = 1;
    
    /* Clear buffers */
    for (int i = 0; i < NVME_QUEUE_ENTRIES; i++) {
        io_cq_buffer[i].status = 0;
    }
    
    /* Create I/O Completion Queue (QID 1) */
    if (nvme_create_io_cq(ctrl, 1) != 0) {
        return -1;
    }
    
    /* Create I/O Submission Queue (QID 1, associated with CQ 1) */
    if (nvme_create_io_sq(ctrl, 1, 1) != 0) {
        return -1;
    }
    
    /* Set up doorbells for I/O queue */
    ioq->sq_doorbell = (uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_SQ0TDBL + 
                                    (1 * 2 * ctrl->doorbell_stride));
    ioq->cq_doorbell = (uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_SQ0TDBL + 
                                    (1 * 2 * ctrl->doorbell_stride) + ctrl->doorbell_stride);
    
    serial_puts("[NVMe] I/O queue pair ready\r\n");
    return 0;
}

/* ============ Namespace Identification ============ */

static int nvme_identify_namespace(nvme_controller_t *ctrl, uint32_t nsid) {
    nvme_sqe_t cmd = {0};
    uint16_t cmd_id = nvme_get_cmd_id();
    
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.command_id = cmd_id;
    cmd.nsid = nsid;
    cmd.prp1 = (uint64_t)(uintptr_t)identify_buffer;
    cmd.cdw10 = 0;  /* CNS = 0 (identify namespace) */
    
    nvme_submit_command(ctrl, &ctrl->admin_queue, 0, &cmd);
    
    if (nvme_wait_completion(&ctrl->admin_queue, cmd_id, 5000) != 0) {
        return -1;
    }
    
    /* Parse namespace data */
    nvme_namespace_t *ns = &ctrl->namespaces[nsid - 1];
    ns->nsid = nsid;
    
    /* NSZE (Number of Logical Blocks) at offset 0, 8 bytes */
    ns->blocks = *(uint64_t*)&identify_buffer[0];
    
    /* FLBAS (Formatted LBA Size) at offset 26, 1 byte */
    uint8_t flbas = identify_buffer[26] & 0x0F;
    
    /* LBA Format at offset 128 + (flbas * 4), get LBADS (bits 23:16) */
    uint32_t lba_format = *(uint32_t*)&identify_buffer[128 + (flbas * 4)];
    uint8_t lbads = (lba_format >> 16) & 0xFF;
    ns->block_size = 1 << lbads;
    
    ns->active = (ns->blocks > 0) ? 1 : 0;
    
    if (ns->active) {
        serial_puts("[NVMe] NS");
        char buf[4];
        buf[0] = '0' + nsid;
        buf[1] = '\0';
        serial_puts(buf);
        serial_puts(": ");
        
        /* Print capacity in MB */
        uint64_t capacity_mb = (ns->blocks * ns->block_size) / (1024 * 1024);
        char cap_buf[16];
        int idx = 0;
        uint64_t temp = capacity_mb;
        if (temp == 0) {
            cap_buf[idx++] = '0';
        } else {
            char rev[16];
            int rev_idx = 0;
            while (temp > 0) {
                rev[rev_idx++] = '0' + (temp % 10);
                temp /= 10;
            }
            while (rev_idx > 0) {
                cap_buf[idx++] = rev[--rev_idx];
            }
        }
        cap_buf[idx] = '\0';
        serial_puts(cap_buf);
        serial_puts(" MB\r\n");
    }
    
    return 0;
}

static int nvme_identify_namespaces(nvme_controller_t *ctrl) {
    ctrl->namespace_count = 0;
    
    /* Try to identify namespaces 1 through NVME_MAX_NAMESPACES */
    for (uint32_t nsid = 1; nsid <= NVME_MAX_NAMESPACES; nsid++) {
        if (nvme_identify_namespace(ctrl, nsid) == 0) {
            if (ctrl->namespaces[nsid - 1].active) {
                ctrl->namespace_count = nsid;
            }
        } else {
            break;  /* No more namespaces */
        }
    }
    
    if (ctrl->namespace_count == 0) {
        serial_puts("[NVMe] No active namespaces found\r\n");
        return -1;
    }
    
    return 0;
}

/* ============ Ring CQ Doorbell ============ */

static void nvme_ring_cq_doorbell(nvme_controller_t *ctrl, int qid) {
    nvme_queue_t *queue = (qid == 0) ? &ctrl->admin_queue : &ctrl->io_queue;
    uint32_t *doorbell = (uint32_t*)((uint8_t*)ctrl->regs + NVME_REG_SQ0TDBL + 
                                      (qid * 2 * ctrl->doorbell_stride) + ctrl->doorbell_stride);
    mmio_write32(doorbell, queue->cq_head);
}

/* ============ Per-Core Queue Support (Phase 4) ============ */

/**
 * get_current_cpu_id - Get current processor ID
 * 
 * Uses CPUID or APIC ID. Returns 0 for single-core or unknown.
 */
static uint8_t get_current_cpu_id(void) {
    /* Read LAPIC ID from APIC base (if APIC is enabled) */
    /* For now, return 0 - proper implementation needs APIC setup */
    /* Future: read from APIC_ID register at 0xFEE00020 */
    return 0;
}

/**
 * nvme_get_cpu_queue - Get I/O queue for current CPU
 */
nvme_io_queue_ctx_t *nvme_get_cpu_queue(nvme_controller_t *ctrl) {
    if (!ctrl || !ctrl->queue_per_core || ctrl->num_io_queues == 0) {
        return 0;  /* Per-core queues not enabled */
    }
    
    uint8_t cpu_id = get_current_cpu_id();
    
    /* Find queue assigned to this CPU */
    for (int i = 0; i < ctrl->num_io_queues; i++) {
        if (ctrl->io_queues[i].active && ctrl->io_queues[i].cpu_id == cpu_id) {
            return &ctrl->io_queues[i];
        }
    }
    
    /* Fallback to first active queue */
    for (int i = 0; i < ctrl->num_io_queues; i++) {
        if (ctrl->io_queues[i].active) {
            return &ctrl->io_queues[i];
        }
    }
    
    return 0;
}

/**
 * nvme_create_io_queue_pair - Create a single I/O queue pair
 * 
 * Used by per-core queue setup to create queue for each CPU.
 */
static int nvme_create_io_queue_pair(nvme_controller_t *ctrl, int qid, 
                                      nvme_io_queue_ctx_t *ctx) {
    if (!ctrl || !ctx || qid < 1 || qid >= NVME_MAX_IO_QUEUES) {
        return -1;
    }
    
    /* Note: This requires separate SQ/CQ buffers per queue */
    /* For full implementation, need to allocate queue memory dynamically */
    /* For now, document the design - actual allocation needs PMM integration */
    
    ctx->qid = qid;
    ctx->active = 0;  /* Mark inactive until full implementation */
    ctx->pending = 0;
    ctx->commands_submitted = 0;
    ctx->commands_completed = 0;
    
    /* Would create CQ then SQ here with allocated buffers */
    /* nvme_create_io_cq(ctrl, qid) with per-queue buffer */
    /* nvme_create_io_sq(ctrl, qid, qid) with per-queue buffer */
    
    serial_puts("[NVMe] Per-core queue ");
    char buf[4];
    buf[0] = '0' + qid;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" designed (not yet active)\r\n");
    
    return 0;
}

/**
 * nvme_setup_per_core_queues - Create one I/O queue per CPU
 * 
 * Optimal setup for multi-core NVMe performance:
 * - One I/O SQ/CQ pair per CPU
 * - CPU-local submission (no locks needed)
 * - Per-queue MSI-X vector (if available)
 */
int nvme_setup_per_core_queues(nvme_controller_t *ctrl, int num_cpus) {
    if (!ctrl || !ctrl->initialized) {
        return -1;
    }
    
    serial_puts("[NVMe] Setting up per-core queues for ");
    char buf[4];
    buf[0] = '0' + num_cpus;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" CPUs\r\n");
    
    /* Limit to available MSI-X vectors and our max */
    int max_queues = NVME_MAX_IO_QUEUES;
    if (ctrl->msix_table_size > 0 && ctrl->msix_table_size < max_queues) {
        max_queues = ctrl->msix_table_size;
    }
    if (num_cpus < max_queues) {
        max_queues = num_cpus;
    }
    
    /* Queue 0 is admin, I/O queues start at 1 */
    ctrl->num_io_queues = 0;
    
    for (int cpu = 0; cpu < max_queues; cpu++) {
        int qid = cpu + 1;  /* Queue ID (admin is 0, I/O starts at 1) */
        nvme_io_queue_ctx_t *ctx = &ctrl->io_queues[cpu];
        
        ctx->cpu_id = cpu;
        ctx->irq_vector = (ctrl->int_mode == NVME_INT_MODE_MSIX) ? qid : ctrl->irq_vector;
        
        if (nvme_create_io_queue_pair(ctrl, qid, ctx) == 0) {
            ctrl->num_io_queues++;
        }
    }
    
    if (ctrl->num_io_queues > 0) {
        ctrl->queue_per_core = 1;
        serial_puts("[NVMe] Per-core queue design ready (");
        buf[0] = '0' + ctrl->num_io_queues;
        buf[1] = '\0';
        serial_puts(buf);
        serial_puts(" queues)\r\n");
    } else {
        ctrl->queue_per_core = 0;
        serial_puts("[NVMe] Per-core queues not available, using shared queue\r\n");
    }
    
    return ctrl->num_io_queues;
}

static int nvme_init_controller(pci_device_t *pci_dev) {
    if (controller_count >= MAX_NVME_CONTROLLERS) {
        return -1;
    }
    
    nvme_controller_t *ctrl = &controllers[controller_count];
    ctrl->pci_dev = pci_dev;
    ctrl->vendor_id = pci_dev->vendor_id;
    ctrl->device_id = pci_dev->device_id;
    
    /* Get BAR0 (MMIO registers) */
    if (pci_dev->bar_count == 0 || pci_dev->bars[0].type != PCI_BAR_MEM) {
        serial_puts("[NVMe] No valid BAR0\r\n");
        return -2;
    }
    
    ctrl->regs = (volatile uint32_t*)(uintptr_t)pci_dev->bars[0].base;
    
    /* Enable bus mastering and memory */
    pci_enable_bus_master(pci_dev);
    pci_enable_memory(pci_dev);
    
    /* Read capabilities */
    uint64_t cap = mmio_read64((volatile uint64_t*)ctrl->regs);
    ctrl->doorbell_stride = 4 << ((cap >> 32) & 0xF);
    
    serial_puts("[NVMe] Initializing controller...\r\n");
    
    /* Disable controller */
    if (nvme_disable_controller(ctrl) != 0) {
        serial_puts("[NVMe] Failed to disable controller\r\n");
        return -3;
    }
    
    /* Setup admin queues */
    if (nvme_setup_admin_queue(ctrl) != 0) {
        serial_puts("[NVMe] Failed to setup admin queue\r\n");
        return -4;
    }
    
    /* Enable controller */
    if (nvme_enable_controller(ctrl) != 0) {
        serial_puts("[NVMe] Failed to enable controller\r\n");
        return -5;
    }
    
    /* Identify controller */
    if (nvme_identify_controller(ctrl) != 0) {
        return -6;
    }
    
    /* Setup I/O queue pair */
    if (nvme_setup_io_queue(ctrl) != 0) {
        serial_puts("[NVMe] Failed to setup I/O queue\r\n");
        return -7;
    }
    
    /* Identify namespaces */
    nvme_identify_namespaces(ctrl);  /* Non-fatal if fails */
    
    /* Setup interrupt handling */
    nvme_setup_interrupts(ctrl);
    
    ctrl->initialized = 1;
    controller_count++;
    
    serial_puts("[NVMe] Controller initialized\r\n");
    return 0;
}

/* ============ Public API ============ */

int nvme_init(void) {
    serial_puts("[NVMe] Scanning for NVMe controllers...\r\n");
    
    controller_count = 0;
    
    /* Find NVMe devices via PCI */
    pci_device_t *dev = pci_find_class(PCI_CLASS_STORAGE, PCI_SUBCLASS_NVME);
    
    while (dev) {
        serial_puts("[NVMe] Found NVMe device\r\n");
        nvme_init_controller(dev);
        
        /* Find next */
        pci_device_t *next = dev->next;
        while (next) {
            if (next->class_code == PCI_CLASS_STORAGE && 
                next->subclass == PCI_SUBCLASS_NVME) {
                break;
            }
            next = next->next;
        }
        dev = next;
    }
    
    serial_puts("[NVMe] Found ");
    char buf[4];
    buf[0] = '0' + controller_count;
    buf[1] = '\0';
    serial_puts(buf);
    serial_puts(" controller(s)\r\n");
    
    return controller_count;
}

nvme_controller_t *nvme_get_controller(int index) {
    if (index < 0 || index >= controller_count) {
        return 0;
    }
    return &controllers[index];
}

int nvme_read(nvme_controller_t *ctrl, uint32_t nsid, 
              uint64_t lba, uint32_t count, void *buffer) {
    if (!ctrl || !ctrl->initialized || !buffer) {
        return -1;
    }
    
    if (nsid == 0 || nsid > ctrl->namespace_count) {
        return -2;
    }
    
    /* Get block size from namespace */
    nvme_namespace_t *ns = &ctrl->namespaces[nsid - 1];
    uint32_t block_size = ns->block_size > 0 ? ns->block_size : NVME_SECTOR_SIZE;
    
    /* Calculate transfer size and limit to our buffer */
    uint32_t max_blocks = (NVME_DATA_PAGES * NVME_PAGE_SIZE) / block_size;
    if (count > max_blocks) count = max_blocks;
    
    uint32_t bytes = count * block_size;
    
    nvme_sqe_t cmd = {0};
    uint16_t cmd_id = nvme_get_cmd_id();
    
    cmd.opcode = NVME_NVM_READ;
    cmd.command_id = cmd_id;
    cmd.nsid = nsid;
    
    /* Set up PRP entries based on transfer size */
    nvme_setup_prp(&cmd, (uint64_t)(uintptr_t)nvme_data_buffer, bytes);
    
    cmd.cdw10 = (uint32_t)lba;           /* Starting LBA (low 32 bits) */
    cmd.cdw11 = (uint32_t)(lba >> 32);   /* Starting LBA (high 32 bits) */
    cmd.cdw12 = count - 1;               /* Number of logical blocks (0-based) */
    
    /* Submit to I/O queue (QID 1) */
    nvme_submit_command(ctrl, &ctrl->io_queue, 1, &cmd);
    
    /* Wait for completion (uses interrupt-assisted polling if available) */
    if (nvme_poll_completion(ctrl, &ctrl->io_queue, cmd_id, 5000) != 0) {
        serial_puts("[NVMe] Read command failed\r\n");
        return -3;
    }
    
    /* Ring CQ doorbell to acknowledge completion */
    nvme_ring_cq_doorbell(ctrl, 1);
    
    /* Copy data from DMA buffer to user buffer */
    uint8_t *dst = (uint8_t*)buffer;
    for (uint32_t i = 0; i < bytes; i++) {
        dst[i] = nvme_data_buffer[i];
    }
    
    return (int)bytes;
}

int nvme_write(nvme_controller_t *ctrl, uint32_t nsid,
               uint64_t lba, uint32_t count, const void *buffer) {
    if (!ctrl || !ctrl->initialized || !buffer) {
        return -1;
    }
    
    if (nsid == 0 || nsid > ctrl->namespace_count) {
        return -2;
    }
    
    /* Get block size from namespace */
    nvme_namespace_t *ns = &ctrl->namespaces[nsid - 1];
    uint32_t block_size = ns->block_size > 0 ? ns->block_size : NVME_SECTOR_SIZE;
    
    /* Calculate transfer size and limit to our buffer */
    uint32_t max_blocks = (NVME_DATA_PAGES * NVME_PAGE_SIZE) / block_size;
    if (count > max_blocks) count = max_blocks;
    
    uint32_t bytes = count * block_size;
    
    /* Copy data from user buffer to DMA buffer */
    const uint8_t *src = (const uint8_t*)buffer;
    for (uint32_t i = 0; i < bytes; i++) {
        nvme_data_buffer[i] = src[i];
    }
    
    nvme_sqe_t cmd = {0};
    uint16_t cmd_id = nvme_get_cmd_id();
    
    cmd.opcode = NVME_NVM_WRITE;
    cmd.command_id = cmd_id;
    cmd.nsid = nsid;
    
    /* Set up PRP entries based on transfer size */
    nvme_setup_prp(&cmd, (uint64_t)(uintptr_t)nvme_data_buffer, bytes);
    
    cmd.cdw10 = (uint32_t)lba;           /* Starting LBA (low 32 bits) */
    cmd.cdw11 = (uint32_t)(lba >> 32);   /* Starting LBA (high 32 bits) */
    cmd.cdw12 = count - 1;               /* Number of logical blocks (0-based) */
    
    /* Submit to I/O queue (QID 1) */
    nvme_submit_command(ctrl, &ctrl->io_queue, 1, &cmd);
    
    /* Wait for completion (uses interrupt-assisted polling if available) */
    if (nvme_poll_completion(ctrl, &ctrl->io_queue, cmd_id, 5000) != 0) {
        serial_puts("[NVMe] Write command failed\r\n");
        return -3;
    }
    
    /* Ring CQ doorbell to acknowledge completion */
    nvme_ring_cq_doorbell(ctrl, 1);
    
    return (int)bytes;
}

uint64_t nvme_get_capacity(nvme_controller_t *ctrl, uint32_t nsid) {
    if (!ctrl || nsid == 0 || nsid > ctrl->namespace_count) {
        return 0;
    }
    
    nvme_namespace_t *ns = &ctrl->namespaces[nsid - 1];
    return ns->blocks * ns->block_size;
}

int nvme_get_controller_count(void) {
    return controller_count;
}
