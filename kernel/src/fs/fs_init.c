/*
 * NeolyxOS Filesystem Initialization
 * 
 * Creates the standard directory hierarchy on boot.
 * 
 * Copyright (c) 2025 KetiveeAI
 * Under KetiveeAI Public License v1.0
 */

#include "fs/vfs.h"
#include "fs/fs_init.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern int vfs_create(const char *path, vfs_type_t type, uint32_t perms);

/* ============ Directory Definitions ============ */

/* Root directories - FROZEN per NeolyxOS Architecture v1.0 */
static const char *root_dirs[] = {
    /* Core System (SIP Protected) */
    "/System",
    "/System/Kernel",
    "/System/Library",
    "/System/Drivers",
    
    /* ============ Security Subsystem (Based on XNU MAC) ============ */
    "/System/Security",
    "/System/Security/Auth",              /* Authentication data */
    "/System/Security/Auth/Users",        /* User account database */
    "/System/Security/Auth/Groups",       /* Group definitions */
    "/System/Security/Auth/Tokens",       /* Session tokens */
    "/System/Security/Auth/Keys",         /* SSH/GPG keys (system) */
    
    "/System/Security/Firewall",          /* Firewall configuration */
    "/System/Security/Firewall/Rules",    /* Firewall rules */
    "/System/Security/Firewall/Logs",     /* Firewall logs */
    "/System/Security/Firewall/Zones",    /* Network zones */
    
    "/System/Security/Certificates",      /* System certificates */
    "/System/Security/Certificates/Root", /* Root CA certificates */
    "/System/Security/Certificates/User", /* User certificates */
    "/System/Security/Certificates/Trusted", /* Trusted certs */
    
    "/System/Security/Audit",             /* Security audit logs */
    "/System/Security/Audit/Events",      /* Audit events */
    "/System/Security/Audit/Policies",    /* Audit policies */
    
    "/System/Security/MAC",               /* Mandatory Access Control */
    "/System/Security/MAC/Policies",      /* MAC policies (SIP, etc) */
    "/System/Security/MAC/Labels",        /* Security labels */
    
    "/System/Security/Sandbox",           /* App sandbox profiles */
    "/System/Security/Sandbox/Profiles",  /* Sandbox definitions */
    "/System/Security/Sandbox/Containers",/* Sandbox containers */
    
    "/System/Security/Keychain",          /* Keychain storage */
    "/System/Security/Keychain/System",   /* System keychain */
    "/System/Security/Keychain/Login",    /* Login keychain template */
    
    /* ============ Desktop Environment Data ============ */
    "/System/Desktop",
    "/System/Desktop/Wallpapers",         /* Default wallpapers */
    "/System/Desktop/Themes",             /* UI themes */
    "/System/Desktop/Icons",              /* System icons */
    "/System/Desktop/Sounds",             /* UI sounds */
    "/System/Desktop/Dock",               /* Dock configuration */
    "/System/Desktop/MenuBar",            /* Menu bar config */
    "/System/Desktop/LockScreen",         /* Lock screen assets */
    
    /* ============ Application Data (System-wide) ============ */
    "/System/AppData",
    "/System/AppData/Defaults",           /* App default settings */
    "/System/AppData/Caches",             /* App cache templates */
    "/System/AppData/Registry",           /* Installed app registry */
    "/System/AppData/Bundles",            /* App bundle metadata */
    
    /* ============ Runtime Data ============ */
    "/System/Run",
    "/System/Run/Daemons",                /* Running daemon PIDs */
    "/System/Run/Services",               /* Service status */
    "/System/Run/Sessions",               /* Active user sessions */
    "/System/Run/Locks",                  /* Lock files */
    
    /* System-wide Applications */
    "/Applications",
    
    /* Library (System-wide resources) */
    "/Library",
    "/Library/Fonts",
    "/Library/Preferences",
    "/Library/Logs",
    "/Library/Frameworks",          /* Shared frameworks */
    "/Library/Bin",                 /* System PATH binaries */
    
    /* Audio Plugins */
    "/Library/Audio",
    "/Library/Audio/Plugins",
    "/Library/Audio/Plugins/VST",   /* VST plugins */
    "/Library/Audio/Plugins/VST3",  /* VST3 plugins */
    "/Library/Audio/Plugins/AU",    /* Audio Units */
    "/Library/Audio/Plugins/CLAP",  /* CLAP plugins */
    "/Library/Audio/Presets",       /* Audio presets */
    
    /* Language Runtimes */
    "/Library/Runtimes",
    "/Library/Runtimes/Python",     /* Python installation */
    "/Library/Runtimes/Node",       /* Node.js */
    "/Library/Runtimes/Rust",       /* Rust toolchain */
    "/Library/Runtimes/Go",         /* Go toolchain */
    "/Library/Runtimes/Java",       /* JVM */
    
    /* Users */
    "/Users",
    "/Users/Shared",
    
    /* Public Storage (System-wide shared data) */
    "/Public",
    "/Public/Documents",            /* Public documents */
    "/Public/Pictures",             /* Public photos */
    "/Public/Music",                /* Public music */
    "/Public/Videos",               /* Public videos */
    "/Public/Downloads",            /* Public downloads */
    
    /* Developer */
    "/Developer",
    "/Developer/SDKs",
    "/Developer/Toolchains",
    "/Developer/Debuggers",
    
    /* Games (First-Class) */
    "/Games",
    
    /* Caches (Volatile, Wipeable) */
    "/Caches",
    "/Caches/ShaderCache",
    "/Caches/RenderCache",
    "/Caches/AssetCache",
    
    /* Crash Reports */
    "/CrashReports",
    "/CrashReports/Kernel",
    "/CrashReports/Drivers",
    "/CrashReports/Applications",
    "/CrashReports/Games",
    
    /* Storage */
    "/Volumes",
    "/Boot",
    "/Boot/EFI",
    "/Boot/Recovery",
    "/tmp",
    NULL
};

/* User home subdirectories */
static const char *user_subdirs[] = {
    /* Standard user directories */
    "Desktop",
    "Documents",
    "Downloads",
    "Pictures",
    "Music",
    "Videos",
    "Public",
    "Public/Drop Box",
    "Applications",               /* User-installed apps */
    
    /* User Library (App data storage) */
    "Library",
    "Library/Application Support",  /* App-specific data */
    "Library/Preferences",          /* App settings (.plist) */
    "Library/Caches",               /* App caches (can be deleted) */
    "Library/Logs",                 /* App logs */
    "Library/Saved Application State",  /* App state for resume */
    "Library/Containers",           /* Sandboxed app data */
    "Library/Group Containers",     /* Shared app group data */
    
    /* Config (dotfiles) */
    ".config",
    ".config/shell",
    ".local",
    ".local/bin",                   /* User PATH binaries */
    ".local/share",                 /* XDG data */
    NULL
};

/* ============ Helper Functions ============ */

static int fs_strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void fs_strcpy(char *dst, const char *src) {
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

static void fs_strcat(char *dst, const char *src) {
    while (*dst) dst++;
    while (*src) *dst++ = *src++;
    *dst = '\0';
}

/* ============ Filesystem Initialization ============ */

int fs_init_root_structure(void) {
    serial_puts("[FS] Creating root directory structure...\n");
    
    int created = 0;
    int failed = 0;
    
    /* Use vfs_makedirs for recursive creation (like mkdir -p) */
    extern int vfs_makedirs(const char *path, uint32_t perms);
    
    for (int i = 0; root_dirs[i] != NULL; i++) {
        int result = vfs_makedirs(root_dirs[i], 0755);
        if (result == 0) {
            created++;
        } else if (result == -2) {
            /* Already exists - ok */
        } else {
            serial_puts("[FS] Failed to create: ");
            serial_puts(root_dirs[i]);
            serial_puts("\n");
            failed++;
        }
    }
    
    serial_puts("[FS] Root structure: ");
    /* Print count */
    char buf[16];
    int n = created;
    int i = 0;
    if (n == 0) { buf[i++] = '0'; }
    else {
        char tmp[16];
        int j = 0;
        while (n > 0) { tmp[j++] = '0' + (n % 10); n /= 10; }
        while (j > 0) buf[i++] = tmp[--j];
    }
    buf[i] = '\0';
    serial_puts(buf);
    serial_puts(" directories created\n");
    
    return (failed == 0) ? 0 : -1;
}

int fs_create_user_home(const char *username) {
    if (!username || username[0] == '\0') {
        return -1;
    }
    
    serial_puts("[FS] Creating home directory for: ");
    serial_puts(username);
    serial_puts("\n");
    
    char path[256];
    
    /* Create user home directory */
    fs_strcpy(path, "/Users/");
    fs_strcat(path, username);
    
    int result = vfs_create(path, VFS_DIRECTORY, 0700);
    if (result != 0 && result != -2) {
        serial_puts("[FS] Failed to create home directory\n");
        return -1;
    }
    
    /* Create subdirectories using vfs_makedirs for nested paths */
    int base_len = fs_strlen(path);
    extern int vfs_makedirs(const char *path, uint32_t perms);
    
    for (int i = 0; user_subdirs[i] != NULL; i++) {
        path[base_len] = '/';
        fs_strcpy(&path[base_len + 1], user_subdirs[i]);
        
        result = vfs_makedirs(path, 0755);
        if (result != 0 && result != -2) {
            serial_puts("[FS] Warning: Failed to create ");
            serial_puts(path);
            serial_puts("\n");
        }
    }
    
    serial_puts("[FS] Home directory created at /Users/");
    serial_puts(username);
    serial_puts("\n");
    
    return 0;
}

/* Get standard directory path for current user */
int fs_get_user_dir(const char *username, fs_user_dir_t dir, char *path, int max_len) {
    if (!username || !path || max_len < 64) {
        return -1;
    }
    
    fs_strcpy(path, "/Users/");
    fs_strcat(path, username);
    
    switch (dir) {
        case FS_DIR_HOME:
            /* Already set */
            break;
        case FS_DIR_DESKTOP:
            fs_strcat(path, "/Desktop");
            break;
        case FS_DIR_DOCUMENTS:
            fs_strcat(path, "/Documents");
            break;
        case FS_DIR_DOWNLOADS:
            fs_strcat(path, "/Downloads");
            break;
        case FS_DIR_PICTURES:
            fs_strcat(path, "/Pictures");
            break;
        case FS_DIR_MUSIC:
            fs_strcat(path, "/Music");
            break;
        case FS_DIR_VIDEOS:
            fs_strcat(path, "/Videos");
            break;
        case FS_DIR_PUBLIC:
            fs_strcat(path, "/Public");
            break;
        case FS_DIR_APPS:
            fs_strcat(path, "/Applications");
            break;
        case FS_DIR_LIBRARY:
            fs_strcat(path, "/Library");
            break;
        case FS_DIR_CONFIG:
            fs_strcat(path, "/.config");
            break;
        default:
            return -1;
    }
    
    return 0;
}

/* Initialize filesystem with default user */
int fs_init(void) {
    serial_puts("[FS] Initializing NeolyxOS filesystem...\n");
    
    /* Create root directory structure */
    if (fs_init_root_structure() != 0) {
        serial_puts("[FS] Warning: Some directories failed to create\n");
    }
    
    /* Create default user */
    fs_create_user_home("root");
    fs_create_user_home("guest");
    
    serial_puts("[FS] Filesystem initialization complete\n");
    serial_puts("[FS] Standard paths:\n");
    serial_puts("     /System      - OS core (SIP)\n");
    serial_puts("     /Applications - Installed apps\n");
    serial_puts("     /Users       - User home dirs\n");
    serial_puts("     /Library     - System configs\n");
    serial_puts("     /Volumes     - Mounted drives\n");
    
    return 0;
}
