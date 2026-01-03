/*
 * NeolyxOS NVMe Driver
 * 
 * Production-ready NVMe storage driver with:
 * - Controller initialization
 * - Admin and I/O queue management
 * - Read/write commands
 * - Namespace handling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include "../pci/pci.h"

/* ============ NVMe Constants ============ */
#define NVME_QUEUE_ENTRIES    64
#define NVME_MAX_NAMESPACES   16
#define NVME_SECTOR_SIZE      512

/* ============ NVMe Registers ============ */
#define NVME_REG_CAP        0x00   /* Controller Capabilities */
#define NVME_REG_VS         0x08   /* Version */
#define NVME_REG_INTMS      0x0C   /* Interrupt Mask Set */
#define NVME_REG_INTMC      0x10   /* Interrupt Mask Clear */
#define NVME_REG_CC         0x14   /* Controller Configuration */
#define NVME_REG_CSTS       0x1C   /* Controller Status */
#define NVME_REG_NSSR       0x20   /* NVM Subsystem Reset */
#define NVME_REG_AQA        0x24   /* Admin Queue Attributes */
#define NVME_REG_ASQ        0x28   /* Admin Submission Queue Base */
#define NVME_REG_ACQ        0x30   /* Admin Completion Queue Base */
#define NVME_REG_SQ0TDBL    0x1000 /* Submission Queue 0 Tail Doorbell */

/* ============ Controller Configuration ============ */
#define NVME_CC_EN          (1 << 0)   /* Enable */
#define NVME_CC_CSS_NVM     (0 << 4)   /* NVM Command Set */
#define NVME_CC_MPS_4K      (0 << 7)   /* Memory Page Size 4KB */
#define NVME_CC_AMS_RR      (0 << 11)  /* Round Robin */
#define NVME_CC_SHN_NONE    (0 << 14)  /* No shutdown */
#define NVME_CC_IOSQES      (6 << 16)  /* I/O SQ Entry Size 64B */
#define NVME_CC_IOCQES      (4 << 20)  /* I/O CQ Entry Size 16B */

/* ============ Controller Status ============ */
#define NVME_CSTS_RDY       (1 << 0)   /* Ready */
#define NVME_CSTS_CFS       (1 << 1)   /* Controller Fatal Status */
#define NVME_CSTS_SHST_MASK (3 << 2)   /* Shutdown Status */

/* ============ NVMe Commands ============ */
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A

#define NVME_NVM_READ           0x02
#define NVME_NVM_WRITE          0x01
#define NVME_NVM_FLUSH          0x00

/* ============ Submission Queue Entry (64 bytes) ============ */
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t command_id;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} __attribute__((packed)) nvme_sqe_t;

/* ============ Completion Queue Entry (16 bytes) ============ */
typedef struct {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} __attribute__((packed)) nvme_cqe_t;

/* ============ Queue ============ */
typedef struct {
    nvme_sqe_t *sq;
    nvme_cqe_t *cq;
    uint16_t sq_head;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint16_t cq_phase;
    uint16_t entries;
    uint32_t *sq_doorbell;
    uint32_t *cq_doorbell;
} nvme_queue_t;

/* ============ Namespace ============ */
typedef struct {
    uint32_t nsid;
    uint64_t blocks;
    uint32_t block_size;
    uint8_t  active;
} nvme_namespace_t;

/* ============ Controller ============ */

/* Interrupt modes */
#define NVME_INT_MODE_POLL      0   /* Polling (fallback) */
#define NVME_INT_MODE_INTX      1   /* Legacy INTx */
#define NVME_INT_MODE_MSI       2   /* MSI */
#define NVME_INT_MODE_MSIX      3   /* MSI-X (preferred) */

/* Per-CPU Queue Support */
#define NVME_MAX_IO_QUEUES      16  /* Max I/O queue pairs (typically 1 per core) */

/**
 * Per-CPU I/O Queue Context
 * 
 * Each CPU gets its own submission and completion queue pair.
 * This enables lock-free I/O submission from any core.
 */
typedef struct {
    nvme_queue_t    queue;          /* The actual queue structures */
    uint8_t         cpu_id;         /* Owning CPU ID */
    uint8_t         qid;            /* NVMe Queue ID (1-based, 0 is admin) */
    uint8_t         active;         /* Queue is created and usable */
    uint8_t         irq_vector;     /* Assigned MSI-X vector for this queue */
    volatile uint8_t pending;       /* Completion pending flag */
    
    /* Statistics (for future profiling) */
    uint64_t        commands_submitted;
    uint64_t        commands_completed;
} nvme_io_queue_ctx_t;

typedef struct {
    pci_device_t *pci_dev;
    volatile uint32_t *regs;
    
    uint16_t vendor_id;
    uint16_t device_id;
    char serial[20];
    char model[40];
    char firmware[8];
    
    uint32_t doorbell_stride;
    uint64_t max_transfer_size;
    
    /* Admin queue (always queue 0) */
    nvme_queue_t admin_queue;
    
    /* Legacy single I/O queue (for backward compatibility) */
    nvme_queue_t io_queue;
    
    /* Per-CPU I/O queues (Phase 4: Queue-per-core) */
    nvme_io_queue_ctx_t io_queues[NVME_MAX_IO_QUEUES];
    uint8_t  num_io_queues;         /* Number of active I/O queues */
    uint8_t  queue_per_core;        /* 1 if per-core queues enabled */
    
    uint32_t namespace_count;
    nvme_namespace_t namespaces[NVME_MAX_NAMESPACES];
    
    /* Interrupt support */
    uint8_t  int_mode;              /* NVME_INT_MODE_* */
    uint8_t  msix_cap_offset;       /* MSI-X capability offset in PCI config */
    uint16_t msix_table_size;       /* Number of MSI-X vectors */
    volatile uint32_t *msix_table;  /* MSI-X table base address */
    uint8_t  irq_vector;            /* Assigned IRQ vector (legacy) */
    volatile uint8_t io_cq_pending; /* I/O completion pending flag (legacy) */
    
    uint8_t initialized;
} nvme_controller_t;

/* ============ Queue-Per-Core API ============ */

/**
 * nvme_get_cpu_queue - Get I/O queue for current CPU
 * 
 * Returns the queue context assigned to the current CPU,
 * or the default queue if per-core queues are not enabled.
 */
nvme_io_queue_ctx_t *nvme_get_cpu_queue(nvme_controller_t *ctrl);

/**
 * nvme_setup_per_core_queues - Create one I/O queue per CPU
 * 
 * Requires MSI-X for optimal performance (one vector per queue).
 * Falls back to shared queue if MSI-X unavailable.
 */
int nvme_setup_per_core_queues(nvme_controller_t *ctrl, int num_cpus);


/* ============ Public API ============ */

/**
 * nvme_init - Initialize NVMe subsystem
 * 
 * Scans PCI for NVMe controllers and initializes them.
 * 
 * Returns: Number of controllers found, or negative on error
 */
int nvme_init(void);

/**
 * nvme_get_controller - Get controller by index
 */
nvme_controller_t *nvme_get_controller(int index);

/**
 * nvme_read - Read sectors from namespace
 * 
 * @ctrl: NVMe controller
 * @nsid: Namespace ID
 * @lba: Starting logical block address
 * @count: Number of blocks to read
 * @buffer: Buffer to store data
 * 
 * Returns: Number of bytes read, or negative on error
 */
int nvme_read(nvme_controller_t *ctrl, uint32_t nsid, 
              uint64_t lba, uint32_t count, void *buffer);

/**
 * nvme_write - Write sectors to namespace
 */
int nvme_write(nvme_controller_t *ctrl, uint32_t nsid,
               uint64_t lba, uint32_t count, const void *buffer);

/**
 * nvme_get_capacity - Get namespace capacity in bytes
 */
uint64_t nvme_get_capacity(nvme_controller_t *ctrl, uint32_t nsid);

/**
 * nvme_get_controller_count - Get number of detected controllers
 */
int nvme_get_controller_count(void);

#endif /* NVME_H */
