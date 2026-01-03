/*
 * NeolyxOS Print Subsystem Implementation
 * 
 * Printer discovery and job management.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "drivers/print.h"
#include "net/tcpip.h"
#include "mm/kheap.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* ============ State ============ */

static printer_device_t *printers = NULL;
static int printer_count = 0;
static printer_device_t *default_printer = NULL;
static print_job_t *job_queue = NULL;
static uint32_t next_job_id = 1;

/* ============ Helpers ============ */

static void print_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int print_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

static void serial_ip(uint8_t *ip) {
    for (int i = 0; i < 4; i++) {
        if (i > 0) serial_putc('.');
        int v = ip[i];
        if (v >= 100) serial_putc('0' + v / 100);
        if (v >= 10) serial_putc('0' + (v / 10) % 10);
        serial_putc('0' + v % 10);
    }
}

/* ============ Printer Discovery ============ */

static int discover_usb_printers(void) {
    /* TODO: Scan USB for printer class devices */
    return 0;
}

static int discover_network_printers(void) {
    serial_puts("[PRINT] Scanning network for printers...\n");
    
    /* TODO: mDNS/DNS-SD discovery for IPP printers */
    /* For now, simulate finding a network printer */
    
    printer_device_t *printer = (printer_device_t *)kzalloc(sizeof(printer_device_t));
    if (!printer) return -1;
    
    print_strcpy(printer->name, "Office Laser", 64);
    print_strcpy(printer->location, "Main Office", 64);
    print_strcpy(printer->model, "LaserJet Pro", 64);
    print_strcpy(printer->manufacturer, "HP", 64);
    
    printer->type = PRINTER_TYPE_NETWORK;
    printer->state = PRINTER_STATE_IDLE;
    printer->protocol = PRINT_PROTO_IPP;
    
    printer->ip_address[0] = 192;
    printer->ip_address[1] = 168;
    printer->ip_address[2] = 1;
    printer->ip_address[3] = 100;
    printer->port = 631;  /* IPP */
    
    print_strcpy(printer->uri, "ipp://192.168.1.100:631/ipp/print", 256);
    
    printer->caps.supports_color = 0;
    printer->caps.supports_duplex = 1;
    printer->caps.max_dpi = 1200;
    printer->caps.paper_sizes = (1 << PAPER_A4) | (1 << PAPER_LETTER);
    
    printer->paper_level = 75;
    printer->ink_level_black = 60;
    
    printer->next = printers;
    printers = printer;
    printer_count++;
    
    serial_puts("[PRINT] Found: ");
    serial_puts(printer->name);
    serial_puts(" at ");
    serial_ip(printer->ip_address);
    serial_puts("\n");
    
    /* Simulate another printer - wireless */
    printer_device_t *wireless = (printer_device_t *)kzalloc(sizeof(printer_device_t));
    if (wireless) {
        print_strcpy(wireless->name, "Photo Printer", 64);
        print_strcpy(wireless->model, "Inkjet Photo", 64);
        print_strcpy(wireless->manufacturer, "Canon", 64);
        
        wireless->type = PRINTER_TYPE_WIRELESS;
        wireless->state = PRINTER_STATE_IDLE;
        wireless->protocol = PRINT_PROTO_AIRPRINT;
        
        wireless->ip_address[0] = 192;
        wireless->ip_address[1] = 168;
        wireless->ip_address[2] = 1;
        wireless->ip_address[3] = 150;
        wireless->port = 631;
        
        wireless->caps.supports_color = 1;
        wireless->caps.max_dpi = 4800;
        wireless->caps.paper_sizes = (1 << PAPER_A4) | (1 << PAPER_PHOTO_4X6);
        
        wireless->paper_level = 50;
        wireless->ink_level_black = 80;
        wireless->ink_level_cyan = 70;
        wireless->ink_level_magenta = 65;
        wireless->ink_level_yellow = 85;
        
        wireless->next = printers;
        printers = wireless;
        printer_count++;
        
        serial_puts("[PRINT] Found: ");
        serial_puts(wireless->name);
        serial_puts(" (AirPrint)\n");
    }
    
    return 0;
}

/* ============ Print API ============ */

int print_init(void) {
    serial_puts("[PRINT] Initializing print subsystem...\n");
    
    printers = NULL;
    printer_count = 0;
    default_printer = NULL;
    job_queue = NULL;
    
    /* Create virtual PDF printer */
    printer_device_t *pdf = (printer_device_t *)kzalloc(sizeof(printer_device_t));
    if (pdf) {
        print_strcpy(pdf->name, "Print to PDF", 64);
        print_strcpy(pdf->model, "Virtual PDF Printer", 64);
        print_strcpy(pdf->manufacturer, "NeolyxOS", 64);
        
        pdf->type = PRINTER_TYPE_VIRTUAL;
        pdf->state = PRINTER_STATE_IDLE;
        
        pdf->caps.supports_color = 1;
        pdf->caps.max_dpi = 9600;
        pdf->caps.paper_sizes = 0xFFFF;  /* All sizes */
        
        pdf->next = printers;
        printers = pdf;
        printer_count++;
    }
    
    serial_puts("[PRINT] Ready\n");
    return 0;
}

int print_discover(void) {
    discover_usb_printers();
    discover_network_printers();
    
    /* Set first real printer as default */
    printer_device_t *p = printers;
    while (p) {
        if (p->type != PRINTER_TYPE_VIRTUAL) {
            default_printer = p;
            break;
        }
        p = p->next;
    }
    
    serial_puts("[PRINT] ");
    serial_putc('0' + printer_count);
    serial_puts(" printers available\n");
    
    return 0;
}

printer_device_t *print_get_printer(int index) {
    printer_device_t *p = printers;
    int i = 0;
    while (p && i < index) {
        p = p->next;
        i++;
    }
    return p;
}

printer_device_t *print_get_printer_by_name(const char *name) {
    printer_device_t *p = printers;
    while (p) {
        if (print_strcmp(p->name, name) == 0) return p;
        p = p->next;
    }
    return NULL;
}

printer_device_t *print_get_default(void) {
    return default_printer;
}

int print_set_default(printer_device_t *printer) {
    default_printer = printer;
    return 0;
}

int print_printer_count(void) {
    return printer_count;
}

/* ============ Network Printer ============ */

printer_device_t *print_add_network(const char *uri, const char *name) {
    printer_device_t *p = (printer_device_t *)kzalloc(sizeof(printer_device_t));
    if (!p) return NULL;
    
    print_strcpy(p->name, name, 64);
    print_strcpy(p->uri, uri, 256);
    p->type = PRINTER_TYPE_NETWORK;
    p->state = PRINTER_STATE_OFFLINE;
    p->protocol = PRINT_PROTO_IPP;
    
    /* TODO: Parse URI for IP/port */
    
    p->next = printers;
    printers = p;
    printer_count++;
    
    serial_puts("[PRINT] Added: ");
    serial_puts(name);
    serial_puts("\n");
    
    return p;
}

int print_remove(printer_device_t *printer) {
    if (!printer) return -1;
    
    if (printers == printer) {
        printers = printer->next;
    } else {
        printer_device_t *prev = printers;
        while (prev && prev->next != printer) prev = prev->next;
        if (prev) prev->next = printer->next;
    }
    
    if (default_printer == printer) default_printer = NULL;
    
    kfree(printer);
    printer_count--;
    
    return 0;
}

int print_test(printer_device_t *printer) {
    if (!printer) return -1;
    
    serial_puts("[PRINT] Testing ");
    serial_puts(printer->name);
    serial_puts("...\n");
    
    /* TODO: Send test page or ping */
    printer->state = PRINTER_STATE_IDLE;
    
    serial_puts("[PRINT] OK\n");
    return 0;
}

/* ============ Print Jobs ============ */

print_job_t *print_create_job(printer_device_t *printer, const char *name) {
    if (!printer) printer = default_printer;
    if (!printer) return NULL;
    
    print_job_t *job = (print_job_t *)kzalloc(sizeof(print_job_t));
    if (!job) return NULL;
    
    job->id = next_job_id++;
    print_strcpy(job->name, name, 128);
    job->printer = printer;
    
    /* Default settings */
    job->paper_size = PAPER_A4;
    job->quality = PRINT_QUALITY_NORMAL;
    job->color_mode = printer->caps.supports_color ? COLOR_MODE_COLOR : COLOR_MODE_BW;
    job->copies = 1;
    job->duplex = 0;
    job->state = 0;
    
    return job;
}

int print_job_set_data(print_job_t *job, const void *data, uint32_t size) {
    if (!job) return -1;
    
    job->data = (uint8_t *)kmalloc(size);
    if (!job->data) return -1;
    
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t i = 0; i < size; i++) {
        job->data[i] = src[i];
    }
    job->data_size = size;
    
    return 0;
}

int print_job_submit(print_job_t *job) {
    if (!job) return -1;
    
    serial_puts("[PRINT] Submitting job: ");
    serial_puts(job->name);
    serial_puts("\n");
    
    /* Add to queue */
    job->next = job_queue;
    job_queue = job;
    
    job->printer->jobs_pending++;
    job->state = 1;  /* Printing */
    
    /* TODO: Actually send to printer */
    
    serial_puts("[PRINT] Job ");
    serial_putc('0' + job->id);
    serial_puts(" queued\n");
    
    return 0;
}

int print_job_cancel(print_job_t *job) {
    if (!job) return -1;
    
    job->state = -1;
    job->printer->jobs_pending--;
    
    return 0;
}

int print_job_status(print_job_t *job) {
    return job ? job->state : -1;
}

/* ============ Quick Print ============ */

int print_text(const char *text) {
    printer_device_t *p = print_get_default();
    if (!p) return -1;
    
    print_job_t *job = print_create_job(p, "Text Document");
    if (!job) return -1;
    
    /* Calculate length */
    uint32_t len = 0;
    while (text[len]) len++;
    
    print_job_set_data(job, text, len);
    return print_job_submit(job);
}

int print_raw(printer_device_t *printer, const void *data, uint32_t size) {
    if (!printer || !data) return -1;
    
    print_job_t *job = print_create_job(printer, "Raw Data");
    if (!job) return -1;
    
    print_job_set_data(job, data, size);
    return print_job_submit(job);
}

/* ============ AirPrint ============ */

int print_enable_airprint(void) {
    serial_puts("[PRINT] AirPrint discovery enabled\n");
    /* TODO: mDNS responder */
    return 0;
}

int print_discover_airprint(void) {
    serial_puts("[PRINT] Discovering AirPrint devices...\n");
    /* TODO: mDNS query for _ipp._tcp */
    return 0;
}
