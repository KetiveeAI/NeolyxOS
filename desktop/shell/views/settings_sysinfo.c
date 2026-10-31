/*
 * NeolyxOS - System Settings: read-only system information
 *
 * Sources, in order of trust:
 *   - CPUID (unprivileged instruction): processor brand, hypervisor bit
 *     and hypervisor vendor
 *   - kernel syscalls that exist today: framebuffer info, monotonic
 *     time, RTC, SYS_INFO (total/free memory)
 * Volume statistics have no kernel interface yet (proposal:
 * docs/desktop/proposals/settings.md); that client returns
 * SETTINGS_ERR_UNSUPPORTED so the UI shows an honest "unavailable"
 * state. The host preview returns demo data, clearly marked, only under
 * NX_HOST_PREVIEW.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "nxsyscall.h"

/* ============ CPUID ============ */

#if defined(__x86_64__) || defined(__i386__)
#define SETTINGS_HAVE_CPUID 1
static void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
    uint32_t ra, rb, rc, rd;
    __asm__ volatile("cpuid" : "=a"(ra), "=b"(rb), "=c"(rc), "=d"(rd) : "a"(leaf), "c"(0));
    *a = ra; *b = rb; *c = rc; *d = rd;
}
#else
#define SETTINGS_HAVE_CPUID 0
#endif

static void put_reg(char *dst, uint32_t r) {
    dst[0] = (char)(r & 0xFF);
    dst[1] = (char)((r >> 8) & 0xFF);
    dst[2] = (char)((r >> 16) & 0xFF);
    dst[3] = (char)((r >> 24) & 0xFF);
}

/* Collapse runs of spaces and trim (brand strings are space padded). */
static void tidy(char *s) {
    int r = 0, w = 0, sp = 1;
    for (; s[r]; r++) {
        char ch = s[r];
        if (ch < 0x20 || ch > 0x7E) ch = ' ';
        if (ch == ' ') {
            if (sp) continue;
            sp = 1;
        } else {
            sp = 0;
        }
        s[w++] = ch;
    }
    while (w > 0 && s[w - 1] == ' ') w--;
    s[w] = '\0';
}

int settings_sys_cpu_brand(char *out, int cap) {
    if (!out || cap < 2) return -1;
    out[0] = '\0';
#if SETTINGS_HAVE_CPUID
    uint32_t a, b, c, d;
    cpuid(0x80000000u, &a, &b, &c, &d);
    if (a < 0x80000004u) {
        /* No brand string: fall back to the vendor id. */
        char v[13];
        cpuid(0, &a, &b, &c, &d);
        put_reg(v, b); put_reg(v + 4, d); put_reg(v + 8, c);
        v[12] = '\0';
        sx_strcpy(out, v, cap);
        tidy(out);
        return out[0] ? 0 : -1;
    }
    char brand[49];
    for (uint32_t i = 0; i < 3; i++) {
        cpuid(0x80000002u + i, &a, &b, &c, &d);
        put_reg(brand + i * 16, a);
        put_reg(brand + i * 16 + 4, b);
        put_reg(brand + i * 16 + 8, c);
        put_reg(brand + i * 16 + 12, d);
    }
    brand[48] = '\0';
    tidy(brand);
    sx_strcpy(out, brand, cap);
    return out[0] ? 0 : -1;
#else
    return SETTINGS_ERR_UNSUPPORTED;
#endif
}

int settings_sys_hypervisor(char *out, int cap) {
    if (!out || cap < 2) return -1;
    out[0] = '\0';
#if SETTINGS_HAVE_CPUID
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    if (!(c & (1u << 31))) return 1;          /* hypervisor bit clear: bare metal */
    char v[13];
    cpuid(0x40000000u, &a, &b, &c, &d);
    put_reg(v, b); put_reg(v + 4, c); put_reg(v + 8, d);
    v[12] = '\0';
    const char *name = "Virtual machine";
    if (sx_streq(v, "TCGTCGTCGTCG")) name = "QEMU (TCG)";
    else if (sx_streq(v, "KVMKVMKVM")) name = "KVM";
    else if (sx_streq(v, "VMwareVMware")) name = "VMware";
    else if (sx_streq(v, "VBoxVBoxVBox")) name = "VirtualBox";
    else if (sx_streq(v, "Microsoft Hv")) name = "Hyper-V";
    else if (sx_streq(v, " lrpepyh  vr") || sx_streq(v, "bhyve bhyve ")) name = "bhyve / Parallels";
    else if (sx_streq(v, "XenVMMXenVMM")) name = "Xen";
    else if (sx_streq(v, "ACRNACRNACRN")) name = "ACRN";
    else if (sx_streq(v, "Apple VZ")) name = "Apple Virtualization";
    sx_strcpy(out, name, cap);
    return 0;
#else
    return SETTINGS_ERR_UNSUPPORTED;
#endif
}

/* ============ Kernel-backed ============ */

int settings_sys_display(uint32_t *w, uint32_t *h, uint32_t *bpp) {
    if (!w || !h || !bpp) return -1;
    fb_info_t info;
    info.width = info.height = info.bpp = 0;
    int r = nx_fb_info(&info);
    if (r != 0 || info.width == 0 || info.height == 0) {
        /* Fall back to the canvas the shell renders into. */
        int32_t cw = nxui_width(), ch = nxui_height();
        if (cw <= 0 || ch <= 0) return -1;
        *w = (uint32_t)cw; *h = (uint32_t)ch; *bpp = 32;
        return 0;
    }
    *w = info.width; *h = info.height; *bpp = info.bpp ? info.bpp : 32;
    return 0;
}

uint64_t settings_sys_uptime_ms(void) {
    return nx_gettime();
}

int settings_sys_clock(int *year, int *month, int *day, int *hour, int *min, int *sec, int *wday) {
    if (!year || !month || !day || !hour || !min || !sec || !wday) return -1;
    rtc_time_t t;
    t.year = 0;
    if (nx_rtc_get(&t) != 0) return -1;
    if (t.year < 1970 || t.month < 1 || t.month > 12 || t.day < 1 || t.day > 31 ||
        t.hour > 23 || t.minute > 59 || t.second > 60 || t.weekday > 6) return -1;
    *year = t.year; *month = t.month; *day = t.day;
    *hour = t.hour; *min = t.minute; *sec = t.second; *wday = t.weekday;
    return 0;
}

/* ============ Not yet exposed by the kernel ============ */

/* Mirror of the kernel's sysinfo_t (kernel/include/core/syscall.h) for
 * SYS_INFO (50). MUST MATCH the kernel layout. */
#define SETTINGS_SYS_INFO 50
typedef struct {
    char     os_name[32];
    char     os_version[32];
    char     arch[16];
    uint64_t uptime_ms;
    uint64_t total_memory;     /* bytes */
    uint64_t free_memory;      /* bytes */
    uint32_t num_cpus;
    uint32_t num_processes;
} settings_kernel_sysinfo_t;
_Static_assert(sizeof(settings_kernel_sysinfo_t) == 112, "sysinfo_t layout changed");

int settings_sys_memory(uint64_t *total_bytes, uint64_t *used_bytes) {
    if (!total_bytes || !used_bytes) return -1;
    *total_bytes = *used_bytes = 0;
    settings_kernel_sysinfo_t info;
    for (unsigned i = 0; i < sizeof(info); i++) ((volatile uint8_t *)&info)[i] = 0;
    int64_t r = syscall1(SETTINGS_SYS_INFO, (int64_t)(uintptr_t)&info);
    if (r == 0 && info.total_memory > 0 && info.free_memory <= info.total_memory) {
        *total_bytes = info.total_memory;
        *used_bytes = info.total_memory - info.free_memory;
        return 0;
    }
#ifdef NX_HOST_PREVIEW
    /* PREVIEW DEMO DATA - not real: the preview harness does not implement SYS_INFO. */
    *total_bytes = 8ull * 1024 * 1024 * 1024;
    *used_bytes  = 3ull * 1024 * 1024 * 1024 + 412ull * 1024 * 1024;
    return 0;
#else
    return r < 0 ? (int)r : -1;
#endif
}

int settings_sys_volume(const char **name, uint64_t *total_bytes, uint64_t *used_bytes) {
    if (!name || !total_bytes || !used_bytes) return -1;
#ifdef NX_HOST_PREVIEW
    /* PREVIEW DEMO DATA - not real: the kernel has no volume-statistics syscall yet. */
    *name = "Neolyx HD (preview demo)";
    *total_bytes = 256ull * 1000 * 1000 * 1000;
    *used_bytes  = 61ull * 1000 * 1000 * 1000 + 400ull * 1000 * 1000;
    return 0;
#else
    *name = 0;
    *total_bytes = *used_bytes = 0;
    return SETTINGS_ERR_UNSUPPORTED;
#endif
}

void settings_format_bytes(uint64_t bytes, int decimal, char *out, int cap) {
    if (!out || cap <= 0) return;
    static const char *const units[] = { "bytes", "KB", "MB", "GB", "TB" };
    int u = 0;
    uint64_t whole = bytes, frac = 0;   /* one decimal */
    uint64_t base = decimal ? 1000u : 1024u;
    while (whole >= base && u < 4) {
        frac = (whole % base) * 10u / base;
        whole /= base;
        u++;
    }
    char num[24];
    sx_itoa((int64_t)whole, out, cap);
    if (u > 0 && whole < 100) {
        sx_strcat(out, ".", cap);
        sx_strcat(out, sx_itoa((int64_t)frac, num, (int)sizeof(num)), cap);
    }
    sx_strcat(out, " ", cap);
    sx_strcat(out, units[u], cap);
}
