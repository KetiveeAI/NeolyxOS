/*
 * NeolyxOS Installer Wizard
 * 
 * Progressive Disclosure Installation Flow:
 *   1. Welcome
 *   2. User Intent (General/Developer/Secure/Custom)
 *   3. Disk selection
 *   4. Layout choice (Auto/Manual)
 *   5. [Conditional] Filesystem selection
 *   6. [Conditional] Encryption toggle
 *   7. [Collapsed] Advanced options
 *   8. Summary confirmation
 *   9. Execute installation
 *   10. Complete
 * 
 * Copyright (c) 2025 KetiveeAI.
 */

#ifndef NEOLYX_INSTALLER_H
#define NEOLYX_INSTALLER_H

#include <stdint.h>

/* ============ Installer States ============ */

typedef enum {
    INSTALL_STATE_WELCOME,
    INSTALL_STATE_INTENT,       /* User intent: General/Developer/Secure/Custom */
    INSTALL_STATE_PROFILE,      /* Legacy: Desktop/Server */
    INSTALL_STATE_DISK,         /* Select installation disk */
    INSTALL_STATE_LAYOUT,       /* Auto or Manual layout */
    INSTALL_STATE_FILESYSTEM,   /* Conditional: FS selection */
    INSTALL_STATE_ENCRYPTION,   /* Conditional: Encryption toggle */
    INSTALL_STATE_ADVANCED,     /* Collapsed: Advanced options */
    INSTALL_STATE_SUMMARY,      /* Final confirmation */
    INSTALL_STATE_PARTITION,    /* Legacy: Creating partitions */
    INSTALL_STATE_COPYING,      /* Legacy: Copying files */
    INSTALL_STATE_EXECUTING,    /* Installing (locked UI) */
    INSTALL_STATE_COMPLETE,
    INSTALL_STATE_REMOVE_MEDIA, /* Prompt to remove USB/CD */
    INSTALL_STATE_ERROR,
} installer_state_t;

/* ============ Usage Profiles ============ */

typedef enum {
    USAGE_GENERAL = 0,      /* 90% of users - simple defaults */
    USAGE_DEVELOPER = 1,    /* Debug tools, dev environment */
    USAGE_SECURE = 2,       /* Encryption forced, hardened */
    USAGE_CUSTOM = 3,       /* Full control over everything */
} usage_profile_t;

/* ============ Filesystem Types ============ */

typedef enum {
    FS_NXFS = 0,            /* NeolyxOS native (default) */
    FS_FAT32 = 1,           /* Compatibility */
    FS_EXT4 = 2,            /* Linux-like (future) */
    FS_NXFS_ENCRYPTED = 3,  /* Encrypted NXFS */
} filesystem_type_t;

/* ============ Partition Layout ============ */

#define PARTITION_EFI_SIZE_MB       200     /* EFI System Partition */
#define PARTITION_RECOVERY_SIZE_MB  3072    /* 3GB Recovery partition */
/* System partition uses remaining space */

typedef enum {
    PARTITION_TYPE_EFI = 0,
    PARTITION_TYPE_RECOVERY,
    PARTITION_TYPE_SYSTEM,
} partition_type_t;

/* ============ Disk Info ============ */

typedef struct {
    int index;
    int port;                   /* AHCI port */
    char name[32];
    char model[64];
    uint64_t size_bytes;
    int supported;              /* 1 = size OK, 0 = too small */
    int has_data;               /* 1 = contains partitions */
    int selected;               /* UI selection state */
} disk_info_t;

#define MAX_DISKS 8
#define MIN_DISK_SIZE_GB 16

/* ============ Installation Plan ============ */
/* Filled by UI screens, executed by installer */

typedef struct {
    usage_profile_t profile;    /* User intent */
    int disk_id;                /* Selected disk index */
    int auto_layout;            /* 1 = automatic, 0 = manual */
    filesystem_type_t fs_type;  /* Filesystem for system partition */
    int encryption;             /* 0 = off, 1 = on */
    int wipe_disk;              /* 0 = quick format, 1 = zero-fill */
    /* Advanced options */
    int debug_symbols;          /* Include debug symbols */
    int disable_recovery;       /* Skip recovery partition */
    int verbose_boot;           /* Verbose boot log */
} install_plan_t;

/* ============ Legacy Compatibility ============ */

typedef enum {
    PROFILE_DESKTOP = 0,
    PROFILE_SERVER = 1,
} profile_type_t;

typedef struct {
    profile_type_t profile;
    disk_info_t *target_disk;
    char hostname[64];
} install_config_t;

/* ============ Installer API ============ */

void installer_init(void);
int installer_run_wizard(void);
int installer_needed(void);

/* Get/set installation plan */
install_plan_t *installer_get_plan(void);
void installer_set_defaults(install_plan_t *plan, usage_profile_t profile);

/* Disk detection */
int installer_detect_disks(disk_info_t *disks, int max);

/* Partitioning */
int installer_create_partitions(disk_info_t *disk);
int installer_format_partitions(disk_info_t *disk);

/* File copy */
int installer_copy_files(disk_info_t *disk, profile_type_t profile);

/* Execute full installation from plan */
int installer_execute(install_plan_t *plan);

#endif /* NEOLYX_INSTALLER_H */
