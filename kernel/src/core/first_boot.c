/*
 * NeolyxOS First Boot Wizard Implementation
 * 
 * Runs once after installation to create user account and configure system.
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#include "core/first_boot.h"
#include "config/nlc_config.h"
#include "fs/vfs.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint32_t fb_width, fb_height;
extern volatile uint32_t *framebuffer;
extern uint8_t kbd_wait_key(void);
extern const uint8_t font_basic[95][16];

/* ============ Colors ============ */

#define COLOR_BG            0x0F0F1A
#define COLOR_PANEL         0x16213E
#define COLOR_PRIMARY       0x0F3460
#define COLOR_ACCENT        0xE94560
#define COLOR_TEXT          0xFFFFFF
#define COLOR_DIM           0x888888
#define COLOR_SUCCESS       0x4CAF50
#define COLOR_INPUT_BG      0x1A1A2E
#define COLOR_CURSOR        0x00D4F8

/* ============ State ============ */

static first_boot_state_t current_state = FIRST_BOOT_LANGUAGE;
static user_account_t current_user;
static system_prefs_t current_prefs;

/* ============ Drawing Helpers ============ */

static void draw_rect(int x, int y, int w, int h, uint32_t color) {
    for (int j = y; j < y + h && j < (int)fb_height; j++) {
        if (j < 0) continue;
        for (int i = x; i < x + w && i < (int)fb_width; i++) {
            if (i >= 0) framebuffer[j * fb_width + i] = color;
        }
    }
}

static void draw_border(int x, int y, int w, int h, uint32_t color) {
    draw_rect(x, y, w, 2, color);
    draw_rect(x, y + h - 2, w, 2, color);
    draw_rect(x, y, 2, h, color);
    draw_rect(x + w - 2, y, 2, h, color);
}

static void draw_char(int x, int y, char c, uint32_t color) {
    if (c < 32 || c > 126) c = '?';
    const uint8_t *glyph = font_basic[c - 32];
    
    for (int cy = 0; cy < 16; cy++) {
        uint8_t row = glyph[cy];
        for (int cx = 0; cx < 8; cx++) {
            if (row & (0x80 >> cx)) {
                int px = x + cx;
                int py = y + cy;
                if (px < fb_width && py < fb_height) {
                    framebuffer[py * fb_width + px] = color;
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *text, uint32_t color) {
    while (*text) {
        draw_char(x, y, *text, color);
        x += 9;
        text++;
    }
}

static void draw_centered(int y, const char *text, uint32_t color) {
    int len = 0;
    const char *p = text;
    while (*p++) len++;
    int x = (fb_width - len * 9) / 2;
    draw_text(x, y, text, color);
}

static void clear_screen(void) {
    draw_rect(0, 0, fb_width, fb_height, COLOR_BG);
}

/* Simple string copy */
static void str_copy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Simple string length */
static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

/* ============ Text Input Field ============ */

/* Returns the edited string, waits for Enter */
static void text_input(int x, int y, int width, char *buffer, int max_len, int is_password) {
    int cursor = str_len(buffer);
    int redraw = 1;
    
    while (1) {
        if (redraw) {
            /* Draw input field */
            draw_rect(x, y, width, 28, COLOR_INPUT_BG);
            draw_border(x, y, width, 28, COLOR_PRIMARY);
            
            /* Draw text (masked if password) */
            int tx = x + 8;
            for (int i = 0; buffer[i] && i < max_len; i++) {
                draw_char(tx, y + 6, is_password ? '*' : buffer[i], COLOR_TEXT);
                tx += 9;
            }
            
            /* Draw cursor */
            draw_rect(x + 8 + cursor * 9, y + 4, 2, 20, COLOR_CURSOR);
            redraw = 0;
        }
        
        uint8_t key = kbd_wait_key();
        if (key & 0x80) continue;  /* Key release */
        
        /* Enter key */
        if (key == 0x1C) {
            break;
        }
        
        /* Backspace */
        if (key == 0x0E && cursor > 0) {
            cursor--;
            buffer[cursor] = '\0';
            redraw = 1;
            continue;
        }
        
        /* Map scan codes to ASCII (simplified) */
        char c = 0;
        if (key >= 0x10 && key <= 0x19) {  /* q-p row */
            const char row1[] = "qwertyuiop";
            c = row1[key - 0x10];
        } else if (key >= 0x1E && key <= 0x26) {  /* a-l row */
            const char row2[] = "asdfghjkl";
            c = row2[key - 0x1E];
        } else if (key >= 0x2C && key <= 0x32) {  /* z-m row */
            const char row3[] = "zxcvbnm";
            c = row3[key - 0x2C];
        } else if (key >= 0x02 && key <= 0x0B) {  /* 1-0 row */
            const char nums[] = "1234567890";
            c = nums[key - 0x02];
        } else if (key == 0x39) {  /* Space */
            c = ' ';
        } else if (key == 0x34) {  /* Period */
            c = '.';
        } else if (key == 0x0C) {  /* Minus */
            c = '-';
        } else if (key == 0x35) {  /* Slash */
            c = '/';
        }
        
        if (c && cursor < max_len - 1) {
            buffer[cursor++] = c;
            buffer[cursor] = '\0';
            redraw = 1;
        }
    }
}

/* ============ Screen: Language Selection ============ */

static void screen_language(void) {
    clear_screen();
    
    /* Header */
    draw_centered(60, "Welcome to NeolyxOS", COLOR_TEXT);
    draw_centered(90, "Choose your language", COLOR_DIM);
    
    /* Language options */
    const char *languages[] = {"English (US)", "English (UK)", "Espanol", "Francais", "Deutsch"};
    const char *codes[] = {"en_US", "en_GB", "es_ES", "fr_FR", "de_DE"};
    int selected = 0;
    int opt_w = 300;
    int opt_h = 40;
    int opt_x = (fb_width - opt_w) / 2;
    int opt_y = 150;
    
    while (1) {
        /* Draw options */
        for (int i = 0; i < 5; i++) {
            int y = opt_y + i * (opt_h + 10);
            uint32_t bg = (i == selected) ? COLOR_PRIMARY : COLOR_PANEL;
            draw_rect(opt_x, y, opt_w, opt_h, bg);
            if (i == selected) draw_border(opt_x, y, opt_w, opt_h, COLOR_ACCENT);
            draw_text(opt_x + 20, y + 12, languages[i], COLOR_TEXT);
        }
        
        draw_centered(fb_height - 40, "UP/DOWN to select, ENTER to continue", COLOR_DIM);
        
        uint8_t key = kbd_wait_key();
        if (key & 0x80) continue;
        
        if (key == 0x48 && selected > 0) selected--;
        else if (key == 0x50 && selected < 4) selected++;
        else if (key == 0x1C) {
            str_copy(current_prefs.language, codes[selected], 8);
            break;
        }
    }
}

/* ============ Screen: Hostname Configuration ============ */

static void screen_hostname(void) {
    clear_screen();
    
    draw_centered(80, "Name Your Computer", COLOR_TEXT);
    draw_centered(110, "This name will identify your computer on the network", COLOR_DIM);
    
    /* Default hostname */
    str_copy(current_prefs.hostname, "neolyx", 64);
    
    int input_w = 400;
    int input_x = (fb_width - input_w) / 2;
    int input_y = fb_height / 2 - 40;
    
    draw_text(input_x, input_y - 30, "Computer Name:", COLOR_TEXT);
    text_input(input_x, input_y, input_w, current_prefs.hostname, 64, 0);
    
    serial_puts("[FIRST BOOT] Hostname: ");
    serial_puts(current_prefs.hostname);
    serial_puts("\n");
}

/* ============ Screen: Create User Account ============ */

static void screen_create_user(void) {
    clear_screen();
    
    draw_centered(60, "Create Your Account", COLOR_TEXT);
    draw_centered(90, "This will be your administrator account", COLOR_DIM);
    
    /* Initialize user */
    current_user.is_admin = 1;
    current_user.auto_login = 0;
    str_copy(current_user.username, "", 64);
    str_copy(current_user.display_name, "", 128);
    
    int input_w = 400;
    int input_x = (fb_width - input_w) / 2;
    int input_y = 180;
    int field_gap = 70;
    
    /* Full Name */
    draw_text(input_x, input_y, "Full Name:", COLOR_TEXT);
    text_input(input_x, input_y + 20, input_w, current_user.display_name, 128, 0);
    
    /* Username */
    draw_text(input_x, input_y + field_gap, "Username:", COLOR_TEXT);
    text_input(input_x, input_y + field_gap + 20, input_w, current_user.username, 64, 0);
    
    serial_puts("[FIRST BOOT] User: ");
    serial_puts(current_user.username);
    serial_puts(" (");
    serial_puts(current_user.display_name);
    serial_puts(")\n");
}

/* ============ Screen: Set Password ============ */

static void screen_password(void) {
    clear_screen();
    
    draw_centered(80, "Create a Password", COLOR_TEXT);
    draw_centered(110, "This password will be used to log in", COLOR_DIM);
    
    char password1[64] = "";
    char password2[64] = "";
    
    int input_w = 400;
    int input_x = (fb_width - input_w) / 2;
    int input_y = fb_height / 2 - 80;
    
    while (1) {
        /* Password field */
        draw_rect(0, input_y - 10, fb_width, 160, COLOR_BG);
        draw_text(input_x, input_y, "Password:", COLOR_TEXT);
        text_input(input_x, input_y + 20, input_w, password1, 64, 1);
        
        /* Confirm password */
        draw_text(input_x, input_y + 70, "Confirm Password:", COLOR_TEXT);
        text_input(input_x, input_y + 90, input_w, password2, 64, 1);
        
        /* Check match */
        int match = 1;
        for (int i = 0; password1[i] || password2[i]; i++) {
            if (password1[i] != password2[i]) { match = 0; break; }
        }
        
        if (match && str_len(password1) > 0) {
            /* Simple "hash" (placeholder - just copy for now) */
            str_copy(current_user.password_hash, password1, 128);
            break;
        } else {
            draw_centered(input_y + 140, "Passwords don't match. Try again.", COLOR_ACCENT);
        }
    }
    
    serial_puts("[FIRST BOOT] Password set\n");
}

/* ============ Screen: Appearance ============ */

static void screen_appearance(void) {
    clear_screen();
    
    draw_centered(80, "Choose Your Look", COLOR_TEXT);
    
    int selected = 0;  /* 0 = Light, 1 = Dark */
    int opt_w = 200;
    int opt_h = 150;
    int gap = 40;
    int start_x = (fb_width - opt_w * 2 - gap) / 2;
    int opt_y = fb_height / 2 - 80;
    
    while (1) {
        /* Light theme preview */
        draw_rect(start_x, opt_y, opt_w, opt_h, 0xF0F0F0);
        draw_border(start_x, opt_y, opt_w, opt_h, (selected == 0) ? COLOR_ACCENT : COLOR_DIM);
        draw_rect(start_x + 20, opt_y + 20, opt_w - 40, 30, 0xFFFFFF);
        draw_rect(start_x + 20, opt_y + 60, opt_w - 40, 60, 0xE0E0E0);
        draw_text(start_x + 70, opt_y + opt_h + 10, "Light", COLOR_TEXT);
        
        /* Dark theme preview */
        int dark_x = start_x + opt_w + gap;
        draw_rect(dark_x, opt_y, opt_w, opt_h, 0x1A1A2E);
        draw_border(dark_x, opt_y, opt_w, opt_h, (selected == 1) ? COLOR_ACCENT : COLOR_DIM);
        draw_rect(dark_x + 20, opt_y + 20, opt_w - 40, 30, 0x252545);
        draw_rect(dark_x + 20, opt_y + 60, opt_w - 40, 60, 0x16213E);
        draw_text(dark_x + 75, opt_y + opt_h + 10, "Dark", COLOR_TEXT);
        
        draw_centered(fb_height - 40, "LEFT/RIGHT to select, ENTER to continue", COLOR_DIM);
        
        uint8_t key = kbd_wait_key();
        if (key & 0x80) continue;
        
        if (key == 0x4B && selected > 0) selected--;  /* Left */
        else if (key == 0x4D && selected < 1) selected++;  /* Right */
        else if (key == 0x1C) {
            current_prefs.dark_mode = selected;
            break;
        }
    }
}

/* ============ Screen: Complete ============ */

static void screen_first_boot_complete(void) {
    clear_screen();
    
    /* Success circle */
    int cx = fb_width / 2;
    int cy = fb_height / 3;
    
    for (int r = 5; r <= 50; r += 5) {
        for (int y = -r; y <= r; y++) {
            for (int x = -r; x <= r; x++) {
                if (x*x + y*y <= r*r) {
                    int px = cx + x;
                    int py = cy + y;
                    if (px >= 0 && px < (int)fb_width && py >= 0 && py < (int)fb_height) {
                        framebuffer[py * fb_width + px] = COLOR_SUCCESS;
                    }
                }
            }
        }
        for (volatile int d = 0; d < 2000000; d++);
    }
    
    draw_centered(cy + 80, "Setup Complete!", COLOR_TEXT);
    draw_centered(cy + 110, "Welcome,", COLOR_DIM);
    draw_centered(cy + 140, current_user.display_name, COLOR_ACCENT);
    
    for (volatile int d = 0; d < 30000000; d++);
    
    draw_centered(fb_height - 60, "Starting NeolyxOS...", COLOR_DIM);
    
    for (volatile int d = 0; d < 20000000; d++);
}

/* ============ Check If First Boot Needed ============ */

int first_boot_needed(void) {
    /* Check boot.nlc for first_boot flag */
    vfs_file_t *cfg = vfs_open("/EFI/BOOT/config/boot.nlc", 0x0001 /* O_RDONLY */);
    if (!cfg) {
        serial_puts("[FIRST BOOT] No boot.nlc - skipping first boot\n");
        return 0;  /* No config = not installed properly */
    }
    
    /* Read file and check for first_boot=true */
    char buffer[4096];
    int len = vfs_read(cfg, buffer, 4095);
    vfs_close(cfg);
    
    if (len <= 0) return 0;
    buffer[len] = '\0';
    
    /* Simple search for "first_boot" */
    for (int i = 0; i < len - 10; i++) {
        if (buffer[i] == 'f' && buffer[i+1] == 'i' && buffer[i+2] == 'r' &&
            buffer[i+3] == 's' && buffer[i+4] == 't' && buffer[i+5] == '_' &&
            buffer[i+6] == 'b' && buffer[i+7] == 'o' && buffer[i+8] == 'o' &&
            buffer[i+9] == 't') {
            /* Found "first_boot" - check if value is "true" */
            for (int j = i + 10; j < len - 4; j++) {
                if (buffer[j] == 't' && buffer[j+1] == 'r' && 
                    buffer[j+2] == 'u' && buffer[j+3] == 'e') {
                    serial_puts("[FIRST BOOT] First boot flag is true\n");
                    return 1;
                }
                if (buffer[j] == '\n') break;
            }
        }
    }
    
    serial_puts("[FIRST BOOT] First boot already completed\n");
    return 0;
}

/* ============ Run First Boot Wizard ============ */

int first_boot_run_wizard(void) {
    serial_puts("[FIRST BOOT] Starting first boot wizard...\n");
    
    current_state = FIRST_BOOT_LANGUAGE;
    
    while (1) {
        switch (current_state) {
            case FIRST_BOOT_LANGUAGE:
                screen_language();
                current_state = FIRST_BOOT_HOSTNAME;
                break;
                
            case FIRST_BOOT_HOSTNAME:
                screen_hostname();
                current_state = FIRST_BOOT_CREATE_USER;
                break;
                
            case FIRST_BOOT_CREATE_USER:
                screen_create_user();
                current_state = FIRST_BOOT_PASSWORD;
                break;
                
            case FIRST_BOOT_PASSWORD:
                screen_password();
                current_state = FIRST_BOOT_APPEARANCE;
                break;
                
            case FIRST_BOOT_APPEARANCE:
                screen_appearance();
                current_state = FIRST_BOOT_COMPLETE;
                break;
                
            case FIRST_BOOT_COMPLETE:
                /* Save user account */
                first_boot_create_user(&current_user);
                
                /* Save preferences */
                first_boot_save_prefs(&current_prefs);
                
                /* Mark complete */
                first_boot_mark_complete();
                
                /* Show completion screen */
                screen_first_boot_complete();
                
                serial_puts("[FIRST BOOT] Wizard complete\n");
                return 0;
        }
    }
}

/* ============ Create User Directory ============ */

int first_boot_create_user(user_account_t *user) {
    serial_puts("[FIRST BOOT] Creating user: ");
    serial_puts(user->username);
    serial_puts("\n");
    
    /* Create /Users directory */
    vfs_create("/Users", VFS_DIRECTORY, 0755);
    
    /* Create user home directory */
    char home_path[128] = "/Users/";
    int i = 7;
    for (int j = 0; user->username[j] && i < 120; j++) {
        home_path[i++] = user->username[j];
    }
    home_path[i] = '\0';
    
    vfs_create(home_path, VFS_DIRECTORY, 0755);
    
    /* Create subdirectories */
    char sub_path[160];
    const char *subdirs[] = {"Desktop", "Documents", "Downloads", "Pictures", "Music", "Movies"};
    for (int d = 0; d < 6; d++) {
        int k = 0;
        for (int j = 0; home_path[j]; j++) sub_path[k++] = home_path[j];
        sub_path[k++] = '/';
        for (int j = 0; subdirs[d][j]; j++) sub_path[k++] = subdirs[d][j];
        sub_path[k] = '\0';
        vfs_create(sub_path, VFS_DIRECTORY, 0755);
    }
    
    /* Write user info to /etc/passwd (simplified format) */
    vfs_create("/etc", VFS_DIRECTORY, 0755);
    vfs_file_t *passwd = vfs_open("/etc/passwd", VFS_O_WRONLY | VFS_O_CREATE);
    if (passwd) {
        /* username:x:1000:1000:Display Name:/Users/username:/bin/nxsh */
        vfs_write(passwd, user->username, str_len(user->username));
        vfs_write(passwd, ":x:1000:1000:", 13);
        vfs_write(passwd, user->display_name, str_len(user->display_name));
        vfs_write(passwd, ":", 1);
        vfs_write(passwd, home_path, str_len(home_path));
        vfs_write(passwd, ":/bin/nxsh\n", 11);
        vfs_close(passwd);
    }
    
    return 0;
}

/* ============ Save System Preferences ============ */

int first_boot_save_prefs(system_prefs_t *prefs) {
    serial_puts("[FIRST BOOT] Saving preferences\n");
    
    vfs_create("/etc", VFS_DIRECTORY, 0755);
    vfs_file_t *pref_file = vfs_open("/etc/neolyx.conf", VFS_O_WRONLY | VFS_O_CREATE);
    if (pref_file) {
        vfs_write(pref_file, "# NeolyxOS System Configuration\n", 33);
        
        vfs_write(pref_file, "hostname = ", 11);
        vfs_write(pref_file, prefs->hostname, str_len(prefs->hostname));
        vfs_write(pref_file, "\n", 1);
        
        vfs_write(pref_file, "language = ", 11);
        vfs_write(pref_file, prefs->language, str_len(prefs->language));
        vfs_write(pref_file, "\n", 1);
        
        vfs_write(pref_file, "dark_mode = ", 12);
        vfs_write(pref_file, prefs->dark_mode ? "true" : "false", prefs->dark_mode ? 4 : 5);
        vfs_write(pref_file, "\n", 1);
        
        vfs_close(pref_file);
    }
    
    return 0;
}

/* ============ Mark First Boot Complete ============ */

int first_boot_mark_complete(void) {
    serial_puts("[FIRST BOOT] Marking first boot complete\n");
    
    /* Update boot.nlc to set first_boot=false */
    static nlc_config_t boot_cfg;
    for (int i = 0; i < sizeof(nlc_config_t); i++) ((char*)&boot_cfg)[i] = 0;
    
    const char *cfg_path = "/EFI/BOOT/config/boot.nlc";
    for (int i = 0; cfg_path[i] && i < 255; i++) {
        boot_cfg.filepath[i] = cfg_path[i];
    }
    
    /* Load existing config */
    nlc_load(&boot_cfg);
    
    /* Update first_boot flag */
    nlc_set(&boot_cfg, "installation", "first_boot", "false");
    
    /* Save */
    nlc_save(&boot_cfg);
    
    return 0;
}
