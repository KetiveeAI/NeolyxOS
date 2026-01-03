/*
 * NeolyxOS Print Subsystem
 * 
 * Printer support:
 *   - USB printers
 *   - Network printers (IPP, LPD, Raw)
 *   - Wireless printers (AirPrint, WiFi Direct)
 * 
 * Features:
 *   - Automatic printer discovery
 *   - Print queue management
 *   - Page formatting
 *   - Multiple paper sizes
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_PRINT_H
#define NEOLYX_PRINT_H

#include <stdint.h>

/* ============ Printer Types ============ */

typedef enum {
    PRINTER_TYPE_USB = 0,
    PRINTER_TYPE_NETWORK,       /* LAN via IPP/LPD */
    PRINTER_TYPE_WIRELESS,      /* WiFi Direct / AirPrint */
    PRINTER_TYPE_BLUETOOTH,
    PRINTER_TYPE_VIRTUAL,       /* PDF printer */
} printer_type_t;

/* ============ Printer State ============ */

typedef enum {
    PRINTER_STATE_OFFLINE = 0,
    PRINTER_STATE_IDLE,
    PRINTER_STATE_PRINTING,
    PRINTER_STATE_PAUSED,
    PRINTER_STATE_ERROR,
    PRINTER_STATE_OUT_OF_PAPER,
    PRINTER_STATE_OUT_OF_INK,
} printer_state_t;

/* ============ Paper Sizes ============ */

typedef enum {
    PAPER_A4 = 0,
    PAPER_LETTER,
    PAPER_LEGAL,
    PAPER_A3,
    PAPER_A5,
    PAPER_B5,
    PAPER_ENVELOPE,
    PAPER_PHOTO_4X6,
    PAPER_PHOTO_5X7,
} paper_size_t;

/* ============ Print Quality ============ */

typedef enum {
    PRINT_QUALITY_DRAFT = 0,
    PRINT_QUALITY_NORMAL,
    PRINT_QUALITY_HIGH,
    PRINT_QUALITY_PHOTO,
} print_quality_t;

/* ============ Color Mode ============ */

typedef enum {
    COLOR_MODE_BW = 0,          /* Black & White */
    COLOR_MODE_GRAYSCALE,
    COLOR_MODE_COLOR,
} color_mode_t;

/* ============ Network Protocols ============ */

typedef enum {
    PRINT_PROTO_RAW = 0,        /* Raw TCP port 9100 */
    PRINT_PROTO_LPD,            /* Line Printer Daemon (515) */
    PRINT_PROTO_IPP,            /* Internet Printing Protocol */
    PRINT_PROTO_IPPS,           /* IPP over HTTPS */
    PRINT_PROTO_SMB,            /* Windows sharing */
    PRINT_PROTO_AIRPRINT,       /* Apple AirPrint */
    PRINT_PROTO_MOPRIA,         /* Android/universal */
} print_protocol_t;

/* ============ Printer Capabilities ============ */

typedef struct {
    int supports_color;
    int supports_duplex;
    int supports_staple;
    int supports_collate;
    int max_copies;
    
    uint32_t max_dpi;
    
    /* Supported paper sizes (bitmap) */
    uint32_t paper_sizes;
    
    /* Trays */
    int num_trays;
} printer_caps_t;

/* ============ Printer Device ============ */

typedef struct printer_device {
    char name[64];
    char location[64];
    char model[64];
    char manufacturer[64];
    
    printer_type_t type;
    printer_state_t state;
    printer_caps_t caps;
    
    /* Network info */
    uint8_t ip_address[4];
    uint16_t port;
    print_protocol_t protocol;
    char uri[256];              /* ipp://... */
    
    /* USB info */
    uint16_t vendor_id;
    uint16_t product_id;
    
    /* Status */
    int paper_level;            /* 0-100% */
    int ink_level_black;        /* 0-100% */
    int ink_level_cyan;
    int ink_level_magenta;
    int ink_level_yellow;
    
    /* Queue */
    int jobs_pending;
    
    struct printer_device *next;
} printer_device_t;

/* ============ Print Job ============ */

typedef struct print_job {
    uint32_t id;
    char name[128];             /* Document name */
    
    printer_device_t *printer;
    
    /* Settings */
    paper_size_t paper_size;
    print_quality_t quality;
    color_mode_t color_mode;
    int copies;
    int duplex;
    int collate;
    
    /* Pages */
    int page_from;
    int page_to;                /* 0 = all */
    
    /* State */
    int state;                  /* 0=queued, 1=printing, 2=done, -1=error */
    int pages_printed;
    int total_pages;
    
    /* Data */
    uint8_t *data;
    uint32_t data_size;
    
    struct print_job *next;
} print_job_t;

/* ============ Print API ============ */

/**
 * Initialize print subsystem.
 */
int print_init(void);

/**
 * Discover printers.
 */
int print_discover(void);

/**
 * Get printer by index.
 */
printer_device_t *print_get_printer(int index);

/**
 * Get printer by name.
 */
printer_device_t *print_get_printer_by_name(const char *name);

/**
 * Get default printer.
 */
printer_device_t *print_get_default(void);

/**
 * Set default printer.
 */
int print_set_default(printer_device_t *printer);

/**
 * Get printer count.
 */
int print_printer_count(void);

/* ============ Network Printer ============ */

/**
 * Add network printer manually.
 */
printer_device_t *print_add_network(const char *uri, const char *name);

/**
 * Remove printer.
 */
int print_remove(printer_device_t *printer);

/**
 * Test printer connection.
 */
int print_test(printer_device_t *printer);

/* ============ Print Jobs ============ */

/**
 * Create a print job.
 */
print_job_t *print_create_job(printer_device_t *printer, const char *name);

/**
 * Set job data.
 */
int print_job_set_data(print_job_t *job, const void *data, uint32_t size);

/**
 * Submit job to queue.
 */
int print_job_submit(print_job_t *job);

/**
 * Cancel job.
 */
int print_job_cancel(print_job_t *job);

/**
 * Get job status.
 */
int print_job_status(print_job_t *job);

/**
 * Get pending jobs.
 */
int print_get_jobs(printer_device_t *printer, print_job_t *jobs, int max);

/* ============ Quick Print ============ */

/**
 * Print text to default printer.
 */
int print_text(const char *text);

/**
 * Print raw data.
 */
int print_raw(printer_device_t *printer, const void *data, uint32_t size);

/* ============ AirPrint / mDNS ============ */

/**
 * Enable AirPrint discovery.
 */
int print_enable_airprint(void);

/**
 * Query AirPrint printers.
 */
int print_discover_airprint(void);

#endif /* NEOLYX_PRINT_H */
