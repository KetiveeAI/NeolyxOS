/*
 * NXMouse Kernel Driver (kdrv format)
 * PS/2 Mouse with IRQ12 interrupt support
 * 
 * Copyright (c) 2025 KetiveeAI - All rights reserved.
 */

#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* Local helper for debug output */
static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 10;
    buf[11] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) {
        buf[i--] = '0' + (val % 10);
        val /= 10;
    }
    serial_puts(&buf[i + 1]);
}

extern void idt_register_handler(uint8_t vector, void (*handler)(void *));
extern void pic_unmask_irq(uint8_t irq);
/* Note: pic_send_eoi is NOT needed here - EOI handled by ISR dispatcher */

/* I/O port access */
static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

/* ============ Constants ============ */

#define NXMOUSE_VERSION      "1.1.0"
#define MOUSE_IRQ            12
#define MOUSE_INT_VEC        44

#define PS2_DATA_PORT        0x60
#define PS2_COMMAND_PORT     0x64
#define PS2_STATUS_PORT      0x64

#define MOUSE_CMD_RESET      0xFF
#define MOUSE_CMD_ENABLE     0xF4
#define MOUSE_CMD_DISABLE    0xF5
#define MOUSE_CMD_SET_DEFAULTS 0xF6
#define MOUSE_CMD_SET_SAMPLE_RATE 0xF3
#define MOUSE_CMD_SET_RESOLUTION 0xE8
#define MOUSE_CMD_GET_DEVICE_ID 0xF2

#define MOUSE_ACK            0xFA

/* Event queue size */
#define EVENT_QUEUE_SIZE     32

/* ============ Types ============ */

typedef struct {
    int32_t  dx;
    int32_t  dy;
    int32_t  dz;  /* Scroll */
    uint8_t  buttons;
    uint64_t timestamp;
} nxmouse_event_t;

typedef struct {
    int      initialized;
    int      enabled;
    uint8_t  mouse_id;      /* 0=standard, 3=scroll, 4=5-button */
    uint8_t  packet_size;   /* 3 or 4 bytes */
    
    /* Absolute position */
    int32_t  x;
    int32_t  y;
    
    /* Accumulated delta (cleared on read) */
    volatile int32_t delta_x;
    volatile int32_t delta_y;
    volatile int32_t delta_z;
    volatile uint8_t buttons;
    
    /* Screen bounds */
    int32_t  screen_width;
    int32_t  screen_height;
    
    /* IRQ packet assembly */
    uint8_t  packet_buf[5];
    int      packet_idx;
    
    /* Event queue (ring buffer) */
    nxmouse_event_t event_queue[EVENT_QUEUE_SIZE];
    int      event_head;
    int      event_tail;
} nxmouse_state_t;

static nxmouse_state_t g_mouse = {0};

/* ============ PS/2 Low-Level ============ */

static int ps2_wait_input(void) {
    for (int i = 0; i < 10000; i++) {
        if (!(inb(PS2_STATUS_PORT) & 0x02))
            return 0;
        for (int j = 0; j < 100; j++) __asm__ volatile("nop");
    }
    return -1;
}

static int ps2_wait_output(void) {
    for (int i = 0; i < 10000; i++) {
        if (inb(PS2_STATUS_PORT) & 0x01)
            return 0;
        for (int j = 0; j < 100; j++) __asm__ volatile("nop");
    }
    return -1;
}

static int ps2_send_mouse_cmd(uint8_t cmd) {
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_COMMAND_PORT, 0xD4);  /* Send to mouse */
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_DATA_PORT, cmd);
    if (ps2_wait_output() != 0) return -1;
    return (inb(PS2_DATA_PORT) == MOUSE_ACK) ? 0 : -1;
}

static int ps2_read_byte(uint8_t *byte) {
    if (ps2_wait_output() != 0) return -1;
    *byte = inb(PS2_DATA_PORT);
    return 0;
}

/* ============ Mouse Detection ============ */

static uint8_t detect_mouse_type(void) {
    uint8_t mouse_id = 0;
    
    /* Enable scroll wheel: magic sequence 200, 100, 80 */
    ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_send_mouse_cmd(200);
    ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_send_mouse_cmd(100);
    ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_send_mouse_cmd(80);
    
    if (ps2_send_mouse_cmd(MOUSE_CMD_GET_DEVICE_ID) == 0) {
        uint8_t id;
        if (ps2_read_byte(&id) == 0) mouse_id = id;
    }
    
    if (mouse_id == 3) {
        /* Try 5-button: 200, 200, 80 */
        ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
        ps2_send_mouse_cmd(200);
        ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
        ps2_send_mouse_cmd(200);
        ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
        ps2_send_mouse_cmd(80);
        
        if (ps2_send_mouse_cmd(MOUSE_CMD_GET_DEVICE_ID) == 0) {
            uint8_t id;
            if (ps2_read_byte(&id) == 0) mouse_id = id;
        }
    }
    
    return mouse_id;
}

/* ============ Packet Processing ============ */

static void process_packet(void) {
    uint8_t *p = g_mouse.packet_buf;
    
    /* BUG #3 FIX: Correct PS/2 sign extension
     * Bit 4 of byte 0 = X sign, Bit 5 = Y sign
     * If sign bit set, value - 256 gives negative
     */
    int32_t dx = p[1];
    int32_t dy = p[2];
    
    if (p[0] & 0x10) dx = dx - 256;  /* X sign bit */
    if (p[0] & 0x20) dy = dy - 256;  /* Y sign bit */
    dy = -dy;  /* Invert Y for screen coordinates */
    
    int8_t dz = 0;
    uint8_t buttons = p[0] & 0x07;
    
    /* Handle 4-byte Intellimouse packet */
    if (g_mouse.packet_size >= 4) {
        dz = (int8_t)(p[3] & 0x0F);
        if (dz & 0x08) dz |= 0xF0;  /* Sign extend */
        
        if (g_mouse.mouse_id == 4) {
            if (p[3] & 0x10) buttons |= 0x08;
            if (p[3] & 0x20) buttons |= 0x10;
        }
    }
    
    /* BUG #4 FIX: No CLI/STI inside IRQ - already in interrupt context */
    /* Accumulate deltas (we're already in IRQ, interrupts disabled) */
    g_mouse.delta_x += dx;
    g_mouse.delta_y += dy;
    g_mouse.delta_z += dz;
    g_mouse.buttons = buttons;
    
    /* Update absolute position */
    g_mouse.x += dx;
    g_mouse.y += dy;
    
    /* Clamp to screen */
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.x >= g_mouse.screen_width) g_mouse.x = g_mouse.screen_width - 1;
    if (g_mouse.y >= g_mouse.screen_height) g_mouse.y = g_mouse.screen_height - 1;
    
    /* Queue event */
    int next = (g_mouse.event_head + 1) % EVENT_QUEUE_SIZE;
    if (next != g_mouse.event_tail) {
        nxmouse_event_t *ev = &g_mouse.event_queue[g_mouse.event_head];
        ev->dx = dx;
        ev->dy = dy;
        ev->dz = dz;
        ev->buttons = buttons;
        ev->timestamp = pit_get_ticks();
        g_mouse.event_head = next;
    }
}

/* ============ IRQ Handler ============ */

static volatile uint32_t mouse_irq_count = 0;  /* Debug counter */
static volatile uint32_t packets_completed = 0;  /* Debug counter */
static volatile uint32_t sync_failures = 0;  /* Debug counter */

/* BUG #1 FIX: Correct handler signature to match IDT ABI */
void nxmouse_irq_handler(void *frame) {
    (void)frame;  /* Unused but required for proper stack handling */
    
    mouse_irq_count++;
    
    /* Debug print first 10 IRQs */
    if (mouse_irq_count <= 10) {
        serial_puts("[MOUSE_IRQ] #");
        serial_dec(mouse_irq_count);
        serial_puts("\n");
    }
    
    if (!g_mouse.initialized || !g_mouse.enabled) {
        /* Not initialized - just return, dispatcher will send EOI */
        return;
    }
    
    /* BUG #2 FIX: Check PS/2 status to verify this is mouse data (AUX bit) */
    uint8_t status = inb(PS2_STATUS_PORT);
    if (!(status & 0x20)) {
        /* Bit 5 not set = this is keyboard data, not mouse */
        /* Just return - dispatcher will send EOI */
        return;
    }
    
    uint8_t data = inb(PS2_DATA_PORT);
    
    /* Sync check: first byte must have bit 3 set */
    /* RELAXED for debugging - just log failures instead of rejecting */
    if (g_mouse.packet_idx == 0 && !(data & 0x08)) {
        sync_failures++;
        /* Log first 5 sync failures */
        if (sync_failures <= 5) {
            serial_puts("[MOUSE] Sync fail: data=0x");
            serial_dec(data);
            serial_puts("\n");
        }
        /* Still try to process for QEMU compat */
    }
    
    g_mouse.packet_buf[g_mouse.packet_idx++] = data;
    
    if (g_mouse.packet_idx >= g_mouse.packet_size) {
        packets_completed++;
        /* Print first 5 completed packets for debug */
        if (packets_completed <= 5) {
            serial_puts("[MOUSE] Packet: ");
            serial_dec(g_mouse.packet_buf[0]);
            serial_puts(" ");
            serial_dec(g_mouse.packet_buf[1]);
            serial_puts(" ");
            serial_dec(g_mouse.packet_buf[2]);
            serial_puts("\n");
        }
        process_packet();
        g_mouse.packet_idx = 0;
    }
    
    /* EOI handled by ISR dispatcher - do not send here */
}

/* ============ Public API ============ */

int nxmouse_kdrv_init(void) {
    if (g_mouse.initialized) return 0;
    
    serial_puts("[NXMouse] Initializing v" NXMOUSE_VERSION "\n");
    
    /* Clear state */
    for (int i = 0; i < (int)sizeof(g_mouse); i++)
        ((uint8_t*)&g_mouse)[i] = 0;
    
    /* Default screen size */
    g_mouse.screen_width = 1280;
    g_mouse.screen_height = 800;
    
    /* ========== CRITICAL: PS/2 Controller Configuration ========== */
    /* This enables the second PS/2 port (mouse) and enables IRQ12 */
    
    serial_puts("[NXMouse] Configuring PS/2 controller...\n");
    
    /* 1. Enable second PS/2 port (for mouse) */
    if (ps2_wait_input() != 0) {
        serial_puts("[NXMouse] PS/2 controller timeout\n");
        return -1;
    }
    outb(PS2_COMMAND_PORT, 0xA8);  /* Enable second port command */
    
    /* Small delay for controller to process */
    for (int i = 0; i < 1000; i++) __asm__ volatile("nop");
    
    /* 2. Read Controller Configuration Byte (CCB) */
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_COMMAND_PORT, 0x20);  /* Read CCB command */
    if (ps2_wait_output() != 0) return -1;
    uint8_t ccb = inb(PS2_DATA_PORT);
    
    serial_puts("[NXMouse] CCB before: 0x");
    /* Print hex nibbles */
    serial_putc("0123456789ABCDEF"[(ccb >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[ccb & 0xF]);
    serial_puts("\n");
    
    /* 3. Modify CCB:
     *    Bit 0: Enable IRQ1 (keyboard) - keep enabled
     *    Bit 1: Enable IRQ12 (mouse) - SET
     *    Bit 4: Disable keyboard clock - CLEAR (enable keyboard)
     *    Bit 5: Disable mouse clock - CLEAR (enable mouse)
     *    Bit 6: Translation - keep as is
     */
    uint8_t new_ccb = ccb;
    new_ccb |= 0x01;   /* Bit 0 = Enable IRQ1 (keyboard) */
    new_ccb |= 0x02;   /* Bit 1 = Enable IRQ12 (mouse) */
    new_ccb &= ~0x10;  /* Bit 4 = Clear to enable keyboard clock */
    new_ccb &= ~0x20;  /* Bit 5 = Clear to enable mouse clock */
    
    serial_puts("[NXMouse] Writing CCB: 0x");
    serial_putc("0123456789ABCDEF"[(new_ccb >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[new_ccb & 0xF]);
    serial_puts("\n");
    
    /* 4. Write back modified CCB */
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_COMMAND_PORT, 0x60);  /* Write CCB command */
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_DATA_PORT, new_ccb);
    
    /* Small delay for controller to process */
    for (int i = 0; i < 1000; i++) __asm__ volatile("nop");
    
    /* 5. READ BACK CCB to verify write succeeded */
    if (ps2_wait_input() != 0) return -1;
    outb(PS2_COMMAND_PORT, 0x20);  /* Read CCB command */
    if (ps2_wait_output() != 0) return -1;
    uint8_t verify_ccb = inb(PS2_DATA_PORT);
    
    serial_puts("[NXMouse] CCB verify: 0x");
    serial_putc("0123456789ABCDEF"[(verify_ccb >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[verify_ccb & 0xF]);
    if (verify_ccb & 0x02) {
        serial_puts(" (IRQ12 ENABLED)\n");
    } else {
        serial_puts(" (IRQ12 still disabled!)\n");
    }
    
    /* ========== End PS/2 Controller Configuration ========== */
    
    /* Reset mouse */
    if (ps2_send_mouse_cmd(MOUSE_CMD_RESET) != 0) {
        serial_puts("[NXMouse] Reset failed\n");
        return -1;
    }
    
    uint8_t response;
    if (ps2_read_byte(&response) != 0 || response != 0xAA) {
        serial_puts("[NXMouse] Self-test failed\n");
        return -1;
    }
    ps2_read_byte(&response);  /* Device ID */
    
    /* Detect mouse type */
    g_mouse.mouse_id = detect_mouse_type();
    g_mouse.packet_size = (g_mouse.mouse_id >= 3) ? 4 : 3;
    
    if (g_mouse.mouse_id == 4) {
        serial_puts("[NXMouse] 5-button Intellimouse detected\n");
    } else if (g_mouse.mouse_id == 3) {
        serial_puts("[NXMouse] Scroll wheel mouse detected\n");
    } else {
        serial_puts("[NXMouse] Standard PS/2 mouse detected\n");
    }
    
    /* Configure mouse */
    ps2_send_mouse_cmd(MOUSE_CMD_SET_DEFAULTS);
    ps2_send_mouse_cmd(MOUSE_CMD_SET_RESOLUTION);
    ps2_send_mouse_cmd(2);  /* 4 counts/mm */
    ps2_send_mouse_cmd(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_send_mouse_cmd(100);  /* 100 Hz */
    ps2_send_mouse_cmd(MOUSE_CMD_ENABLE);
    
    g_mouse.enabled = 1;
    g_mouse.initialized = 1;
    
    /* Register IRQ handler */
    serial_puts("[NXMouse] Registering IRQ handler vector 44...\n");
    idt_register_handler(MOUSE_INT_VEC, (void (*)(void *))nxmouse_irq_handler);
    
    /* Unmask IRQ12 on PIC2 (slave PIC)
     * IRQ12 = bit 4 on slave PIC (12 - 8 = 4)
     * Port 0xA1 = PIC2 data (mask register)
     */
    serial_puts("[NXMouse] PIC2 mask before: 0x");
    uint8_t pic2_before = inb(0xA1);
    serial_putc("0123456789ABCDEF"[(pic2_before >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[pic2_before & 0xF]);
    serial_puts("\n");
    
    /* Directly clear bit 4 to unmask IRQ12 */
    uint8_t pic2_new = pic2_before & ~(1 << 4);  /* Clear bit 4 */
    outb(0xA1, pic2_new);
    
    /* Read back to verify */
    uint8_t pic2_after = inb(0xA1);
    serial_puts("[NXMouse] PIC2 mask after:  0x");
    serial_putc("0123456789ABCDEF"[(pic2_after >> 4) & 0xF]);
    serial_putc("0123456789ABCDEF"[pic2_after & 0xF]);
    
    if (pic2_after & (1 << 4)) {
        serial_puts(" (IRQ12 MASKED - ERROR!)\n");
    } else {
        serial_puts(" (IRQ12 UNMASKED - OK)\n");
    }
    
    serial_puts("[NXMouse] Driver ready - move mouse to test!\n");
    return 0;
}

void nxmouse_kdrv_shutdown(void) {
    if (!g_mouse.initialized) return;
    ps2_send_mouse_cmd(MOUSE_CMD_DISABLE);
    g_mouse.enabled = 0;
    g_mouse.initialized = 0;
    serial_puts("[NXMouse] Shutdown\n");
}

/* Get and clear accumulated delta - MAIN INTERFACE for desktop */
int nxmouse_get_delta(int32_t *dx, int32_t *dy, uint8_t *buttons) {
    if (!g_mouse.initialized) return -1;
    
    /* Atomically read and clear */
    __asm__ volatile("cli");
    *dx = g_mouse.delta_x;
    *dy = g_mouse.delta_y;
    *buttons = g_mouse.buttons;
    g_mouse.delta_x = 0;
    g_mouse.delta_y = 0;
    __asm__ volatile("sti");
    
    return 0;
}

/* Get absolute position */
int nxmouse_get_position(int32_t *x, int32_t *y) {
    if (!g_mouse.initialized) return -1;
    *x = g_mouse.x;
    *y = g_mouse.y;
    return 0;
}

/* Set absolute position */
void nxmouse_set_position(int32_t x, int32_t y) {
    g_mouse.x = x;
    g_mouse.y = y;
}

/* Set screen bounds */
void nxmouse_set_screen_size(int32_t w, int32_t h) {
    g_mouse.screen_width = w;
    g_mouse.screen_height = h;
}

/* Poll event from queue */
int nxmouse_poll_event(nxmouse_event_t *event) {
    if (!event) return -1;
    if (g_mouse.event_head == g_mouse.event_tail) return 0;
    
    *event = g_mouse.event_queue[g_mouse.event_tail];
    g_mouse.event_tail = (g_mouse.event_tail + 1) % EVENT_QUEUE_SIZE;
    return 1;
}

/* Check if mouse is ready */
int nxmouse_is_ready(void) {
    return g_mouse.initialized && g_mouse.enabled;
}

/* Get button state */
uint8_t nxmouse_get_buttons(void) {
    return g_mouse.buttons;
}

/* ============ Legacy Compatibility Layer ============ */
/* These functions are called from desktop_shell.c */

int mouse_get_movement(int *dx, int *dy) {
    int32_t mx, my;
    uint8_t btn;
    if (nxmouse_get_delta(&mx, &my, &btn) != 0) {
        *dx = 0;
        *dy = 0;
        return -1;
    }
    *dx = mx;
    *dy = my;
    return 0;
}

int mouse_get_buttons(void) {
    return (int)g_mouse.buttons;
}

int mouse_is_ready(void) {
    return nxmouse_is_ready();
}

int mouse_set_enabled(int enabled) {
    if (!g_mouse.initialized) return -1;
    if (enabled) {
        ps2_send_mouse_cmd(MOUSE_CMD_ENABLE);
        g_mouse.enabled = 1;
        serial_puts("[NXMouse] Enabled\n");
    } else {
        ps2_send_mouse_cmd(MOUSE_CMD_DISABLE);
        g_mouse.enabled = 0;
        serial_puts("[NXMouse] Disabled\n");
    }
    return 0;
}

/* Called from drivers.c */
int mouse_driver_init(void *drv) {
    (void)drv;
    return nxmouse_kdrv_init();
}

int mouse_driver_deinit(void *drv) {
    (void)drv;
    nxmouse_kdrv_shutdown();
    return 0;
}

/* ============ Syscall Interface ============ */

/**
 * mouse_get_event - Get mouse event for syscall
 * Called by sys_input_poll syscall to get mouse events
 * Returns 0 if event/data available, -1 if no movement
 */
static uint32_t mouse_poll_count = 0;
int mouse_get_event(int16_t *dx, int16_t *dy, uint8_t *buttons) {
    mouse_poll_count++;
    
    if (!g_mouse.initialized) {
        return -1;
    }
    
    /* ===== CRITICAL FIX: Poll PS/2 port directly if IRQ12 isn't working ===== */
    /* This is a fallback when interrupts aren't delivered after CR3 switch */
    if (mouse_irq_count == 0 && mouse_poll_count > 100) {
        /* Check if mouse data is available on port 0x60 */
        uint8_t status = inb(PS2_STATUS_PORT);
        
        /* Debug: Log port status periodically */
        static uint32_t poll_debug = 0;
        poll_debug++;
        if (poll_debug <= 5 || poll_debug % 2000 == 0) {
            serial_puts("[PS2_POLL] status=0x");
            serial_putc("0123456789ABCDEF"[(status >> 4) & 0xF]);
            serial_putc("0123456789ABCDEF"[status & 0xF]);
            serial_puts(" (need 0x21 for mouse data)\n");
        }
        
        /* Bit 0 = output buffer full, Bit 5 = mouse data (vs keyboard) */
        /* Also try with just bit 0 set - some emulators don't set bit 5 */
        if (status & 0x01) {
            /* Read byte */
            uint8_t byte = inb(PS2_DATA_PORT);
            
            /* Only process if bit 5 says it's from mouse, OR if we're desperate */
            if ((status & 0x20) || poll_debug > 5000) {
                /* Feed into packet buffer */
                if (g_mouse.packet_idx < g_mouse.packet_size) {
                    /* Validate first byte (bit 3 must be set) */
                    if (g_mouse.packet_idx == 0 && !(byte & 0x08)) {
                        /* Bad sync, skip */
                        return -1;
                    }
                    g_mouse.packet_buf[g_mouse.packet_idx++] = byte;
                    
                    /* Complete packet? */
                    if (g_mouse.packet_idx >= g_mouse.packet_size) {
                        /* Process like IRQ handler would */
                        uint8_t *p = g_mouse.packet_buf;
                        int32_t pdx = p[1];
                        int32_t pdy = p[2];
                        if (p[0] & 0x10) pdx = pdx - 256;
                        if (p[0] & 0x20) pdy = pdy - 256;
                        pdy = -pdy;
                        
                        g_mouse.delta_x += pdx;
                        g_mouse.delta_y += pdy;
                        g_mouse.buttons = p[0] & 0x07;
                        g_mouse.packet_idx = 0;
                        
                        serial_puts("[PS2_POLL] Got packet! dx=");
                        serial_dec((uint32_t)(pdx < 0 ? -pdx : pdx));
                        serial_puts(" dy=");
                        serial_dec((uint32_t)(pdy < 0 ? -pdy : pdy));
                        serial_puts("\n");
                    }
                }
            }
        }
    }
    /* ======================================================================= */
    
    /* Check if there's any delta to report */
    if (g_mouse.delta_x == 0 && g_mouse.delta_y == 0) {
        /* No movement, just return current button state */
        *dx = 0;
        *dy = 0;
        *buttons = g_mouse.buttons;
        return -1;  /* No event */
    }
    
    /* Atomically read and clear delta */
    __asm__ volatile("cli");
    *dx = (int16_t)g_mouse.delta_x;
    *dy = (int16_t)g_mouse.delta_y;
    *buttons = g_mouse.buttons;
    g_mouse.delta_x = 0;
    g_mouse.delta_y = 0;
    __asm__ volatile("sti");
    
    return 0;  /* Event available */
}

/**
 * mouse_poll - Poll for mouse movement (desktop.c interface)
 * This matches the signature expected by desktop.c
 * Returns 1 if event available, 0 if none
 */
int mouse_poll(int8_t *dx, int8_t *dy, uint8_t *buttons) {
    static int poll_counter = 0;
    poll_counter++;
    
    /* Debug: log every 10000 calls */
    if (poll_counter % 10000 == 0) {
        serial_puts("[MOUSE] poll #");
        serial_dec(poll_counter);
        serial_puts(" init=");
        serial_putc(g_mouse.initialized ? '1' : '0');
        serial_puts("\n");
    }
    
    if (!g_mouse.initialized) {
        return 0;
    }
    
    /* Check if there's any delta to report */
    if (g_mouse.delta_x == 0 && g_mouse.delta_y == 0) {
        *dx = 0;
        *dy = 0;
        *buttons = g_mouse.buttons;
        return 0;  /* No movement */
    }
    
    /* Atomically read and clear delta */
    __asm__ volatile("cli");
    int16_t rx = g_mouse.delta_x;
    int16_t ry = g_mouse.delta_y;
    g_mouse.delta_x = 0;
    g_mouse.delta_y = 0;
    __asm__ volatile("sti");
    
    /* Clamp to int8_t range */
    if (rx > 127) rx = 127;
    if (rx < -128) rx = -128;
    if (ry > 127) ry = 127;
    if (ry < -128) ry = -128;
    
    *dx = (int8_t)rx;
    *dy = (int8_t)ry;
    *buttons = g_mouse.buttons;
    
    return 1;  /* Event available */
}

/**
 * mouse_debug_status - Get debug info for diagnosis
 * Prints IRQ count and packet count to serial for debugging
 */
void mouse_debug_status(void) {
    serial_puts("[MOUSE_DEBUG] IRQ count: ");
    serial_dec(mouse_irq_count);
    serial_puts(", packets: ");
    serial_dec(packets_completed);
    serial_puts(", sync_fail: ");
    serial_dec(sync_failures);
    serial_puts(", init: ");
    serial_putc(g_mouse.initialized ? '1' : '0');
    serial_puts(", dx=");
    serial_dec((uint32_t)(g_mouse.delta_x < 0 ? -g_mouse.delta_x : g_mouse.delta_x));
    serial_puts(", dy=");
    serial_dec((uint32_t)(g_mouse.delta_y < 0 ? -g_mouse.delta_y : g_mouse.delta_y));
    serial_puts("\n");
}
