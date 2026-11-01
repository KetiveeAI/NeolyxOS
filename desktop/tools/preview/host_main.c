/*
 * NeolyxOS - Desktop preview harness: entry point and input script
 *
 * Runs the real desktop shell on the development machine and writes
 * selected frames as PNG files, so shell visuals can be iterated on
 * without booting the OS.
 *
 *   preview [-v] [-n frames] [-o out.png] [-s script.txt]
 *
 * Script lines (frame numbers count frame ticks, drawn or skipped, '#' comments):
 *   <frame> move <x> <y>      move the pointer to absolute (x, y)
 *   <frame> down | up         press / release the left button
 *   <frame> rdown | rup       press / release the right button
 *   <frame> key <hex> [up]    PS/2 set-1 scancode press (or release)
 *   <frame> wheel <n>         turn the scroll wheel n notches (> 0 = toward
 *                             the user, i.e. scroll down)
 *   <frame> launch <App_Name> open a startup app window (desktop_launch_app;
 *                             underscores become spaces)
 *   <frame> call <function>   call a global void(void) shell function by name
 *                             (e.g. control_center_toggle) to reach a state
 *   <frame> shot <file.png>   write this frame
 *   <frame> end               write -o (if given) and exit
 *
 * Copyright (c) 2025 KetiveeAI
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "preview.h"

extern int desktop_init(uint64_t fb_addr, uint32_t width, uint32_t height, uint32_t pitch);
extern int desktop_launch_app(const char *name);

typedef enum { ACT_MOVE, ACT_DOWN, ACT_UP, ACT_RDOWN, ACT_RUP, ACT_KEY, ACT_WHEEL, ACT_CALL, ACT_LAUNCH, ACT_SHOT, ACT_END } act_type_t;

typedef struct {
    uint32_t   frame;
    act_type_t type;
    int32_t    x, y;
    uint32_t   key;
    int        key_up;
    char       path[512];
} action_t;

#define MAX_ACTIONS 4096
#define MAX_QUEUE   8192

static action_t g_actions[MAX_ACTIONS];
static int g_nactions;
static int g_next_action;
static input_event_t g_queue[MAX_QUEUE];
static int g_qhead, g_qtail;
static int32_t g_px = PREVIEW_W / 2, g_py = PREVIEW_H / 2;
static uint8_t g_buttons;
static int g_verbose;
static uint32_t g_end_frame = 120;
static const char *g_out_path;

int preview_verbose(void) { return g_verbose; }

/* ============ PNG writer (stored deflate) ============ */

static uint32_t crc_tab[256];

static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_tab[n] = c;
    }
}

static uint32_t crc(uint32_t c, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_tab[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}

static void be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

static int chunk(FILE *f, const char *t, const uint8_t *d, uint32_t n) {
    uint8_t h[8], tl[4];
    be32(h, n); memcpy(h + 4, t, 4);
    uint32_t c = crc(0xFFFFFFFFu, (const uint8_t *)t, 4);
    if (n) c = crc(c, d, n);
    be32(tl, c ^ 0xFFFFFFFFu);
    return (fwrite(h, 1, 8, f) == 8 && (!n || fwrite(d, 1, n, f) == n) && fwrite(tl, 1, 4, f) == 4) ? 0 : -1;
}

static int write_png(const char *path) {
    static int ready;
    if (!ready) { crc_init(); ready = 1; }
    const uint32_t *fb = preview_framebuffer();
    size_t w = PREVIEW_W, h = PREVIEW_H, rl = h * (1 + w * 3);
    uint8_t *raw = malloc(rl);
    size_t zl = 2 + ((rl + 65534) / 65535) * 5 + rl + 4;
    uint8_t *z = malloc(zl);
    if (!raw || !z) { free(raw); free(z); return -1; }
    for (size_t y = 0; y < h; y++) {
        uint8_t *r = raw + y * (1 + w * 3);
        r[0] = 0;
        for (size_t x = 0; x < w; x++) {
            uint32_t p = fb[y * w + x];
            r[1 + x * 3] = (uint8_t)(p >> 16); r[2 + x * 3] = (uint8_t)(p >> 8); r[3 + x * 3] = (uint8_t)p;
        }
    }
    size_t zp = 0;
    uint32_t a1 = 1, a2 = 0;
    z[zp++] = 0x78; z[zp++] = 0x01;
    for (size_t off = 0; off < rl; off += 65535) {
        size_t n = rl - off < 65535 ? rl - off : 65535;
        z[zp++] = (off + n >= rl);
        z[zp++] = (uint8_t)n; z[zp++] = (uint8_t)(n >> 8);
        z[zp++] = (uint8_t)~n; z[zp++] = (uint8_t)(~n >> 8);
        memcpy(z + zp, raw + off, n);
        for (size_t i = 0; i < n; i++) { a1 = (a1 + raw[off + i]) % 65521; a2 = (a2 + a1) % 65521; }
        zp += n;
    }
    be32(z + zp, (a2 << 16) | a1); zp += 4;
    FILE *f = fopen(path, "wb");
    int rc = -1;
    if (f) {
        static const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        uint8_t ih[13];
        be32(ih, (uint32_t)w); be32(ih + 4, (uint32_t)h);
        ih[8] = 8; ih[9] = 2; ih[10] = ih[11] = ih[12] = 0;
        rc = (fwrite(sig, 1, 8, f) == 8 && !chunk(f, "IHDR", ih, 13) && !chunk(f, "IDAT", z, (uint32_t)zp) &&
              !chunk(f, "IEND", NULL, 0)) ? 0 : -1;
        if (fclose(f)) rc = -1;
    }
    free(raw); free(z);
    if (rc == 0) fprintf(stderr, "preview: wrote %s (frame %u)\n", path, preview_frame_count());
    else fprintf(stderr, "preview: FAILED to write %s\n", path);
    return rc;
}

/* ============ Input queue ============ */

static void push(input_event_t ev) {
    int next = (g_qtail + 1) % MAX_QUEUE;
    if (next == g_qhead) { fprintf(stderr, "preview: input queue full\n"); return; }
    g_queue[g_qtail] = ev;
    g_qtail = next;
}

static void emit_mouse(int32_t dx, int32_t dy, uint32_t type) {
    /* Split large moves into int16-sized steps. */
    do {
        input_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.type = type;
        int32_t sx = dx > 120 ? 120 : (dx < -120 ? -120 : dx);
        int32_t sy = dy > 120 ? 120 : (dy < -120 ? -120 : dy);
        ev.mouse.dx = (int16_t)sx;
        ev.mouse.dy = (int16_t)sy;
        ev.mouse.buttons = g_buttons;
        push(ev);
        dx -= sx; dy -= sy;
        type = INPUT_TYPE_MOUSE_MOVE;
    } while (dx != 0 || dy != 0);
}

static void run_action(const action_t *a) {
    switch (a->type) {
        case ACT_MOVE: {
            int32_t tx = a->x < 0 ? 0 : (a->x >= PREVIEW_W ? PREVIEW_W - 1 : a->x);
            int32_t ty = a->y < 0 ? 0 : (a->y >= PREVIEW_H ? PREVIEW_H - 1 : a->y);
            emit_mouse(tx - g_px, ty - g_py, INPUT_TYPE_MOUSE_MOVE);
            g_px = tx; g_py = ty;
            break;
        }
        case ACT_DOWN:  g_buttons |= 1;  emit_mouse(0, 0, INPUT_TYPE_MOUSE_BUTTON); break;
        case ACT_UP:    g_buttons &= ~1; emit_mouse(0, 0, INPUT_TYPE_MOUSE_BUTTON); break;
        case ACT_RDOWN: g_buttons |= 2;  emit_mouse(0, 0, INPUT_TYPE_MOUSE_BUTTON); break;
        case ACT_RUP:   g_buttons &= ~2; emit_mouse(0, 0, INPUT_TYPE_MOUSE_BUTTON); break;
        case ACT_WHEEL: {
            input_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = INPUT_TYPE_MOUSE_MOVE;
            ev.mouse.buttons = g_buttons;
            ev.mouse.wheel = (int8_t)(a->x > 127 ? 127 : (a->x < -127 ? -127 : a->x));
            push(ev);
            break;
        }
        case ACT_KEY: {
            input_event_t ev;
            memset(&ev, 0, sizeof(ev));
            ev.type = INPUT_TYPE_KEYBOARD;
            ev.keyboard.scancode = (uint8_t)a->key;
            ev.keyboard.pressed = a->key_up ? 0 : 1;
            /* PS/2 set 1 -> ASCII for the main block, like the kernel driver */
            static const char map[0x40] = {
                0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0, 0,
                'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0, 0, 'a', 's',
                'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', 0, '\\', 'z', 'x', 'c', 'v',
                'b', 'n', 'm', ',', '.', '/', 0, '*', 0, ' ', 0, 0, 0, 0, 0, 0,
            };
            if (a->key < 0x40) ev.keyboard.ascii = (uint8_t)map[a->key];
            push(ev);
            break;
        }
        case ACT_CALL: {
            /* The preview is linked with -rdynamic so shell symbols resolve. */
            void *sym = dlsym(RTLD_DEFAULT, a->path);
            if (!sym) {
                fprintf(stderr, "preview: call: no function '%s'\n", a->path);
                break;
            }
            void (*fn)(void);
            *(void **)&fn = sym;
            fn();
            break;
        }
        case ACT_LAUNCH: {
            char name[512];
            snprintf(name, sizeof(name), "%s", a->path);
            for (char *q = name; *q; q++) if (*q == '_') *q = ' ';
            int rc = desktop_launch_app(name);
            if (rc != 0) fprintf(stderr, "preview: launch '%s' failed (%d)\n", name, rc);
            break;
        }
        case ACT_SHOT:
            write_png(a->path);
            break;
        case ACT_END:
            if (g_out_path) write_png(g_out_path);
            fprintf(stderr, "preview: %u ticks, %u presents\n", preview_frame_count(), preview_flip_count());
            exit(0);
    }
}

int preview_input_poll(input_event_t *ev, int max, uint32_t frame, uint64_t now_ms) {
    (void)frame;
    if (!ev || max <= 0 || g_qhead == g_qtail) return 0;
    *ev = g_queue[g_qhead];
    ev->timestamp = (uint32_t)now_ms;
    g_qhead = (g_qhead + 1) % MAX_QUEUE;
    return 1;
}

void preview_on_flip(uint32_t frame) {
    while (g_next_action < g_nactions && g_actions[g_next_action].frame <= frame) {
        run_action(&g_actions[g_next_action++]);
    }
    if (frame >= g_end_frame) {
        if (g_out_path) write_png(g_out_path);
        fprintf(stderr, "preview: %u ticks, %u presents\n", frame, preview_flip_count());
        exit(0);
    }
}

/* ============ Script parsing ============ */

static int cmp_action(const void *a, const void *b) {
    const action_t *x = a, *y = b;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return 0;
}

static int load_script(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "preview: cannot open %s\n", path); return -1; }
    char line[1024];
    int lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        unsigned frame;
        char cmd[32], arg1[512] = "", arg2[64] = "";
        int n = sscanf(line, "%u %31s %511s %63s", &frame, cmd, arg1, arg2);
        if (n < 2) continue;
        if (g_nactions >= MAX_ACTIONS) { fprintf(stderr, "preview: too many actions\n"); break; }
        action_t *a = &g_actions[g_nactions];
        memset(a, 0, sizeof(*a));
        a->frame = frame;
        if (!strcmp(cmd, "move") && n >= 4) {
            a->type = ACT_MOVE; a->x = atoi(arg1); a->y = atoi(arg2);
        } else if (!strcmp(cmd, "down")) a->type = ACT_DOWN;
        else if (!strcmp(cmd, "up")) a->type = ACT_UP;
        else if (!strcmp(cmd, "rdown")) a->type = ACT_RDOWN;
        else if (!strcmp(cmd, "rup")) a->type = ACT_RUP;
        else if (!strcmp(cmd, "key") && n >= 3) {
            a->type = ACT_KEY; a->key = (uint32_t)strtoul(arg1, NULL, 16); a->key_up = !strcmp(arg2, "up");
        } else if (!strcmp(cmd, "wheel") && n >= 3) {
            a->type = ACT_WHEEL; a->x = atoi(arg1);
        } else if (!strcmp(cmd, "call") && n >= 3) {
            a->type = ACT_CALL; snprintf(a->path, sizeof(a->path), "%s", arg1);
        } else if (!strcmp(cmd, "launch") && n >= 3) {
            a->type = ACT_LAUNCH; snprintf(a->path, sizeof(a->path), "%s", arg1);
        } else if (!strcmp(cmd, "shot") && n >= 3) {
            a->type = ACT_SHOT; snprintf(a->path, sizeof(a->path), "%s", arg1);
        } else if (!strcmp(cmd, "end")) {
            a->type = ACT_END;
            if (frame < g_end_frame || g_end_frame == 120) g_end_frame = frame + 1;
        } else {
            fprintf(stderr, "preview: %s:%d: bad command\n", path, lineno);
            continue;
        }
        g_nactions++;
    }
    fclose(f);
    qsort(g_actions, (size_t)g_nactions, sizeof(action_t), cmp_action);
    return 0;
}

int main(int argc, char **argv) {
    const char *script = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) g_verbose = 1;
        else if (!strcmp(argv[i], "-n") && i + 1 < argc) g_end_frame = (uint32_t)strtoul(argv[++i], NULL, 10);
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) g_out_path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc) script = argv[++i];
        else {
            fprintf(stderr, "usage: preview [-v] [-n frames] [-o out.png] [-s script.txt]\n");
            return 2;
        }
    }
    if (script && load_script(script) != 0) return 2;
    preview_machine_init();
    desktop_init(0, 0, 0, 0);    /* runs the shell loop; exits from preview_on_flip */
    fprintf(stderr, "preview: desktop_init returned unexpectedly\n");
    return 1;
}
