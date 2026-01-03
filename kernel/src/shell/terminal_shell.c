/*
 * NeolyxOS Terminal Shell
 * 
 * Main terminal shell runner - connects console, keyboard, and terminal parser.
 * This is the entry point called from kernel_main after initialization.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "shell/terminal.h"
#include "video/console.h"

/* ============ External Dependencies ============ */

/* Serial output for debugging */
extern void serial_puts(const char *s);

/* Keyboard input - use interrupt-driven driver */
#include "drivers/keyboard.h"

/* ============ Color Scheme ============ */

#define SHELL_BG_COLOR      0x00151530   /* Dark blue background */
#define SHELL_FG_COLOR      0x00E0E0E0   /* Light gray text */
#define SHELL_PROMPT_COLOR  0x0058A6FF   /* Blue prompt */
#define SHELL_ERROR_COLOR   0x00FF5555   /* Red for errors */
#define SHELL_SUCCESS_COLOR 0x0050FA7B   /* Green for success */

/* ============ Terminal State ============ */

static terminal_state_t g_terminal;
static int shell_should_exit = 0;

/* ============ Output Rendering ============ */

/*
 * Render terminal output to console
 * This takes the terminal's output buffer and displays it via console_puts
 */
static void shell_render_output(terminal_state_t *term) {
    if (term->output_len > 0) {
        console_puts(term->output);
        terminal_clear_output(term);
    }
}

/*
 * Render the command prompt
 */
extern int keyboard_is_capslock(void);

static void shell_render_prompt(terminal_state_t *term) {
    const char *prompt = terminal_get_prompt(term);
    
    /* Show caps lock indicator if active */
    if (keyboard_is_capslock()) {
        console_set_color(0xFFFF00, SHELL_BG_COLOR);  /* Yellow warning */
        console_puts("[CAPS] ");
    }
    
    /* Username@hostname in prompt color */
    console_set_color(SHELL_PROMPT_COLOR, SHELL_BG_COLOR);
    console_puts(prompt);
    
    /* Reset to normal color for user input */
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
}

/*
 * Display welcome banner
 */
static void shell_show_banner(void) {
    console_set_color(SHELL_SUCCESS_COLOR, SHELL_BG_COLOR);
    console_puts("\n");
    console_puts("  ╔═══════════════════════════════════════════════╗\n");
    console_puts("  ║            NeolyxOS Terminal v1.0             ║\n");
    console_puts("  ║        Independent Operating System          ║\n");
    console_puts("  ╚═══════════════════════════════════════════════╝\n");
    console_puts("\n");
    
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
    console_puts("  Type 'help' for available commands.\n");
    console_puts("\n");
}

/* ============ Input Handling ============ */

/* Keyboard driver API */
/* Keyboard driver API */
extern int keyboard_has_key(void);
extern uint16_t keyboard_get_key_nb(void);

/* Note: terminal_lookup and command_t declared in shell/terminal.h */

/*
 * Process keyboard input and update terminal
 * Uses keyboard driver's IRQ-driven ring buffer
 * Returns: 1 if command was executed, 0 otherwise
 */
static int shell_process_input(terminal_state_t *term) {
    /* Check if key available in driver's buffer */
    if (!keyboard_has_key()) {
        return 0;
    }
    
    /* Get key from driver (non-blocking, ASCII in low byte) */
    uint16_t keycode = keyboard_get_key_nb();
    if (keycode == 0) {
        return 0;
    }
    
    /* Debug: Log key to serial */
    char c = (char)(keycode & 0xFF);
    serial_puts("[KEY] Got: '");
    if (c >= ' ' && c <= '~') {
        char buf[2] = {c, 0};
        serial_puts(buf);
    } else {
        serial_puts("\\x");
        char hex[3];
        hex[0] = "0123456789ABCDEF"[(c >> 4) & 0xF];
        hex[1] = "0123456789ABCDEF"[c & 0xF];
        hex[2] = '\0';
        serial_puts(hex);
    }
    serial_puts("'\n");
    
    /* Special key (F-keys, arrows, etc) */
    if (keycode & 0x100) {
        /* TODO: Handle arrow keys for history */
        return 0;
    }
    
    /* Handle ESC key */
    if (c == 27) {
        serial_puts("[SHELL] ESC pressed - requesting exit\n");
        shell_should_exit = 1;
        return 1;
    }
    
    /* Handle Enter key */
    if (c == '\n') {
        console_putchar('\n');
        
        /* Execute command */
        term_status_t status = terminal_execute(term);
        
        /* Render output */
        shell_render_output(term);
        
        /* Clear input buffer for next command */
        term->input_pos = 0;
        term->input[0] = '\0';
        
        /* Check for exit */
        if (status == TERM_EXIT) {
            shell_should_exit = 1;
            return 1;
        }
        
        /* Show new prompt */
        shell_render_prompt(term);
        return 1;
    }
    
    /* Handle Backspace */
    if (c == '\b') {
        if (term->input_pos > 0) {
            term->input_pos--;
            term->input[term->input_pos] = '\0';
            console_puts("\b \b");  /* Erase character on screen */
        }
        return 0;
    }
    
    /* Handle Tab (double-tab shows directory listing) */
    if (c == '\t') {
        static uint64_t last_tab_time = 0;
        extern uint64_t pit_get_ticks(void);
        uint64_t now = pit_get_ticks();
        
        /* Double-tab detection (within 500ms / ~50 ticks at 100Hz) */
        if (now - last_tab_time < 50) {
            /* Double-tab: show directory contents */
            console_puts("\n");
            serial_puts("[SHELL] Double-tab: listing directory\n");
            
            /* Execute list command on current directory */
            char *list_argv[] = {"list"};
            term_status_t status = terminal_lookup("list", NULL)->handler(term, 1, list_argv);
            (void)status;
            
            /* Show output */
            shell_render_output(term);
            
            /* Redraw prompt with current input */
            shell_render_prompt(term);
            console_puts(term->input);
            
            last_tab_time = 0;  /* Reset */
        } else {
            last_tab_time = now;
        }
        return 0;
    }
    
    /* Handle printable characters */
    if (c >= ' ' && c <= '~') {
        if (term->input_pos < TERM_INPUT_MAX - 1) {
            term->input[term->input_pos++] = c;
            term->input[term->input_pos] = '\0';
            console_putchar(c);  /* Echo character */
        }
    }
    
    return 0;
}

/* ============ Main Shell Entry Point ============ */

/*
 * terminal_shell_run - Main terminal shell loop
 * 
 * Called from kernel_main after all initialization is complete.
 * This function does not return until the user exits.
 *
 * @fb_addr: Framebuffer physical address
 * @width:   Screen width in pixels
 * @height:  Screen height in pixels
 * @pitch:   Bytes per scanline
 */

/* Serial input functions (from serial.c) */
extern int serial_data_ready(void);
extern int serial_getc_nonblock(void);

/* Process serial input (for headless/QEMU testing) */
static int shell_process_serial(terminal_state_t *term) {
    int c = serial_getc_nonblock();
    if (c < 0) return 0;
    
    /* Echo to serial for feedback */
    extern void serial_putc(char ch);
    serial_putc((char)c);
    
    /* Handle Enter */
    if (c == '\r' || c == '\n') {
        serial_putc('\n');
        console_putchar('\n');
        term_status_t status = terminal_execute(term);
        shell_render_output(term);
        term->input_pos = 0;
        term->input[0] = '\0';
        if (status == TERM_EXIT) {
            shell_should_exit = 1;
            return 1;
        }
        shell_render_prompt(term);
        
        /* Echo prompt to serial */
        extern void serial_puts(const char *s);
        serial_puts(terminal_get_prompt(term));
        return 1;
    }
    
    /* Handle Backspace (0x7F or 0x08) */
    if (c == 0x7F || c == 0x08) {
        if (term->input_pos > 0) {
            term->input_pos--;
            term->input[term->input_pos] = '\0';
            console_puts("\b \b");
            extern void serial_puts(const char *s);
            serial_puts("\b \b");
        }
        return 0;
    }
    
    /* Handle printable characters */
    if (c >= ' ' && c <= '~') {
        if (term->input_pos < TERM_INPUT_MAX - 1) {
            term->input[term->input_pos++] = (char)c;
            term->input[term->input_pos] = '\0';
            console_putchar((char)c);
        }
    }
    
    return 0;
}

void terminal_shell_run(uint64_t fb_addr, uint32_t width, 
                        uint32_t height, uint32_t pitch) {
    serial_puts("[SHELL] Starting NeolyxOS Terminal Shell...\n");
    
    /* Initialize keyboard driver (ensure buffer is ready for IRQs) */
    extern void keyboard_init(void);
    keyboard_init();
    serial_puts("[SHELL] Keyboard driver initialized\n");
    
    /* Initialize console with framebuffer */
    console_init(fb_addr, width, height, pitch);
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
    console_clear();
    
    /* Initialize terminal state */
    terminal_init(&g_terminal);
    shell_should_exit = 0;
    
    /* Show welcome banner */
    shell_show_banner();
    
    /* Show initial prompt */
    shell_render_prompt(&g_terminal);
    
    /* Show prompt on serial too for headless mode */
    serial_puts("\n");
    serial_puts(terminal_get_prompt(&g_terminal));
    
    serial_puts("[SHELL] Terminal ready - both keyboard and serial input enabled\n");
    
    /* Main shell loop - check both keyboard AND serial */
    while (!shell_should_exit && g_terminal.running) {
        /* Check PS/2 keyboard */
        shell_process_input(&g_terminal);
        
        /* Check serial input (for QEMU -serial stdio) */
        shell_process_serial(&g_terminal);
        
        /* Small delay to prevent busy-waiting */
        for (volatile int i = 0; i < 10000; i++);
    }
    
    /* User exited - show goodbye */
    console_set_color(SHELL_SUCCESS_COLOR, SHELL_BG_COLOR);
    console_puts("\nGoodbye!\n");
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
    
    serial_puts("[SHELL] Terminal exited\n");
}

/* ============ Shell Utilities ============ */

/*
 * shell_print_error - Print error message in red
 */
void shell_print_error(const char *msg) {
    console_set_color(SHELL_ERROR_COLOR, SHELL_BG_COLOR);
    console_puts("error: ");
    console_puts(msg);
    console_putchar('\n');
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
}

/*
 * shell_print_success - Print success message in green
 */
void shell_print_success(const char *msg) {
    console_set_color(SHELL_SUCCESS_COLOR, SHELL_BG_COLOR);
    console_puts(msg);
    console_putchar('\n');
    console_set_color(SHELL_FG_COLOR, SHELL_BG_COLOR);
}
