/*
 * NeolyxOS - System Settings: system panes
 *
 * About This System, General, Software Update, Storage, Date & Time, Privacy &
 * Security, Users & Accounts, Developer.
 *
 * Panes show live values where the system has an interface and say so
 * plainly where it does not. Preferences go through settings_prefs.c
 * (validated and audited).
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "settings_internal.h"
#include "control_center.h"
#include "config/nlc_config.h"

/* ============ Formatting helpers ============ */

static void fmt_uptime(uint64_t ms, char *out, int cap) {
    uint64_t mins = ms / 60000u;
    uint64_t days = mins / (60u * 24u), hours = (mins / 60u) % 24u, m = mins % 60u;
    char n[24];
    out[0] = '\0';
    if (days) { sx_strcat(out, sx_itoa((int64_t)days, n, 24), cap); sx_strcat(out, days == 1 ? " day, " : " days, ", cap); }
    if (days || hours) { sx_strcat(out, sx_itoa((int64_t)hours, n, 24), cap); sx_strcat(out, " h ", cap); }
    sx_strcat(out, sx_itoa((int64_t)m, n, 24), cap);
    sx_strcat(out, " min", cap);
}

static void two_digits(int v, char *out) {
    out[0] = (char)('0' + (v / 10) % 10);
    out[1] = (char)('0' + v % 10);
    out[2] = '\0';
}

/* ============ About This System ============ */

static void fmt_display(char *out, int cap) {
    char n[16];
    uint32_t w = 0, h = 0, bpp = 0;
    if (settings_sys_display(&w, &h, &bpp) != 0) { sx_strcpy(out, "Unavailable", cap); return; }
    sx_itoa(w, out, cap);
    sx_strcat(out, " x ", cap);
    sx_strcat(out, sx_itoa(h, n, (int)sizeof(n)), cap);
    sx_strcat(out, ", ", cap);
    sx_strcat(out, sx_itoa(bpp, n, (int)sizeof(n)), cap);
    sx_strcat(out, "-bit color", cap);
}

/*
 * Everything about this computer in one place. Values come from the
 * kernel (SYS_INFO, abi.query, framebuffer info), the processor (CPUID)
 * and the build; anything the system cannot report is labelled as such
 * instead of being guessed.
 */
static void build_about(sui_t *ui) {
    char ver[48], cpu[64], feat[96], topo[48], hv[40], disp[48], up[48], n[16], a[24], b[24], cap[80];
    sui_header(ui, "about", "About This System",
               "Your computer's software, processor, memory, display and storage, read live from the system.");

    /* ---- Software ---- */
    settings_kernel_info_t k;
    int k_ok = settings_sys_kernel(&k) == 0;
    sx_join3(ver, (int)sizeof(ver), NEOLYX_VERSION, " (", NEOLYX_CODENAME);
    sx_strcat(ver, ")", (int)sizeof(ver));
    sui_section(ui, "Software");
    sui_card_begin(ui);
    sui_info(ui, "Operating system", NEOLYX_OS_NAME);
    sui_info(ui, "Version", ver);
    sui_info(ui, "Kernel", k_ok && k.os_version[0] ? k.os_version : "Not reported");
    if (k.abi_ok) {
        char abi[48];
        sx_itoa(k.abi_major, abi, (int)sizeof(abi));
        sx_strcat(abi, ".", (int)sizeof(abi));
        sx_strcat(abi, sx_itoa(k.abi_minor, n, (int)sizeof(n)), (int)sizeof(abi));
        sx_strcat(abi, ".", (int)sizeof(abi));
        sx_strcat(abi, sx_itoa(k.abi_patch, n, (int)sizeof(n)), (int)sizeof(abi));
        sx_strcat(abi, ", ", (int)sizeof(abi));
        sx_strcat(abi, sx_itoa((int64_t)k.syscall_count, n, (int)sizeof(n)), (int)sizeof(abi));
        sx_strcat(abi, " system calls", (int)sizeof(abi));
        sui_info(ui, "Kernel interface", abi);
    } else {
        sui_info(ui, "Kernel interface", "Not reported");
    }
    sui_info(ui, "Desktop", "Vajra shell");
    sui_info(ui, "Architecture", k_ok && k.arch[0] ? k.arch : NEOLYX_ARCH);
    fmt_uptime(settings_sys_uptime_ms(), up, (int)sizeof(up));
    sui_info(ui, "Uptime", up);
    sui_card_end(ui);

    /* ---- Processor ---- */
    if (settings_sys_cpu_brand(cpu, (int)sizeof(cpu)) != 0) sx_strcpy(cpu, "Unavailable", (int)sizeof(cpu));
    uint32_t lp = 0, cores = 0;
    if (settings_sys_cpu_topology(&lp, &cores) == 0 && lp > 0) {
        sx_itoa(cores, topo, (int)sizeof(topo));
        sx_strcat(topo, cores == 1 ? " core, " : " cores, ", (int)sizeof(topo));
        sx_strcat(topo, sx_itoa(lp, n, (int)sizeof(n)), (int)sizeof(topo));
        sx_strcat(topo, lp == 1 ? " thread" : " threads", (int)sizeof(topo));
    } else {
        sx_strcpy(topo, "Not reported", (int)sizeof(topo));
    }
    if (settings_sys_cpu_features(feat, (int)sizeof(feat)) != 0) sx_strcpy(feat, "Not reported", (int)sizeof(feat));
    int hvr = settings_sys_hypervisor(hv, (int)sizeof(hv));
    if (hvr == 1) sx_strcpy(hv, "Physical hardware", (int)sizeof(hv));
    else if (hvr < 0) sx_strcpy(hv, "Unknown", (int)sizeof(hv));
    sui_section(ui, "Processor");
    sui_card_begin(ui);
    sui_info(ui, "Model", cpu);
    sui_info(ui, "Cores per processor", topo);
    if (k_ok && k.num_cpus > 0) sui_info(ui, "Processors in use", sx_itoa(k.num_cpus, n, (int)sizeof(n)));
    sui_info(ui, "Extensions", feat);
    sui_info(ui, "Platform", hv);
    sui_card_end(ui);

    /* ---- Memory ---- */
    uint64_t mt = 0, mu = 0;
    int mem_ok = settings_sys_memory(&mt, &mu) == 0 && mt > 0;
    sui_section(ui, "Memory");
    sui_card_begin(ui);
    if (mem_ok) {
        settings_format_bytes(mt, 0, b, (int)sizeof(b));
        sui_info(ui, "Installed", b);
        settings_format_bytes(mu, 0, a, (int)sizeof(a));
        sx_join3(cap, (int)sizeof(cap), a, " of ", b);
        sx_strcat(cap, " in use", (int)sizeof(cap));
        sui_meter(ui, "In use", cap, (float)((double)mu / (double)mt));
        settings_format_bytes(mt - mu, 0, a, (int)sizeof(a));
        sui_info(ui, "Available", a);
    } else {
        sui_info(ui, "Installed", "Not reported");
    }
    sui_card_end(ui);
#ifdef NX_HOST_PREVIEW
    if (mem_ok) sui_note(ui, "Preview build: the memory figures above are demo data.");
#else
    if (!mem_ok) sui_note(ui, "The kernel did not report memory size.");
#endif

    /* ---- Graphics and display ---- */
    fmt_display(disp, (int)sizeof(disp));
    sui_section(ui, "Display");
    sui_card_begin(ui);
    sui_info(ui, "Resolution", disp);
    sui_info(ui, "Output", "UEFI framebuffer (GOP)");
    sui_card_end(ui);

    /* ---- Storage ---- */
    const char *vname = 0;
    uint64_t vt = 0, vu = 0;
    int vol_ok = settings_sys_volume(&vname, &vt, &vu) == 0 && vt > 0;
    sui_section(ui, "Storage");
    sui_card_begin(ui);
    if (vol_ok) {
        settings_format_bytes(vu, 1, a, (int)sizeof(a));
        settings_format_bytes(vt, 1, b, (int)sizeof(b));
        sx_join3(cap, (int)sizeof(cap), a, " of ", b);
        sx_strcat(cap, " used", (int)sizeof(cap));
        sui_meter(ui, vname ? vname : "System volume", cap, (float)((double)vu / (double)vt));
    } else {
        sui_meter(ui, "System volume", "Unavailable", -1.0f);
    }
    sui_card_end(ui);
#ifdef NX_HOST_PREVIEW
    sui_note(ui, "Preview build: the volume figures above are demo data.");
#else
    if (!vol_ok)
        sui_note(ui, "Volume size cannot be shown yet: the kernel does not report volume statistics to applications.");
#endif

    sui_card_begin(ui);
    sui_nav_row(ui, "storage", "Storage", NULL);
    sui_nav_row(ui, "update", "Software Update", NULL);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_about, "about", "About This System", build_about);

/* ============ General ============ */

static void build_general(sui_t *ui) {
    char ver[48];
    sui_header(ui, "general", "General",
               "System-wide settings that apply to every user of this computer.");

    sx_join3(ver, (int)sizeof(ver), NEOLYX_VERSION, " (", NEOLYX_CODENAME);
    sx_strcat(ver, ")", (int)sizeof(ver));

    sui_card_begin(ui);
    sui_info(ui, "Name", NEOLYX_OS_NAME);
    sui_info(ui, "Version", ver);
    sui_nav_row(ui, "about", "About This System", "Processor, memory, display and storage");
    sui_card_end(ui);

    sui_card_begin(ui);
    sui_nav_row(ui, "update", "Software Update", NULL);
    sui_nav_row(ui, "storage", "Storage", NULL);
    sui_nav_row(ui, "datetime", "Date & Time", NULL);
    sui_nav_row(ui, "users", "Users & Accounts", NULL);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_general, "general", "General", build_general);

/* ============ Software Update ============ */

static int g_update_checked = 0;

static void build_update(sui_t *ui) {
    char ver[48];
    sx_join3(ver, (int)sizeof(ver), NEOLYX_VERSION, " (", NEOLYX_CODENAME);
    sx_strcat(ver, ")", (int)sizeof(ver));
    sui_header(ui, "update", "Software Update", "Keep NeolyxOS current with the latest features and security responses.");

    sui_card_begin(ui);
    sui_info(ui, "Installed version", ver);
    sui_status(ui, "Update service", "Not available", nxui_theme()->warning);
    if (sui_button_row(ui, "Check for updates",
                       g_update_checked ? "No update service is installed on this system." : NULL,
                       "Check Now", NXGLASS_BUTTON_DEFAULT, 1)) {
        g_update_checked = 1;
        settings_log("update: check requested; no update service available");
    }
    sui_card_end(ui);

    int auto_check = sprefs_get_bool("update", "auto_check", 1);
    int auto_dl = sprefs_get_bool("update", "auto_download", 0);
    int security = sprefs_get_bool("update", "security_responses", 1);
    sui_section(ui, "Automatic Updates");
    sui_card_begin(ui);
    if (sui_toggle(ui, "Check for updates", NULL, &auto_check)) sprefs_set_bool("update", "auto_check", auto_check);
    if (sui_toggle(ui, "Download new updates when available", NULL, &auto_dl)) sprefs_set_bool("update", "auto_download", auto_dl);
    if (sui_toggle(ui, "Install security responses", NULL, &security)) sprefs_set_bool("update", "security_responses", security);
    sui_card_end(ui);
    sui_note(ui, "These preferences are saved now and take effect when the update service is installed.");
}

SUI_DEFINE_PANE(settings_pane_update, "update", "Software Update", build_update);

/* ============ Storage ============ */

static void build_storage(sui_t *ui) {
    sui_header(ui, "storage", "Storage", "Space used on your system volume and how much memory is in use.");
    const char *vname = 0;
    uint64_t vt = 0, vu = 0, mt = 0, mu = 0;
    int vol_ok = settings_sys_volume(&vname, &vt, &vu) == 0 && vt > 0;
    int mem_ok = settings_sys_memory(&mt, &mu) == 0 && mt > 0;
    char cap[80], a[24], b[24];

    sui_card_begin(ui);
    if (vol_ok) {
        settings_format_bytes(vu, 1, a, (int)sizeof(a));
        settings_format_bytes(vt, 1, b, (int)sizeof(b));
        sx_join3(cap, (int)sizeof(cap), a, " of ", b);
        sx_strcat(cap, " used", (int)sizeof(cap));
        sui_meter(ui, vname ? vname : "System volume", cap, (float)((double)vu / (double)vt));
    } else {
        sui_meter(ui, "System volume", "Unavailable", -1.0f);
    }
    if (mem_ok) {
        settings_format_bytes(mu, 0, a, (int)sizeof(a));
        settings_format_bytes(mt, 0, b, (int)sizeof(b));
        sx_join3(cap, (int)sizeof(cap), a, " of ", b);
        sx_strcat(cap, " in use", (int)sizeof(cap));
        sui_meter(ui, "Memory", cap, (float)((double)mu / (double)mt));
    } else {
        sui_meter(ui, "Memory", "Unavailable", -1.0f);
    }
    sui_card_end(ui);
#ifdef NX_HOST_PREVIEW
    sui_note(ui, "Preview build: volume and memory figures are demo data.");
#else
    if (!vol_ok)
        sui_note(ui, "Volume usage cannot be shown yet: the kernel does not report volume statistics to applications.");
#endif

    sui_section(ui, "Volumes");
    sui_card_begin(ui);
    sui_info(ui, "File system", "NXFS");
    sui_info(ui, "System configuration", "/System/Config");
    sui_info(ui, "User preferences", "/Users/<name>/Library/Preferences");
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_storage, "storage", "Storage", build_storage);

/* ============ Date & Time ============ */

static const char *const k_wday[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
static const char *const k_month[12] = { "January", "February", "March", "April", "May", "June", "July",
                                         "August", "September", "October", "November", "December" };

static void build_datetime(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "datetime", "Date & Time", "The date and time come from this computer's real-time clock.");
    int y, mo, d, hh, mm, ss, wd;
    int ok = settings_sys_clock(&y, &mo, &d, &hh, &mm, &ss, &wd) == 0;
    int h24 = sprefs_get_bool("datetime", "clock_24h", 1);
    int secs = sprefs_get_bool("datetime", "show_seconds", 0);

    sui_card_begin(ui);
    nxc_rect_t blk = sui_block(ui, 86.0f);
    if (ui->pass == SUI_DRAW) {
        char t[32], dd[3], date[64], n[12];
        if (ok) {
            int hd = hh;
            if (!h24) { hd = hh % 12; if (hd == 0) hd = 12; }
            sx_itoa(hd, t, (int)sizeof(t));
            if (h24 && hd < 10) { t[0] = '0'; sx_itoa(hd, t + 1, (int)sizeof(t) - 1); }
            sx_strcat(t, ":", (int)sizeof(t));
            two_digits(mm, dd); sx_strcat(t, dd, (int)sizeof(t));
            if (secs) { sx_strcat(t, ":", (int)sizeof(t)); two_digits(ss, dd); sx_strcat(t, dd, (int)sizeof(t)); }
            if (!h24) sx_strcat(t, hh < 12 ? " AM" : " PM", (int)sizeof(t));
            sx_join3(date, (int)sizeof(date), k_wday[wd], ", ", sx_itoa(d, n, (int)sizeof(n)));
            sx_strcat(date, " ", (int)sizeof(date));
            sx_strcat(date, k_month[mo - 1], (int)sizeof(date));
            sx_strcat(date, " ", (int)sizeof(date));
            sx_strcat(date, sx_itoa(y, n, (int)sizeof(n)), (int)sizeof(date));
        } else {
            sx_strcpy(t, "--:--", (int)sizeof(t));
            sx_strcpy(date, "The real-time clock could not be read", (int)sizeof(date));
        }
        nxui_text(ui->c, NXUI_TEXT_LARGE_TITLE, blk.x, blk.y + 44.0f, t, th->text_primary, NULL);
        nxui_text(ui->c, NXUI_TEXT_BODY, blk.x, blk.y + 68.0f, date, th->text_secondary, NULL);
    }
    sui_info(ui, "Time source", ok ? "Real-time clock" : "Unavailable");
    sui_status(ui, "Set time automatically", "Unavailable", th->text_tertiary);
    sui_card_end(ui);
    sui_note(ui, "Network time and time zones need a time service, which NeolyxOS does not have yet. The clock shows the time stored in the hardware clock.");

    sui_section(ui, "Format");
    sui_card_begin(ui);
    if (sui_toggle(ui, "24-hour time", NULL, &h24)) sprefs_set_bool("datetime", "clock_24h", h24);
    if (sui_toggle(ui, "Show seconds", NULL, &secs)) sprefs_set_bool("datetime", "show_seconds", secs);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_datetime, "datetime", "Date & Time", build_datetime);

/* ============ Privacy & Security ============ */

static void build_privacy(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "privacy", "Privacy & Security",
               "Protection for your data and control over what the system lets apps do.");
    control_center_state_t *cc = control_center_get_state();
    char n[24];

    sui_section(ui, "Firewall");
    sui_card_begin(ui);
    if (cc) {
        int on = cc->firewall.enabled;
        sui_status(ui, "Firewall", on ? "Active" : "Off", on ? th->success : th->text_tertiary);
        sui_info(ui, "Connections blocked since startup", sx_itoa(cc->blocked_count, n, (int)sizeof(n)));
        sui_info(ui, "Active connections", sx_itoa(cc->active_connections, n, (int)sizeof(n)));
    } else {
        sui_status(ui, "Firewall", "Unknown", th->text_tertiary);
    }
    sui_card_end(ui);
    sui_note(ui, "Turn the firewall on or off from Control Center.");

    static const char *const sources[] = { "store", "identified" };
    static const char *const source_labels[] = { "Neolyx Store", "Store & Identified" };
    int src = sprefs_get_choice("security", "allow_apps_from", sources, 2, 1);
    int pw = sprefs_get_bool("security", "require_password", 1);
    sui_section(ui, "Security");
    sui_card_begin(ui);
    if (sui_segmented(ui, "Allow apps from", source_labels, 2, &src))
        sprefs_set_choice("security", "allow_apps_from", sources, 2, src);
    if (sui_toggle(ui, "Require password after screen lock", NULL, &pw))
        sprefs_set_bool("security", "require_password", pw);
    sui_status(ui, "Configuration audit log", "Always on", th->success);
    sui_card_end(ui);
    sui_note(ui, "Every settings change is written to the system log with its old and new value.");
}

SUI_DEFINE_PANE(settings_pane_privacy, "privacy", "Privacy & Security", build_privacy);

/* ============ Users & Accounts ============ */

static void build_users(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "users", "Users & Accounts", "Accounts that can sign in to this computer and how they sign in.");

    sui_card_begin(ui);
    nxc_rect_t blk = sui_block(ui, 72.0f);
    if (ui->pass == SUI_DRAW) {
        float cx = blk.x + 24.0f, cy = blk.y + blk.h * 0.5f;
        nxc_paint_t p = nxc_paint_linear(cx, cy - 22.0f, cx, cy + 22.0f, NXC_RGB(0xA0, 0xA8, 0xB8), NXC_RGB(0x6E, 0x76, 0x88));
        nxc_fill_circle_paint(ui->c, cx, cy, 22.0f, &p);
        nxsym_draw(ui->c, NXSYM_USER, cx, cy, 24.0f, NXC_RGB(0xFF, 0xFF, 0xFF), 1.0f);
        nxui_text(ui->c, NXUI_TEXT_TITLE3, blk.x + 58.0f, cy - 3.0f, "Current session", th->text_primary, NULL);
        nxui_text(ui->c, NXUI_TEXT_FOOTNOTE, blk.x + 58.0f, cy + 15.0f, "Signed in on this desktop", th->text_secondary, NULL);
    }
    sui_button_row(ui, "Add an account", NULL, "Add Account...", NXGLASS_BUTTON_DEFAULT, 0);
    sui_card_end(ui);
    sui_note(ui, "Account management needs the account service, which is not available to the desktop yet.");

    int auto_login = sprefs_get_bool("users", "auto_login", 0);
    int hints = sprefs_get_bool("users", "password_hints", 0);
    int power = sprefs_get_bool("users", "login_power_buttons", 1);
    sui_section(ui, "Login Window");
    sui_card_begin(ui);
    if (sui_toggle(ui, "Automatic login", "Signs in without a password at startup", &auto_login))
        sprefs_set_bool("users", "auto_login", auto_login);
    if (sui_toggle(ui, "Show password hints", NULL, &hints)) sprefs_set_bool("users", "password_hints", hints);
    if (sui_toggle(ui, "Show Sleep, Restart and Shut Down", NULL, &power)) sprefs_set_bool("users", "login_power_buttons", power);
    sui_card_end(ui);
}

SUI_DEFINE_PANE(settings_pane_users, "users", "Users & Accounts", build_users);

/* ============ Developer ============ */

static void build_developer(sui_t *ui) {
    const nxui_theme_t *th = nxui_theme();
    sui_header(ui, "developer", "Developer", "Tools and diagnostics for building and debugging NeolyxOS software.");

    sui_card_begin(ui);
#ifdef NX_HOST_PREVIEW
    sui_info(ui, "Desktop build", "Host preview");
#else
    sui_info(ui, "Desktop build", "Freestanding userspace");
#endif
    sui_status(ui, "Preferences store", sprefs_available() ? "Loaded" : "Unavailable",
               sprefs_available() ? th->success : th->warning);
    sui_status(ui, "Configuration audit log", "Always on", th->success);
    sui_card_end(ui);

    sui_section(ui, "Tools");
    sui_card_begin(ui);
    if (sui_button_row(ui, "Task Manager", "Processes, windows and resource use", "Open", NXGLASS_BUTTON_DEFAULT, 1))
        settings_open_app("Task Manager");
    if (sui_button_row(ui, "Font Manager", "Installed typefaces", "Open", NXGLASS_BUTTON_DEFAULT, 1))
        settings_open_app("Font Manager");
    sui_card_end(ui);

}

SUI_DEFINE_PANE(settings_pane_developer, "developer", "Developer", build_developer);
