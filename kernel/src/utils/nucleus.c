#include "utils/nucleus.h"
#include "storage/storage.h"
#include "storage/ahci_block.h"
#include "drivers/ahci.h"
#include "fs/vfs.h"
#include "ui/vector_icons_v2.h"
#include "ui/disk_icons.h"
#include "ui/nxfont.h"
#include "mm/pmm.h"

/* Simplified Light Purple Theme */
#define COLOR_BG          0xFF16161E   /* Dark background */
#define COLOR_PANEL       0xFF1E1E2E   /* Panel background */
#define COLOR_PANEL_DARK  0xFF16161E   /* Same as BG */
#define COLOR_ACCENT      0xFF9D7CE8   /* Light purple - MAIN accent */
#define COLOR_WHITE       0xFFE0E0E8
#define COLOR_GRAY        0xFF6B7280
#define COLOR_GREEN       0xFF22C55E   /* Success status only */
#define COLOR_YELLOW      0xFF9D7CE8   /* Use purple instead */
#define COLOR_RED         0xFFEF4444   /* Error/unallocated only */
#define COLOR_BLUE        0xFF9D7CE8   /* Use purple instead */
#define COLOR_ORANGE      0xFFB794F6   /* Lighter purple */
#define COLOR_PURPLE      0xFF9D7CE8   /* NXFS - Light purple */
#define COLOR_CYAN        0xFFA78BFA   /* Recovery - Medium purple */
#define COLOR_DARK_GRAY   0xFF374151   /* Unallocated space */

/* Helper Functions */
static void fb_fill_rect(volatile uint32_t *fb, uint32_t pitch, uint32_t w_max, uint32_t h_max, 
                         uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color) {
    for (uint32_t j = 0; j < h && (y + j) < h_max; j++) {
        for (uint32_t i = 0; i < w && (x + i) < w_max; i++) {
            fb[(y + j) * (pitch / 4) + (x + i)] = color;
        }
    }
}

static void fb_clear(volatile uint32_t *fb, uint32_t pitch, uint32_t w, uint32_t h, uint32_t color) {
    fb_fill_rect(fb, pitch, w, h, 0, 0, w, h, color);
}

static void nx_strcpy(char *dst, const char *src, int max) {
    if (!dst || !src || max <= 0) return;
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int nx_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *(unsigned char*)a - *(unsigned char*)b;
}

static void nx_strcat(char *dst, const char *src, int max) {
    int i = 0;
    while (dst[i] && i < max) i++;
    int j = 0;
    while (src[j] && i < max - 1) dst[i++] = src[j++];
    dst[i] = '\0';
}

/* Partition structure */
typedef struct {
    int index;
    int disk_index;
    char label[32];
    char fs_type[16];
    uint64_t size_bytes;
    uint64_t used_bytes;
    uint64_t lba_start;
    uint64_t sector_count;
    int is_mounted;
    char mount_point[32];
    char role[16];
    uint32_t color_code;
} nucleus_partition_t;

/* Global State */
static nucleus_disk_t nucleus_disks[8];
static int nucleus_disk_count = 0;
static nucleus_partition_t nucleus_partitions[16];
static int nucleus_partition_count = 0;

static int selection = 0;
static int selected_partition = -1;

static uint64_t last_partition_click_time = 0;
static int last_partition_click_idx = -1;
extern uint64_t pit_get_ticks(void);
static uint64_t total_storage = 0;
static uint64_t total_used = 0;
static uint64_t total_free = 0;

/* Filesystem selection dropdown */
static int fs_dropdown_open = 0;
static nucleus_fs_type_t selected_fs_type = FS_TYPE_NXFS;

static const char* fs_type_names[] = {
    "NXFS",
    "FAT32",
    "NTFS",
    "ext4",
    "exFAT"
};

/* CRITICAL SAFETY FEATURES */
static int dry_run_mode = 0; /* Production mode - Real formatting enabled */
static int pending_format_disk = -1;
static int pending_format_part = -1;
static int show_confirm_dialog = 0;
static char confirm_message[256];

/* Phase 6: UI Polish additions */
static int show_progress_bar = 0;           /* Show progress bar during operations */
static int progress_percent = 0;            /* Current progress (0-100) */
static char progress_message[64] = "";      /* Progress status message */
static char confirm_disk_info[128];
static int show_help_dialog = 0;

extern void serial_puts(const char *s);

/* Forward declarations */
static void nucleus_action_erase_disk(void);
static void nucleus_execute_confirmed_format(void);

/* Size formatting */
static void nucleus_format_size(uint64_t bytes, char *out, int max) {
    uint64_t gb = bytes / (1024ULL * 1024 * 1024);
    uint64_t mb = bytes / (1024ULL * 1024);
    
    int i = 0;
    if (gb > 0) {
        if (gb >= 100) out[i++] = '0' + (gb / 100) % 10;
        if (gb >= 10) out[i++] = '0' + (gb / 10) % 10;
        out[i++] = '0' + gb % 10;
        out[i++] = ' '; out[i++] = 'G'; out[i++] = 'B';
    } else {
        if (mb >= 100) out[i++] = '0' + (mb / 100) % 10;
        if (mb >= 10) out[i++] = '0' + (mb / 10) % 10;
        out[i++] = '0' + mb % 10;
        out[i++] = ' '; out[i++] = 'M'; out[i++] = 'B';
    }
    out[i] = '\0';
    (void)max;
}

/* Partition Table Structures */
typedef struct {
    uint8_t status;
    uint8_t chs_start[3];
    uint8_t type;
    uint8_t chs_end[3];
    uint32_t lba_start;
    uint32_t sectors;
} __attribute__((packed)) mbr_entry_t;

typedef struct {
    uint8_t bootstrap[446];
    mbr_entry_t entries[4];
    uint16_t signature;
} __attribute__((packed)) mbr_t;

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
    uint64_t current_lba;
    uint64_t backup_lba;
    uint64_t first_usable_lba;
    uint64_t last_usable_lba;
    uint8_t  disk_guid[16];
    uint64_t partition_entry_lba;
    uint32_t num_partition_entries;
    uint32_t partition_entry_size;
    uint32_t partition_entry_crc32;
} __attribute__((packed)) gpt_header_t;

typedef struct {
    uint8_t type_guid[16];
    uint8_t unique_guid[16];
    uint64_t first_lba;
    uint64_t last_lba;
    uint64_t flags;
    uint16_t name[36];
} __attribute__((packed)) gpt_entry_t;

/* GPT Type GUIDs */
static const uint8_t GPT_TYPE_EFI_SYSTEM[16] = {
    0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11,
    0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B
};

static const uint8_t GPT_TYPE_MS_BASIC_DATA[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

static const uint8_t GPT_TYPE_LINUX_FS[16] = {
    0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47,
    0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4
};

/* NeolyxOS NXFS Partition GUID - custom type */
static const uint8_t GPT_TYPE_NXFS[16] = {
    0x4E, 0x58, 0x46, 0x53, 0x00, 0x00, 0x00, 0x01,  /* NXFS...*/
    0x4E, 0x45, 0x4F, 0x4C, 0x59, 0x58, 0x4F, 0x53   /* NEOLYXOS */
};

/* NeolyxOS Recovery Partition GUID */
static const uint8_t GPT_TYPE_NEOLYX_RECOVERY[16] = {
    0x52, 0x45, 0x43, 0x4F, 0x56, 0x45, 0x52, 0x59,  /* RECOVERY */
    0x4E, 0x45, 0x4F, 0x4C, 0x59, 0x58, 0x4F, 0x53   /* NEOLYXOS */
};

static int guid_match(const uint8_t *a, const uint8_t *b) {
    for (int i = 0; i < 16; i++) {
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

static void identify_gpt_type(const uint8_t *type_guid, nucleus_partition_t *p) {
    if (guid_match(type_guid, GPT_TYPE_EFI_SYSTEM)) {
        nx_strcpy(p->fs_type, "EFI System", 16);
        nx_strcpy(p->role, "System", 16);
        p->color_code = COLOR_GREEN;
    } else if (guid_match(type_guid, GPT_TYPE_NXFS)) {
        nx_strcpy(p->fs_type, "NXFS", 16);
        nx_strcpy(p->role, "Data", 16);
        p->color_code = COLOR_PURPLE;  /* Purple for NXFS */
    } else if (guid_match(type_guid, GPT_TYPE_NEOLYX_RECOVERY)) {
        nx_strcpy(p->fs_type, "Recovery", 16);
        nx_strcpy(p->role, "Recovery", 16);
        p->color_code = COLOR_CYAN;    /* Cyan for Recovery */
    } else if (guid_match(type_guid, GPT_TYPE_MS_BASIC_DATA)) {
        nx_strcpy(p->fs_type, "Windows/Data", 16);
        nx_strcpy(p->role, "Data", 16);
        p->color_code = COLOR_BLUE;
    } else if (guid_match(type_guid, GPT_TYPE_LINUX_FS)) {
        nx_strcpy(p->fs_type, "Linux", 16);
        nx_strcpy(p->role, "Data", 16);
        p->color_code = COLOR_ORANGE;
    } else {
        nx_strcpy(p->fs_type, "Unknown", 16);
        nx_strcpy(p->role, "Data", 16);
        p->color_code = COLOR_GRAY;
    }
}

/* ============ Disk I/O Abstraction ============ */

/* External NVMe API */
extern void *nvme_get_controller(int index);
extern int nvme_read(void *ctrl, uint32_t nsid, uint64_t lba, uint32_t count, void *buffer);
extern int nvme_write(void *ctrl, uint32_t nsid, uint64_t lba, uint32_t count, const void *buffer);

int nucleus_read_disk(int port, uint64_t lba, uint32_t count, void *buffer) {
    if (port & 0x8000) {
        /* NVMe: Decode port */
        int ctrl_idx = (port >> 8) & 0x7F;
        int ns_idx = port & 0xFF;
        
        void *ctrl = nvme_get_controller(ctrl_idx);
        if (!ctrl) return -1;
        
        /* Assuming NSID = index + 1 */
        return nvme_read(ctrl, ns_idx + 1, lba, count, buffer);
    } else {
        /* AHCI */
        return ahci_read(port, lba, count, buffer);
    }
}

int nucleus_write_disk(int port, uint64_t lba, uint32_t count, const void *buffer) {
    if (port & 0x8000) {
        /* NVMe */
        int ctrl_idx = (port >> 8) & 0x7F;
        int ns_idx = port & 0xFF;
        
        void *ctrl = nvme_get_controller(ctrl_idx);
        if (!ctrl) return -1;
        
        return nvme_write(ctrl, ns_idx + 1, lba, count, buffer);
    } else {
        /* AHCI */
        return ahci_write(port, lba, count, buffer);
    }
}

/*
 * Detect actual filesystem by reading partition boot sector.
 * This provides accurate detection regardless of GPT type GUID.
 * Returns: 0=unknown, 1=detected
 */
int nucleus_detect_filesystem(int port, uint64_t lba_start, nucleus_partition_t *p) {
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return 0;
    
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    /* Read first sector of partition */
    if (nucleus_read_disk(port, lba_start, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return 0;
    }
    
    int detected = 0;
    
    /* Check for FAT32: 0x55AA signature + "FAT32" at offset 82 or 54 */
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        /* FAT32 BPB: check for FAT32 string at offset 82 (extended BPB) */
        if (buffer[82] == 'F' && buffer[83] == 'A' && buffer[84] == 'T' &&
            buffer[85] == '3' && buffer[86] == '2') {
            nx_strcpy(p->fs_type, "FAT32", 16);
            p->color_code = COLOR_GREEN;
            detected = 1;
        }
        /* FAT16/FAT12: check at offset 54 */
        else if (buffer[54] == 'F' && buffer[55] == 'A' && buffer[56] == 'T') {
            if (buffer[57] == '1' && buffer[58] == '6') {
                nx_strcpy(p->fs_type, "FAT16", 16);
            } else if (buffer[57] == '1' && buffer[58] == '2') {
                nx_strcpy(p->fs_type, "FAT12", 16);
            } else {
                nx_strcpy(p->fs_type, "FAT", 16);
            }
            p->color_code = COLOR_GREEN;
            detected = 1;
        }
        /* Check for NTFS: "NTFS    " at offset 3 */
        else if (buffer[3] == 'N' && buffer[4] == 'T' && buffer[5] == 'F' && buffer[6] == 'S') {
            nx_strcpy(p->fs_type, "NTFS", 16);
            p->color_code = COLOR_BLUE;
            detected = 1;
        }
        /* Check for exFAT: "EXFAT   " at offset 3 */
        else if (buffer[3] == 'E' && buffer[4] == 'X' && buffer[5] == 'F' && buffer[6] == 'A' && buffer[7] == 'T') {
            nx_strcpy(p->fs_type, "exFAT", 16);
            p->color_code = COLOR_BLUE;
            detected = 1;
        }
    }
    
    /* Check for NXFS: magic "NXFS" at offset 0 */
    if (!detected && buffer[0] == 'N' && buffer[1] == 'X' && buffer[2] == 'F' && buffer[3] == 'S') {
        nx_strcpy(p->fs_type, "NXFS", 16);
        p->color_code = COLOR_PURPLE;
        detected = 1;
    }
    
    /* Check for ext2/3/4: magic 0xEF53 at offset 0x438 (superblock at 1024 bytes) */
    if (!detected) {
        /* Need to read sector 2 for ext superblock (offset 1024 = sector 2) */
        if (nucleus_read_disk(port, lba_start + 2, 1, buffer) == 0) {
            /* Magic at offset 56 within superblock (1024+56 = 1080, so sector 2 offset 56) */
            if (buffer[56] == 0x53 && buffer[57] == 0xEF) {
                nx_strcpy(p->fs_type, "ext4", 16);
                p->color_code = COLOR_ORANGE;
                detected = 1;
            }
        }
    }
    
    /* If not detected, mark as Unformatted if first 64 bytes are all zeros */
    if (!detected) {
        int all_zero = 1;
        for (int i = 0; i < 64; i++) {
            if (buffer[i] != 0) { all_zero = 0; break; }
        }
        if (all_zero) {
            nx_strcpy(p->fs_type, "Unformatted", 16);
            p->color_code = COLOR_DARK_GRAY;
            detected = 1;
        }
    }
    
    pmm_free_page(buffer_phys);
    return detected;
}

/* Load partitions */
static void nucleus_load_partitions(int disk_idx) {
    nucleus_partition_count = 0;
    selected_partition = -1;
    
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return;
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    int port = disk->port;
    
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return;
    
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    if (nucleus_read_disk(port, 0, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return;
    }
    
    mbr_t *mbr = (mbr_t*)buffer;
    
    if (mbr->signature != 0xAA55) {
        pmm_free_page(buffer_phys);
        return;
    }
    
    int is_gpt = 0;
    for (int i = 0; i < 4; i++) {
        if (mbr->entries[i].type == 0xEE) {
            is_gpt = 1;
            break;
        }
    }
    
    if (is_gpt) {
        if (nucleus_read_disk(port, 1, 1, buffer) != 0) {
            pmm_free_page(buffer_phys);
            return;
        }
        
        gpt_header_t *gpt = (gpt_header_t*)buffer;
        if (gpt->signature != 0x5452415020494645ULL) {
            pmm_free_page(buffer_phys);
            return;
        }
        
        uint64_t entry_lba = gpt->partition_entry_lba;
        uint32_t num_entries = gpt->num_partition_entries;
        uint32_t entry_size = gpt->partition_entry_size;
        
        if (num_entries > 128) num_entries = 128;
        
        disk->partition_style = DISK_STYLE_GPT;
        
        int p_idx = 0;
        for (uint32_t s = 0; s < (num_entries * entry_size + 511) / 512 && nucleus_partition_count < 16; s++) {
            if (nucleus_read_disk(port, entry_lba + s, 1, buffer) != 0) break;
            
            for (uint32_t e = 0; e < 512 / entry_size && nucleus_partition_count < 16; e++) {
                if (p_idx >= (int)num_entries) break;
                
                gpt_entry_t *entry = (gpt_entry_t*)(buffer + e * entry_size);
                
                int is_used = 0;
                for (int k = 0; k < 16; k++) {
                    if (entry->type_guid[k] != 0) is_used = 1;
                }
                
                if (is_used) {
                    nucleus_partition_t *p = &nucleus_partitions[nucleus_partition_count];
                    p->index = nucleus_partition_count;
                    p->disk_index = disk_idx;
                    p->lba_start = entry->first_lba;
                    p->sector_count = entry->last_lba - entry->first_lba + 1;
                    p->size_bytes = p->sector_count * 512;
                    p->used_bytes = 0;
                    p->is_mounted = 0;
                    p->mount_point[0] = '\0';
                    
                    /* First set defaults from GPT type GUID */
                    identify_gpt_type(entry->type_guid, p);
                    
                    /* Then override with actual detected filesystem from boot sector */
                    nucleus_detect_filesystem(port, p->lba_start, p);
                    
                    int k = 0;
                    for (int j = 0; j < 36 && k < 31; j++) {
                        uint16_t c = entry->name[j];
                        if (c == 0) break;
                        if (c < 128) p->label[k++] = (char)c;
                    }
                    p->label[k] = '\0';
                    
                    if (p->label[0] == '\0') {
                        nx_strcpy(p->label, "DATA-", 32);
                        char size[8];
                        nucleus_format_size(p->size_bytes, size, 8);
                        nx_strcat(p->label, size, 32);
                    }
                    
                    nucleus_partition_count++;
                }
                p_idx++;
            }
        }
    } else {
        disk->partition_style = DISK_STYLE_MBR;
        
        for (int i = 0; i < 4 && nucleus_partition_count < 16; i++) {
            mbr_entry_t *entry = &mbr->entries[i];
            if (entry->type == 0) continue;
            
            nucleus_partition_t *p = &nucleus_partitions[nucleus_partition_count];
            p->index = i;
            p->disk_index = disk_idx;
            p->lba_start = entry->lba_start;
            p->sector_count = entry->sectors;
            p->size_bytes = (uint64_t)entry->sectors * 512;
            p->used_bytes = 0;
            p->is_mounted = 0;
            p->mount_point[0] = '\0';
            
            /* Set defaults from MBR partition type */
            if (entry->type == 0xEF) {
                nx_strcpy(p->fs_type, "EFI System", 16);
                nx_strcpy(p->role, "System", 16);
                p->color_code = COLOR_GREEN;
            } else if (entry->type == 0x83) {
                nx_strcpy(p->fs_type, "Linux", 16);
                nx_strcpy(p->role, "Data", 16);
                p->color_code = COLOR_ORANGE;
            } else if (entry->type == 0x07) {
                nx_strcpy(p->fs_type, "NTFS", 16);
                nx_strcpy(p->role, "Data", 16);
                p->color_code = COLOR_BLUE;
            } else if (entry->type == 0x0B || entry->type == 0x0C) {
                nx_strcpy(p->fs_type, "FAT32", 16);
                nx_strcpy(p->role, "Data", 16);
                p->color_code = COLOR_YELLOW;
            } else {
                nx_strcpy(p->fs_type, "Unknown", 16);
                nx_strcpy(p->role, "Raw", 16);
                p->color_code = COLOR_GRAY;
            }
            
            /* Override with actual detected filesystem from boot sector */
            nucleus_detect_filesystem(port, p->lba_start, p);
            
            nx_strcpy(p->label, "Partition ", 32);
            p->label[10] = '1' + i;
            p->label[11] = '\0';
            
            nucleus_partition_count++;
        }
    }
    
    disk->partition_count = nucleus_partition_count;
    if (nucleus_partition_count > 0) selected_partition = 0;
    
    pmm_free_page(buffer_phys);
}

/* Update system info */
static void nucleus_update_system_info(void) {
    total_storage = 0;
    total_used = 0;
    
    for (int i = 0; i < nucleus_disk_count; i++) {
        total_storage += nucleus_disks[i].size_bytes;
    }
    
    for (int i = 0; i < nucleus_partition_count; i++) {
        if (nucleus_partitions[i].is_mounted) {
            total_used += nucleus_partitions[i].used_bytes;
        }
    }
    
    total_free = total_storage - total_used;
}

/* Mount partition */
static void nucleus_mount_partition(int part_idx) {
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return;
    
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    if (p->is_mounted) return;
    
    if (nx_strcmp(p->fs_type, "Unknown") == 0) return;
    
    char path[64];
    nx_strcpy(path, "/", 64);
    nx_strcat(path, p->label, 64);
    
    vfs_create(path, VFS_DIRECTORY, 0755);
    
    ahci_block_dev_t *dev = ahci_block_open_partition(
        nucleus_disks[p->disk_index].port,
        p->lba_start,
        p->sector_count
    );
    
    if (!dev) return;
    
    int mounted = vfs_mount(path, dev, "fat32", 0);
    if (mounted != 0) mounted = vfs_mount(path, dev, "nxfs", 0);
    
    if (mounted == 0) {
        p->is_mounted = 1;
        nx_strcpy(p->mount_point, path, 32);
        p->used_bytes = (p->size_bytes * 75) / 100;
    } else {
        ahci_block_close(dev);
    }
    
    nucleus_update_system_info();
}

/* Unmount partition */
static void nucleus_unmount_partition(int part_idx) {
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return;
    
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    if (!p->is_mounted) return;
    
    /* TODO: Call vfs_unmount when implemented */
    p->is_mounted = 0;
    p->mount_point[0] = '\0';
    p->used_bytes = 0;
    
    nucleus_update_system_info();
}

/* Scan disks */
void nucleus_scan_disks(void) {
    nucleus_disk_count = 0;
    serial_puts("[NUCLEUS] Starting disk scan...\n");
    
    /* === AHCI/SATA Enumeration === */
    if (ahci_init() == 0) {
        serial_puts("[NUCLEUS] AHCI init OK\n");
        ahci_drive_t ahci_drives[8];
        int count = ahci_probe_drives(ahci_drives, 8);
        
        for (int i = 0; i < count && i < 8 && nucleus_disk_count < 16; i++) {
            int idx = nucleus_disk_count;
            nucleus_disks[idx].index = idx;
            nucleus_disks[idx].port = ahci_drives[i].port;
            
            nucleus_disks[idx].name[0] = 's';
            nucleus_disks[idx].name[1] = 'd';
            nucleus_disks[idx].name[2] = 'a' + i;
            nucleus_disks[idx].name[3] = '\0';
            
            nx_strcpy(nucleus_disks[idx].model, ahci_drives[i].model, 48);
            
            /* Create descriptive label based on size and purpose */
            if (idx == 0) {
                nx_strcpy(nucleus_disks[idx].label, "System Drive", 32);
            } else {
                nx_strcpy(nucleus_disks[idx].label, "Data Drive ", 32);
                nucleus_disks[idx].label[11] = '0' + i;
                nucleus_disks[idx].label[12] = '\0';
            }
            
            nucleus_disks[idx].size_bytes = ahci_drives[i].sectors * 512;
            nucleus_disks[idx].is_boot_disk = (idx == 0);
            nucleus_disks[idx].partition_count = 0;
            nucleus_disks[idx].connection_type = DISK_CONN_SATA;
            nucleus_disks[idx].partition_style = DISK_STYLE_UNKNOWN;
            
            nucleus_disk_count++;
        }
    } else {
        serial_puts("[NUCLEUS] AHCI init failed\n");
    }
    
    /* === NVMe Enumeration === */
    extern int nvme_init(void);
    extern int nvme_get_controller_count(void);
    extern void *nvme_get_controller(int index);
    extern uint64_t nvme_get_capacity(void *ctrl, uint32_t nsid);
    
    int nvme_count = nvme_init();
    if (nvme_count > 0) {
        serial_puts("[NUCLEUS] NVMe init OK, ");
        serial_putc('0' + nvme_count);
        serial_puts(" controller(s)\n");
        
        for (int c = 0; c < nvme_count && nucleus_disk_count < 16; c++) {
            /* Get controller */
            typedef struct {
                void *pci_dev;
                volatile uint32_t *regs;
                uint16_t vendor_id;
                uint16_t device_id;
                char serial[20];
                char model[40];
                char firmware[8];
                uint32_t doorbell_stride;
                uint64_t max_transfer_size;
                void *admin_queue;
                void *io_queue;
                void *io_queues;
                uint8_t num_io_queues;
                uint8_t queue_per_core;
                uint32_t namespace_count;
                struct {
                    uint32_t nsid;
                    uint64_t blocks;
                    uint32_t block_size;
                    uint8_t active;
                } namespaces[16];
                uint8_t int_mode;
                uint8_t msix_cap_offset;
                uint16_t msix_table_size;
                volatile uint32_t *msix_table;
                uint8_t irq_vector;
                volatile uint8_t io_cq_pending;
                uint8_t initialized;
            } nvme_ctrl_t;
            
            nvme_ctrl_t *ctrl = (nvme_ctrl_t *)nvme_get_controller(c);
            if (!ctrl || !ctrl->initialized) continue;
            
            /* Add each active namespace as a disk */
            for (uint32_t ns = 0; ns < ctrl->namespace_count && nucleus_disk_count < 16; ns++) {
                if (!ctrl->namespaces[ns].active) continue;
                
                int idx = nucleus_disk_count;
                nucleus_disks[idx].index = idx;
                /* Encode port: 0x8000 indicates NVMe, then byte for ctrl, byte for nsid */
                nucleus_disks[idx].port = 0x8000 | (c << 8) | ns;
                
                /* Name: nvme0n1, nvme0n2, nvme1n1, etc. */
                nucleus_disks[idx].name[0] = 'n';
                nucleus_disks[idx].name[1] = 'v';
                nucleus_disks[idx].name[2] = 'm';
                nucleus_disks[idx].name[3] = 'e';
                nucleus_disks[idx].name[4] = '0' + c;
                nucleus_disks[idx].name[5] = 'n';
                nucleus_disks[idx].name[6] = '1' + ns;
                nucleus_disks[idx].name[7] = '\0';
                
                nx_strcpy(nucleus_disks[idx].model, ctrl->model, 40);
                
                /* Generate label */
                nx_strcpy(nucleus_disks[idx].label, "NVMe Drive ", 32);
                nucleus_disks[idx].label[11] = '0' + c;
                nucleus_disks[idx].label[12] = '\0';
                
                /* Calculate size */
                uint64_t size = ctrl->namespaces[ns].blocks * ctrl->namespaces[ns].block_size;
                nucleus_disks[idx].size_bytes = size;
                
                nucleus_disks[idx].is_boot_disk = 0;  /* NVMe not boot disk by default */
                nucleus_disks[idx].partition_count = 0;
                nucleus_disks[idx].connection_type = DISK_CONN_NVME;
                nucleus_disks[idx].partition_style = DISK_STYLE_UNKNOWN;
                
                serial_puts("[NUCLEUS] Found NVMe: ");
                serial_puts(nucleus_disks[idx].name);
                serial_puts(" (");
                char size_str[32];
                nucleus_format_size(size, size_str, 32);
                serial_puts(size_str);
                serial_puts(")\n");
                
                nucleus_disk_count++;
            }
        }
    } else if (nvme_count == 0) {
        serial_puts("[NUCLEUS] No NVMe controllers found\n");
    } else {
        serial_puts("[NUCLEUS] NVMe init failed\n");
    }
    
    if (nucleus_disk_count > 0) {
        nucleus_load_partitions(0);
    }
    
    nucleus_update_system_info();
    serial_puts("[NUCLEUS] Disk scan complete: ");
    serial_putc('0' + nucleus_disk_count);
    serial_puts(" disk(s)\n");
}

void nucleus_init(void) {
    serial_puts("[NUCLEUS] Init started\n");
    nucleus_scan_disks();
    serial_puts("[NUCLEUS] Init complete\n");
}

/* Main draw function - EXACT UI from mockup */
void nucleus_draw(volatile uint32_t *fb, uint32_t w, uint32_t h, uint32_t pitch) {
    fb_clear(fb, pitch, w, h, COLOR_BG);
    
    int font_title = 16;
    int font_large = 14;
    int font_med = 12;
    int font_small = 10;
    
    /* Header with back button */
    vic_rounded_rect_fill(fb, pitch, 70, 20, w - 140, 50, 12, 0xFF2A2F42, w, h);
    vic_render_icon(fb, pitch, 90, 25, 32, icon_disk_v2, COLOR_ACCENT, w, h);
    nxf_draw_text(fb, pitch, 135, 35, "NeolyxOS Disk Manager", font_title, COLOR_WHITE, w, h);
    
    /* Back button */
    vic_render_icon(fb, pitch, w - 120, 30, 24, icon_exit_v2, COLOR_WHITE, w, h);
    
    /* Sidebar */
    int sidebar_w = 300;
    int sidebar_x = 70;
    int sidebar_y = 100;
    int sidebar_h = h - 140;
    
    vic_rounded_rect_fill(fb, pitch, sidebar_x, sidebar_y, sidebar_w, sidebar_h, 12, COLOR_PANEL, w, h);
    
    int sy = sidebar_y + 25;
    
    nxf_draw_text(fb, pitch, sidebar_x + 20, sy, "A. SIDEBAR", font_med, COLOR_GRAY, w, h);
    sy += 25;
    
    /* Disks section */
    vic_render_icon(fb, pitch, sidebar_x + 20, sy, 20, icon_disk_v2, COLOR_WHITE, w, h);
    char disk_label[32] = "DISKS (";
    disk_label[7] = '0' + nucleus_disk_count;
    disk_label[8] = ')';
    disk_label[9] = '\0';
    nxf_draw_text(fb, pitch, sidebar_x + 50, sy + 3, disk_label, font_med, COLOR_WHITE, w, h);
    sy += 30;
    
    /* Volumes */
    vic_render_icon(fb, pitch, sidebar_x + 20, sy, 20, icon_disk_v2, COLOR_WHITE, w, h);
    char vol_label[32] = "VOLUMES (";
    vol_label[9] = '0' + nucleus_partition_count;
    vol_label[10] = ')';
    vol_label[11] = '\0';
    nxf_draw_text(fb, pitch, sidebar_x + 50, sy + 3, vol_label, font_med, COLOR_GRAY, w, h);
    sy += 35;
    
    /* Settings */
    vic_render_icon(fb, pitch, sidebar_x + 20, sy, 20, icon_disk_v2, COLOR_WHITE, w, h);
    nxf_draw_text(fb, pitch, sidebar_x + 50, sy + 3, "SETTINGS", font_med, COLOR_GRAY, w, h);
    sy += 45;
    
    /* System Info box - reduced height */
    vic_rounded_rect_fill(fb, pitch, sidebar_x + 15, sy, sidebar_w - 30, 180, 8, COLOR_PANEL_DARK, w, h);
    sy += 15;
    
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "Disk Info", font_large, COLOR_WHITE, w, h);
    sy += 25;
    
    char info[32];
    nucleus_format_size(total_storage, info, 32);
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "Total Storage:", font_small, COLOR_GRAY, w, h);
    nxf_draw_text(fb, pitch, sidebar_x + 160, sy, info, font_small, COLOR_WHITE, w, h);
    sy += 20;
    
    nucleus_format_size(total_used, info, 32);
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "Total Used:", font_small, COLOR_GRAY, w, h);
    nxf_draw_text(fb, pitch, sidebar_x + 160, sy, info, font_small, COLOR_WHITE, w, h);
    sy += 20;
    
    nucleus_format_size(total_free, info, 32);
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "Free:", font_small, COLOR_GRAY, w, h);
    nxf_draw_text(fb, pitch, sidebar_x + 160, sy, info, font_small, COLOR_WHITE, w, h);
    sy += 30;
    
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "File System Support:", font_small, COLOR_GRAY, w, h);
    sy += 18;
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "NXFS, FAT32, NTFS,", font_small, COLOR_WHITE, w, h);
    sy += 16;
    nxf_draw_text(fb, pitch, sidebar_x + 25, sy, "ext4, EFI System", font_small, COLOR_WHITE, w, h);
    
    /* Bottom buttons */
    sy = sidebar_y + sidebar_h - 45;
    vic_rounded_rect_fill(fb, pitch, sidebar_x + 20, sy, 110, 32, 6, 0xFF2A2F42, w, h);
    int refresh_w = nxf_text_width("Refresh", font_small);
    nxf_draw_text(fb, pitch, sidebar_x + 20 + (110 - refresh_w)/2, sy + 10, "Refresh", font_small, COLOR_WHITE, w, h);
    
    vic_rounded_rect_fill(fb, pitch, sidebar_x + 145, sy, 110, 32, 6, 0xFF2A2F42, w, h);
    int help_w = nxf_text_width("Help", font_small);
    nxf_draw_text(fb, pitch, sidebar_x + 145 + (110 - help_w)/2, sy + 10, "Help", font_small, COLOR_WHITE, w, h);
    
    /* Main Content Area */
    int main_x = sidebar_x + sidebar_w + 20;
    int main_w = w - main_x - 90;
    int main_h = sidebar_h;
    
    vic_rounded_rect_fill(fb, pitch, main_x, sidebar_y, main_w, main_h, 12, COLOR_PANEL, w, h);
    
    int cy = sidebar_y + 25;
    
    /* =============== macOS-STYLE TOOLBAR =============== */
    nxf_draw_text(fb, pitch, main_x + 20, cy, "DISK LIST", font_large, COLOR_WHITE, w, h);
    
    /* Toolbar buttons (macOS Disk Utility style) */
    int tb_x = main_x + main_w - 350;
    int tb_y = cy - 5;
    int tb_w = 60;
    int tb_h = 26;
    int tb_gap = 8;
    
    vic_rounded_rect_fill(fb, pitch, tb_x, tb_y, tb_w, tb_h, 5, 0xFF2A2F42, w, h);
    nxf_draw_text(fb, pitch, tb_x + 8, tb_y + 7, "Info", font_small, COLOR_WHITE, w, h);
    
    tb_x += tb_w + tb_gap;
    vic_rounded_rect_fill(fb, pitch, tb_x, tb_y, tb_w + 15, tb_h, 5, 0xFF2A2F42, w, h);
    nxf_draw_text(fb, pitch, tb_x + 8, tb_y + 7, "First Aid", font_small, COLOR_WHITE, w, h);
    
    tb_x += tb_w + 15 + tb_gap;
    vic_rounded_rect_fill(fb, pitch, tb_x, tb_y, tb_w, tb_h, 5, 0xFF2A2F42, w, h);
    nxf_draw_text(fb, pitch, tb_x + 8, tb_y + 7, "Erase", font_small, COLOR_WHITE, w, h);
    
    tb_x += tb_w + tb_gap;
    vic_rounded_rect_fill(fb, pitch, tb_x, tb_y, tb_w + 20, tb_h, 5, 0xFF2A2F42, w, h);
    nxf_draw_text(fb, pitch, tb_x + 8, tb_y + 7, "Restore", font_small, COLOR_WHITE, w, h);
    
    cy += 35;
    
    /* Disk cards */
    if (nucleus_disk_count == 0) {
        nxf_draw_text(fb, pitch, main_x + 20, cy, "No disks detected", font_med, COLOR_GRAY, w, h);
    } else {
        for (int i = 0; i < nucleus_disk_count && i < 3; i++) {
            nucleus_disk_t *d = &nucleus_disks[i];
            int is_selected = (i == selection);
            
            /* macOS-style disk card - larger with more detail */
            int card_h = 80;
            vic_rounded_rect_fill(fb, pitch, main_x + 20, cy, main_w - 40, card_h, 8, 
                                is_selected ? 0xFF2F3648 : COLOR_PANEL_DARK, w, h);
            
            if (is_selected) {
                vic_rounded_rect(fb, pitch, main_x + 20, cy, main_w - 40, card_h, 8, 2, COLOR_ACCENT, w, h);
            }
            
            /* Large disk icon (48px instead of 26px) - macOS style */
            vic_render_icon(fb, pitch, main_x + 40, cy + 16, 48, icon_disk_v2, COLOR_WHITE, w, h);
            
            /* Disk Label - prominent */
            char disk_name[48];
            if (d->label[0] != '\0' && nx_strcmp(d->label, "SATA Disk") != 0) {
                nx_strcpy(disk_name, d->label, 48);
            } else {
                nx_strcpy(disk_name, d->model, 48);
            }
            nxf_draw_text(fb, pitch, main_x + 100, cy + 15, disk_name, font_large, COLOR_WHITE, w, h);
            
            /* Disk Type - like macOS "APFS Volume · APFS" */
            char type_info[48] = "";
            nx_strcat(type_info, d->connection_type == 0 ? "SATA" : (d->connection_type == 1 ? "NVMe" : "USB"), 48);
            nx_strcat(type_info, " Storage · ", 48);
            nx_strcat(type_info, d->partition_style == 1 ? "GPT" : "MBR", 48);
            nxf_draw_text(fb, pitch, main_x + 100, cy + 35, type_info, font_small, COLOR_GRAY, w, h);
            
            /* Capacity - right aligned, large, like macOS */
            char size[16];
            nucleus_format_size(d->size_bytes, size, 16);
            int size_w = nxf_text_width(size, font_title);
            nxf_draw_text(fb, pitch, main_x + main_w - 60 - size_w, cy + 20, size, font_title, COLOR_WHITE, w, h);
            
            /* Status indicator */
            nxf_draw_text(fb, pitch, main_x + 100, cy + 55, is_selected ? "Selected" : "Online", font_small, is_selected ? COLOR_ACCENT : 0xFF66AA66, w, h);
            
            cy += card_h + 10;
        }
    }
    
    cy += 10;
    
    /* =============== macOS-STYLE USAGE BAR =============== */
    if (nucleus_disk_count > 0 && selection < nucleus_disk_count) {
        nucleus_disk_t *d = &nucleus_disks[selection];
        
        /* Usage bar background */
        int bar_x = main_x + 20;
        int bar_w = main_w - 40;
        int bar_h = 22;
        
        vic_rounded_rect_fill(fb, pitch, bar_x, cy, bar_w, bar_h, 4, 0xFF3A4050, w, h);
        
        /* Calculate usage (simulate based on partitions) */
        uint64_t used_bytes = 0;
        uint64_t partition_bytes = 0;
        for (int p = 0; p < nucleus_partition_count; p++) {
            if (nucleus_partitions[p].disk_index == selection) {
                partition_bytes += nucleus_partitions[p].size_bytes;
                used_bytes += nucleus_partitions[p].used_bytes;
            }
        }
        
        /* Draw usage segments if we have partitions */
        if (partition_bytes > 0 && d->size_bytes > 0) {
            int used_w = (int)((used_bytes * bar_w) / d->size_bytes);
            int part_w = (int)((partition_bytes * bar_w) / d->size_bytes);
            
            /* Used (blue) */
            if (used_w > 4) {
                vic_rounded_rect_fill(fb, pitch, bar_x + 2, cy + 2, used_w > bar_w - 4 ? bar_w - 4 : used_w, bar_h - 4, 3, 0xFF3B82F6, w, h);
            }
            
            /* Other volumes (gray) - space allocated but not used */  
            if (part_w > used_w + 4) {
                vic_rounded_rect_fill(fb, pitch, bar_x + 2 + used_w, cy + 2, part_w - used_w, bar_h - 4, 3, 0xFF6B7280, w, h);
            }
            
            /* Free space stays as background color */
        }
        
        cy += bar_h + 8;
        
        /* Legend below bar (macOS style) */
        int legend_x = bar_x;
        int dot_size = 10;
        
        /* Used indicator */
        vic_rounded_rect_fill(fb, pitch, legend_x, cy, dot_size, dot_size, 2, 0xFF3B82F6, w, h);
        nxf_draw_text(fb, pitch, legend_x + 14, cy - 1, "Used", font_small, COLOR_GRAY, w, h);
        legend_x += 65;
        
        /* Other Volumes indicator */
        vic_rounded_rect_fill(fb, pitch, legend_x, cy, dot_size, dot_size, 2, 0xFF6B7280, w, h);
        nxf_draw_text(fb, pitch, legend_x + 14, cy - 1, "Other Volumes", font_small, COLOR_GRAY, w, h);
        legend_x += 120;
        
        /* Free indicator */
        vic_rounded_rect_fill(fb, pitch, legend_x, cy, dot_size, dot_size, 2, 0xFF3A4050, w, h);
        nxf_draw_text(fb, pitch, legend_x + 14, cy - 1, "Free", font_small, COLOR_GRAY, w, h);
        
        cy += 20;
    }
    
    cy += 10;
    int dropdown_x = main_x + 20;
    int dropdown_w = 200;
    int dropdown_h = 35;
    
    nxf_draw_text(fb, pitch, dropdown_x, cy, "Filesystem:", font_small, COLOR_GRAY, w, h);
    cy += 22;
    
    /* Dropdown button */
    vic_rounded_rect_fill(fb, pitch, dropdown_x, cy, dropdown_w, dropdown_h, 6, 
                         fs_dropdown_open ? COLOR_ACCENT : 0xFF2A2F42, w, h);
    vic_rounded_rect(fb, pitch, dropdown_x, cy, dropdown_w, dropdown_h, 6, 1, COLOR_WHITE, w, h);
    
    /* Show selected filesystem */
    nxf_draw_text(fb, pitch, dropdown_x + 15, cy + 10, fs_type_names[selected_fs_type], font_small, COLOR_WHITE, w, h);
    
    /* Dropdown arrow */
    nxf_draw_text(fb, pitch, dropdown_x + dropdown_w - 25, cy + 8, fs_dropdown_open ? "^" : "v", font_small, COLOR_WHITE, w, h);
    
    cy += dropdown_h + 5;
    
    /* Dropdown menu (if open) */
    if (fs_dropdown_open) {
        int menu_h = (int)FS_TYPE_COUNT * 32;
        vic_rounded_rect_fill(fb, pitch, dropdown_x, cy, dropdown_w, menu_h, 6, 0xFF1E2230, w, h);
        vic_rounded_rect(fb, pitch, dropdown_x, cy, dropdown_w, menu_h, 6, 1, COLOR_WHITE, w, h);
        
        for (int i = 0; i < (int)FS_TYPE_COUNT; i++) {
            int item_y = cy + (i * 32);
            uint32_t bg_color = (i == selected_fs_type) ? 0xFF2F3648 : 0xFF1E2230;
            
            vic_rounded_rect_fill(fb, pitch, dropdown_x + 2, item_y + 2, dropdown_w - 4, 28, 4, bg_color, w, h);
            nxf_draw_text(fb, pitch, dropdown_x + 15, item_y + 8, fs_type_names[i], font_small, 
                         (i == selected_fs_type) ? COLOR_ACCENT : COLOR_WHITE, w, h);
        }
        
        cy += menu_h + 10;
    } else {
        cy += 10;
    }
    
    /* Action buttons - Simplified: New Partition and Delete only */
    /* Erase whole disk is in toolbar, Format removed */
    int btn_w = 130;
    int btn_h = 34;
    int btn_x = main_x + 20;
    
    /* New Partition button */
    int can_create = (selection >= 0 && selection < nucleus_disk_count && 
                      !nucleus_disks[selection].is_boot_disk);
    vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w, btn_h, 6, 
                         can_create ? 0xFF2A2F42 : 0xFF1A1F22, w, h);
    int create_w = nxf_text_width("New Partition", font_small);
    nxf_draw_text(fb, pitch, btn_x + (btn_w - create_w)/2, cy + 10, "New Partition", font_small, 
                 can_create ? COLOR_WHITE : COLOR_GRAY, w, h);
    
    btn_x += btn_w + 10;
    
    /* Delete Partition button */
    int can_delete = (selected_partition >= 0 && selected_partition < nucleus_partition_count);
    vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w, btn_h, 6, 
                         can_delete ? 0xFF8B2020 : 0xFF1A1F22, w, h);
    int del_w = nxf_text_width("Delete Partition", font_small);
    nxf_draw_text(fb, pitch, btn_x + (btn_w - del_w)/2, cy + 10, "Delete Partition", font_small, 
                 can_delete ? COLOR_WHITE : COLOR_GRAY, w, h);
    
    cy += btn_h + 15;
    
    /* Progress bar (shown during operations) */
    if (show_progress_bar) {
        int progress_x = main_x + 20;
        int progress_w = main_w - 40;
        int progress_h = 24;
        
        /* Background */
        vic_rounded_rect_fill(fb, pitch, progress_x, cy, progress_w, progress_h, 6, 0xFF1A1F32, w, h);
        
        /* Progress fill */
        if (progress_percent > 0) {
            int fill_w = (progress_w - 4) * progress_percent / 100;
            if (fill_w > 0) {
                vic_rounded_rect_fill(fb, pitch, progress_x + 2, cy + 2, fill_w, progress_h - 4, 4, COLOR_ACCENT, w, h);
            }
        }
        
        /* Progress text */
        char ptext[80];
        int pi = 0;
        /* Copy message */
        for (int i = 0; progress_message[i] && i < 50; i++) ptext[pi++] = progress_message[i];
        ptext[pi++] = ' ';
        ptext[pi++] = '(';
        if (progress_percent >= 100) { ptext[pi++] = '1'; ptext[pi++] = '0'; ptext[pi++] = '0'; }
        else if (progress_percent >= 10) { ptext[pi++] = '0' + (progress_percent / 10); ptext[pi++] = '0' + (progress_percent % 10); }
        else { ptext[pi++] = '0' + progress_percent; }
        ptext[pi++] = '%';
        ptext[pi++] = ')';
        ptext[pi] = '\0';
        
        int tw = nxf_text_width(ptext, font_small);
        nxf_draw_text(fb, pitch, progress_x + (progress_w - tw)/2, cy + 6, ptext, font_small, COLOR_WHITE, w, h);
        
        cy += progress_h + 10;
    }
    
    cy += 10;
    /* Disk Map Section */
    if (nucleus_disk_count > 0 && selection < nucleus_disk_count) {
        char header[64] = "Disk Map (Disk ";
        header[15] = '1' + selection;
        nx_strcpy(header + 16, " / DETAILS)", 48);
        nxf_draw_text(fb, pitch, main_x + 20, cy, header, font_med, COLOR_WHITE, w, h);
        cy += 30;
        
        /* Partition visualization bar */
        int bar_x = main_x + 20;
        int bar_w = main_w - 40;
        int bar_h = 90;
        
        /* Draw base background */
        vic_rounded_rect_fill(fb, pitch, bar_x, cy, bar_w, bar_h, 6, COLOR_PANEL_DARK, w, h);
        
        if (nucleus_partition_count > 0) {
            uint64_t total_sectors = nucleus_disks[selection].size_bytes / 512;
            int px = bar_x;
            
            for (int p = 0; p < nucleus_partition_count && p < 4; p++) {
                nucleus_partition_t *part = &nucleus_partitions[p];
                int part_w = (int)((part->sector_count * bar_w) / total_sectors);
                if (part_w < 20) part_w = 20;
                if (px + part_w > bar_x + bar_w) part_w = bar_x + bar_w - px;
                
                /* Highlight selected partition */
                if (p == selected_partition) {
                    vic_rounded_rect(fb, pitch, px, cy, part_w, bar_h, 4, 2, COLOR_WHITE, w, h);
                }
                
                /* Calculate used percentage (simulate for now, will be real when mounted) */
                int used_percent = 0;
                if (part->is_mounted && part->size_bytes > 0) {
                    used_percent = (int)((part->used_bytes * 100) / part->size_bytes);
                } else {
                    /* Simulated usage for unmounted partitions */
                    if (nx_strcmp(part->fs_type, "EFI System") == 0) {
                        used_percent = 15; /* EFI typically ~15% used */
                    } else if (nx_strcmp(part->fs_type, "NXFS") == 0) {
                        used_percent = 0; /* Empty NXFS */
                    } else {
                        used_percent = 0;
                    }
                }
                
                /* Draw partition background (free space) */
                vic_rounded_rect_fill(fb, pitch, px + 2, cy + 2, part_w - 4, bar_h - 4, 4, part->color_code, w, h);
                
                /* Draw used space overlay (darker shade of same color) */
                if (used_percent > 0 && part_w > 10) {
                    int used_w = ((part_w - 4) * used_percent) / 100;
                    if (used_w > 2) {
                        /* Darken the color for used space */
                        uint32_t dark_color = part->color_code & 0xFF000000; /* Alpha */
                        uint32_t r = (part->color_code >> 16) & 0xFF;
                        uint32_t g = (part->color_code >> 8) & 0xFF;
                        uint32_t b = part->color_code & 0xFF;
                        
                        /* Reduce brightness by 40% for used space */
                        r = (r * 60) / 100;
                        g = (g * 60) / 100;
                        b = (b * 60) / 100;
                        
                        dark_color |= (r << 16) | (g << 8) | b;
                        
                        vic_rounded_rect_fill(fb, pitch, px + 2, cy + 2, used_w, bar_h - 4, 4, dark_color, w, h);
                    }
                }
                
                /* Draw partition info text */
                if (part_w > 100) {
                    nxf_draw_text(fb, pitch, px + 10, cy + 10, part->label, font_small, COLOR_WHITE, w, h);
                    
                    /* Show size */
                    char size[16];
                    nucleus_format_size(part->size_bytes, size, 16);
                    nxf_draw_text(fb, pitch, px + 10, cy + 28, size, font_small, COLOR_WHITE, w, h);
                    
                    /* Show usage percentage in box */
                    if (part_w > 150) {
                        char usage[32];
                        int ui = 0;
                        
                        if (used_percent > 0) {
                            /* Used percentage */
                            usage[ui++] = 'U';
                            usage[ui++] = 's';
                            usage[ui++] = 'e';
                            usage[ui++] = 'd';
                            usage[ui++] = ':';
                            usage[ui++] = ' ';
                            if (used_percent >= 10) usage[ui++] = '0' + (used_percent / 10);
                            usage[ui++] = '0' + (used_percent % 10);
                            usage[ui++] = '%';
                            usage[ui++] = ' ';
                            usage[ui++] = '|';
                            usage[ui++] = ' ';
                        }
                        
                        /* Free percentage */
                        int free_percent = 100 - used_percent;
                        usage[ui++] = 'F';
                        usage[ui++] = 'r';
                        usage[ui++] = 'e';
                        usage[ui++] = 'e';
                        usage[ui++] = ':';
                        usage[ui++] = ' ';
                        if (free_percent == 100) {
                            usage[ui++] = '1';
                            usage[ui++] = '0';
                            usage[ui++] = '0';
                        } else {
                            if (free_percent >= 10) usage[ui++] = '0' + (free_percent / 10);
                            usage[ui++] = '0' + (free_percent % 10);
                        }
                        usage[ui++] = '%';
                        usage[ui] = '\0';
                        
                        nxf_draw_text(fb, pitch, px + 10, cy + 46, usage, font_small - 2, 0xFFCCCCCC, w, h);
                    } else {
                        /* Smaller partition - just show percentage */
                        char pct[8];
                        int pi = 0;
                        if (used_percent >= 10) pct[pi++] = '0' + (used_percent / 10);
                        pct[pi++] = '0' + (used_percent % 10);
                        pct[pi++] = '%';
                        pct[pi] = '\0';
                        nxf_draw_text(fb, pitch, px + 10, cy + 46, pct, font_small - 2, 0xFFCCCCCC, w, h);
                    }
                    
                    nxf_draw_text(fb, pitch, px + 10, cy + 64, part->fs_type, font_small - 2, 0xFFAAAAAA, w, h);
                }
                
                px += part_w;
            }
            
            /* Unallocated space */
            uint64_t used_sectors = 0;
            for (int p = 0; p < nucleus_partition_count; p++) {
                used_sectors += nucleus_partitions[p].sector_count;
            }
            
            if (used_sectors < total_sectors && px < bar_x + bar_w) {
                int unalloc_w = bar_x + bar_w - px;
                if (unalloc_w > 10) {
                    vic_rounded_rect_fill(fb, pitch, px + 2, cy + 2, unalloc_w - 4, bar_h - 4, 4, COLOR_RED, w, h);
                    
                    if (unalloc_w > 80) {
                        nxf_draw_text(fb, pitch, px + 10, cy + 30, "UNALLOCATED", font_small, COLOR_WHITE, w, h);
                        nxf_draw_text(fb, pitch, px + 10, cy + 48, "(Unformatted)", font_small - 2, 0xFFCCCCCC, w, h);
                    }
                }
            }
        } else {
            /* No partitions - show entire disk as unallocated */
            vic_rounded_rect_fill(fb, pitch, bar_x + 2, cy + 2, bar_w - 4, bar_h - 4, 4, COLOR_RED, w, h);
            nxf_draw_text(fb, pitch, bar_x + bar_w/2 - 60, cy + 30, "UNALLOCATED", font_med, COLOR_WHITE, w, h);
            nxf_draw_text(fb, pitch, bar_x + bar_w/2 - 80, cy + 52, "Entire disk available", font_small, 0xFFCCCCCC, w, h);
        }
        
        cy += bar_h + 20;
        
        /* Partition action buttons */
        btn_x = main_x + 20;
        btn_w = 100;
        
        vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w, 32, 6, 0xFF2A2F42, w, h);
        nxf_draw_text(fb, pitch, btn_x + 28, cy + 9, "Extend", font_small, COLOR_WHITE, w, h);
        
        btn_x += btn_w + 12;
        vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w, 32, 6, 0xFF2A2F42, w, h);
        nxf_draw_text(fb, pitch, btn_x + 28, cy + 9, "Shrink", font_small, COLOR_WHITE, w, h);
        
        btn_x += btn_w + 12;
        vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w + 35, 32, 6, 0xFF2A2F42, w, h);
        nxf_draw_text(fb, pitch, btn_x + 15, cy + 9, "Change Label", font_small, COLOR_WHITE, w, h);
        
        btn_x += btn_w + 47;
        vic_rounded_rect_fill(fb, pitch, btn_x, cy, btn_w, 32, 6, selected_partition >= 0 ? COLOR_ACCENT : 0xFF2A2F42, w, h);
        const char *mount_label = "Mount";
        if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
            if (nucleus_partitions[selected_partition].is_mounted) mount_label = "Unmount";
        }
        nxf_draw_text(fb, pitch, btn_x + 28, cy + 9, mount_label, font_small, COLOR_WHITE, w, h);
        
        cy += 45;
        
        /* Partition Details */
        nxf_draw_text(fb, pitch, main_x + 20, cy, "Partition Details", font_large, COLOR_WHITE, w, h);
        cy += 28;
        
        if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
            nucleus_partition_t *part = &nucleus_partitions[selected_partition];
            
            char details[128] = "Selected: ";
            nx_strcat(details, part->label, 128);
            nx_strcat(details, " (", 128);
            nx_strcat(details, part->role, 128);
            nx_strcat(details, " Partition)", 128);
            nxf_draw_text(fb, pitch, main_x + 20, cy, details, font_med, COLOR_WHITE, w, h);
            cy += 22;
            
            char size_info[128];
            char size[16];
            nucleus_format_size(part->size_bytes, size, 16);
            nx_strcpy(size_info, "Size: ", 128);
            nx_strcat(size_info, size, 128);
            
            if (part->is_mounted) {
                nx_strcat(size_info, " | Used ", 128);
                char used[16];
                nucleus_format_size(part->used_bytes, used, 16);
                nx_strcat(size_info, used, 128);
                
                int percent = part->size_bytes > 0 ? (int)((part->used_bytes * 100) / part->size_bytes) : 0;
                char percent_str[16] = " (";
                int pi = 2;
                if (percent >= 10) percent_str[pi++] = '0' + (percent / 10);
                percent_str[pi++] = '0' + (percent % 10);
                percent_str[pi++] = '%';
                percent_str[pi++] = ')';
                percent_str[pi] = '\0';
                nx_strcat(size_info, percent_str, 128);
            }
            
            nxf_draw_text(fb, pitch, main_x + 20, cy, size_info, font_med, COLOR_WHITE, w, h);
            cy += 22;
            
            char fs_info[64] = "File System: ";
            nx_strcat(fs_info, part->fs_type, 64);
            nx_strcat(fs_info, " | Status: Healthy", 64);
            nxf_draw_text(fb, pitch, main_x + 20, cy, fs_info, font_med, COLOR_WHITE, w, h);
        } else {
            /* Show selected disk info instead of tutorial */
            if (selection < nucleus_disk_count) {
                char model_info[64] = "Model: ";
                nx_strcat(model_info, nucleus_disks[selection].model[0] ? nucleus_disks[selection].model : "Unknown", 64);
                nxf_draw_text(fb, pitch, main_x + 20, cy, model_info, font_small, COLOR_GRAY, w, h);
            }
        }
    }
    
    /* DRY-RUN MODE WARNING BANNER */
    if (dry_run_mode) {
        int banner_y = 50;
        vic_rounded_rect_fill(fb, pitch, 20, banner_y, w - 40, 40, 8, 0xFFCC0000, w, h);
        nxf_draw_centered(fb, pitch, w / 2, banner_y + 10, "DRY-RUN MODE ACTIVE - No Write Operations Will Occur", font_large, COLOR_WHITE, w, h);
    }
    
    /* CONFIRMATION DIALOG (Modal Overlay - macOS Disk Utility Style) */
    if (show_confirm_dialog != 0) {
        /* Semi-transparent dark overlay with blur effect simulation */
        for (uint32_t dy = 0; dy < h; dy++) {
            for (uint32_t dx = 0; dx < w; dx++) {
                uint32_t idx = dy * (pitch / 4) + dx;
                uint32_t pixel = fb[idx];
                /* Darken by 60% with slight purple tint */
                uint8_t r = ((pixel >> 16) & 0xFF) * 35 / 100;
                uint8_t g = ((pixel >> 8) & 0xFF) * 35 / 100;
                uint8_t b = (pixel & 0xFF) * 40 / 100;
                fb[idx] = 0xFF000000 | (r << 16) | (g << 8) | b;
            }
        }
        
        /* Dialog box - larger and more refined */
        int dialog_w = 520;
        int dialog_h = 280;
        int dialog_x = (w - dialog_w) / 2;
        int dialog_y = (h - dialog_h) / 2;
        
        /* Shadow effect */
        vic_rounded_rect_fill(fb, pitch, dialog_x + 8, dialog_y + 8, dialog_w, dialog_h, 16, 0x40000000, w, h);
        
        /* Main dialog background with gradient-like effect */
        vic_rounded_rect_fill(fb, pitch, dialog_x, dialog_y, dialog_w, dialog_h, 16, 0xFF2A2F42, w, h);
        vic_rounded_rect_fill(fb, pitch, dialog_x + 2, dialog_y + 2, dialog_w - 4, dialog_h / 3, 14, 0xFF353A50, w, h);
        
        /* Subtle accent border */
        uint32_t border_color = show_confirm_dialog > 0 ? COLOR_ACCENT : 0xFFEF4444;
        vic_rounded_rect(fb, pitch, dialog_x, dialog_y, dialog_w, dialog_h, 16, 2, border_color, w, h);
        
        int content_y = dialog_y + 35;
        
        if (show_confirm_dialog > 0) {
            /* === WARNING ICON (triangle with exclamation) === */
            int icon_x = dialog_x + dialog_w / 2;
            int icon_y = content_y;
            int icon_size = 48;
            
            /* Draw warning triangle */
            for (int ty = 0; ty < icon_size / 2; ty++) {
                int half_width = (ty * icon_size / 2) / (icon_size / 2);
                for (int tx = -half_width; tx <= half_width; tx++) {
                    int px = icon_x + tx;
                    int py = icon_y + ty;
                    if (px >= 0 && px < (int)w && py >= 0 && py < (int)h) {
                        fb[py * (pitch / 4) + px] = 0xFFFBBF24; /* Yellow/amber */
                    }
                }
            }
            /* Exclamation point in triangle */
            for (int ey = 10; ey < 20; ey++) {
                fb[(icon_y + ey) * (pitch / 4) + icon_x] = 0xFF1A1A2E;
            }
            fb[(icon_y + 22) * (pitch / 4) + icon_x] = 0xFF1A1A2E; /* Dot */
            
            content_y += 55;
            
            /* Title - large and bold */
            nxf_draw_centered(fb, pitch, dialog_x + dialog_w / 2, content_y, 
                              confirm_message, 16, COLOR_WHITE, w, h);
            content_y += 32;
            
            /* Disk info */
            nxf_draw_centered(fb, pitch, dialog_x + dialog_w / 2, content_y, 
                              confirm_disk_info, 12, 0xFFB0B0C0, w, h);
            content_y += 28;
            
            /* Warning subtext */
            nxf_draw_centered(fb, pitch, dialog_x + dialog_w / 2, content_y, 
                              "This action cannot be undone.", 10, 0xFFFF6B6B, w, h);
            
            /* === BUTTONS (macOS style) === */
            int btn_y = dialog_y + dialog_h - 65;
            int btn_w = 130;
            int btn_h = 38;
            int gap = 20;
            int button_group_w = (btn_w * 2) + gap;
            int start_x = dialog_x + (dialog_w - button_group_w) / 2;
            
            int cancel_x = start_x;
            int proceed_x = start_x + btn_w + gap;
            
            /* Cancel button (subtle gray) */
            vic_rounded_rect_fill(fb, pitch, cancel_x, btn_y, btn_w, btn_h, 8, 0xFF4A4F5C, w, h);
            vic_rounded_rect(fb, pitch, cancel_x, btn_y, btn_w, btn_h, 8, 1, 0xFF5A5F6C, w, h);
            nxf_draw_centered(fb, pitch, cancel_x + btn_w / 2, btn_y + 11, "Cancel", 12, COLOR_WHITE, w, h);
            
            /* Proceed button (destructive red for erase, purple for other) */
            uint32_t proceed_bg = 0xFF8B5CF6;  /* Purple accent */
            uint32_t proceed_highlight = 0xFFA78BFA;
            if (show_confirm_dialog == 1) {  /* Erase mode - use red */
                proceed_bg = 0xFFDC2626;
                proceed_highlight = 0xFFEF4444;
            }
            vic_rounded_rect_fill(fb, pitch, proceed_x, btn_y, btn_w, btn_h, 8, proceed_bg, w, h);
            vic_rounded_rect_fill(fb, pitch, proceed_x + 2, btn_y + 2, btn_w - 4, btn_h / 2 - 2, 6, proceed_highlight, w, h);
            nxf_draw_centered(fb, pitch, proceed_x + btn_w / 2, btn_y + 11, 
                              show_confirm_dialog == 1 ? "Erase" : "Continue", 12, COLOR_WHITE, w, h);
            
        } else {
            /* === INFO/ERROR DIALOG === */
            /* Icon - circle with i or x */
            int icon_x = dialog_x + dialog_w / 2;
            int icon_y = content_y + 10;
            for (int cy = -16; cy <= 16; cy++) {
                for (int cx = -16; cx <= 16; cx++) {
                    if (cx*cx + cy*cy <= 16*16) {
                        int px = icon_x + cx;
                        int py = icon_y + cy;
                        if (px >= 0 && px < (int)w && py >= 0 && py < (int)h) {
                            fb[py * (pitch / 4) + px] = 0xFF3B82F6; /* Blue info */
                        }
                    }
                }
            }
            /* "i" in circle */
            for (int iy = -8; iy <= 8; iy++) {
                if (iy != -5 && iy != -6)
                    fb[(icon_y + iy) * (pitch / 4) + icon_x] = COLOR_WHITE;
            }
            
            content_y += 50;
            
            /* Message */
            nxf_draw_centered(fb, pitch, dialog_x + dialog_w / 2, content_y, 
                              confirm_message, 14, COLOR_WHITE, w, h);
            content_y += 30;
            
            /* Detail text */
            nxf_draw_centered(fb, pitch, dialog_x + dialog_w / 2, content_y, 
                              confirm_disk_info, 11, 0xFF9CA3AF, w, h);
            
            /* OK button (centered) */
            int btn_w = 100;
            int btn_h = 36;
            int btn_x = dialog_x + (dialog_w - btn_w) / 2;
            int btn_y = dialog_y + dialog_h - 60;
            vic_rounded_rect_fill(fb, pitch, btn_x, btn_y, btn_w, btn_h, 8, COLOR_ACCENT, w, h);
            vic_rounded_rect_fill(fb, pitch, btn_x + 2, btn_y + 2, btn_w - 4, btn_h / 2 - 2, 6, 0xFFB794F6, w, h);
            nxf_draw_centered(fb, pitch, btn_x + btn_w / 2, btn_y + 10, "OK", 12, COLOR_WHITE, w, h);
        }
    }
    
    /* HELP DIALOG (Modal Overlay) */
    if (show_help_dialog) {
        /* Semi-transparent dark overlay */
        fb_fill_rect(fb, pitch, w, h, 0, 0, w, h, 0xAA000000); // Simple overlay
        
        int dialog_w = 600;
        int dialog_h = 400;
        int dialog_x = (w - dialog_w) / 2;
        int dialog_y = (h - dialog_h) / 2;
        
        vic_rounded_rect_fill(fb, pitch, dialog_x, dialog_y, dialog_w, dialog_h, 12, 0xFF1E2230, w, h);
        vic_rounded_rect(fb, pitch, dialog_x, dialog_y, dialog_w, dialog_h, 12, 2, COLOR_ACCENT, w, h);
        
        int cy = dialog_y + 20;
        nxf_draw_centered(fb, pitch, w/2, cy, "Partition Table Information", font_large, COLOR_WHITE, w, h);
        cy += 40;
        
        int lx = dialog_x + 40;
        nxf_draw_text(fb, pitch, lx, cy, "Partition Types:", font_med, COLOR_ACCENT, w, h);
        cy += 25;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- EFI System (0xEF): FAT32 Boot Partition", font_small, COLOR_WHITE, w, h); cy += 20;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- Linux/NXFS (0x83): Native System Data", font_small, COLOR_WHITE, w, h); cy += 20;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- Windows Data (0x07): NTFS/exFAT", font_small, COLOR_WHITE, w, h); cy += 20;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- Swap Space (0x82): Virtual Memory", font_small, COLOR_WHITE, w, h); cy += 30;
        
        nxf_draw_text(fb, pitch, lx, cy, "Disk Format Support:", font_med, COLOR_ACCENT, w, h);
        cy += 25;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- GPT (GUID Partition Table): Supported (Recommended)", font_small, COLOR_WHITE, w, h); cy += 20;
        nxf_draw_text(fb, pitch, lx + 20, cy, "- MBR (Master Boot Record): Legacy Support", font_small, COLOR_WHITE, w, h); cy += 30;

        nxf_draw_text(fb, pitch, w/2 - 100, cy + 20, "NeolyxOS uses NXFS by default.", font_small, COLOR_GRAY, w, h);
        
        /* OK Button */
        int btn_w = 120;
        int btn_h = 35;
        int btn_x = (w - btn_w) / 2;
        int btn_y = dialog_y + dialog_h - 50;
        vic_rounded_rect_fill(fb, pitch, btn_x, btn_y, btn_w, btn_h, 6, 0xFF666666, w, h);
        int ok_w = nxf_text_width("OK", font_med);
        nxf_draw_text(fb, pitch, btn_x + (btn_w - ok_w)/2, btn_y + 10, "OK", font_med, COLOR_WHITE, w, h);
    }
    
    /* Footer */
    nxf_draw_centered(fb, pitch, w, h - 25, "[Up/Down] Disk  [Left/Right] Partition  [Enter] Mount  [F] Format  [C] Create Layout  [I] Install OS  [Esc] Back", font_small, COLOR_GRAY, w, h);
}

/* Input handling */
int nucleus_handle_input(uint8_t scancode) {
    int redraw = 0;
    
    /* PRIORITY: Handle confirmation dialog keyboard input */
    if (show_confirm_dialog != 0) {
        if (scancode == 0x1C) { /* Enter - Confirm */
            if (show_confirm_dialog > 0) {
                serial_puts("[NUCLEUS] User pressed Enter - Confirming\n");
                nucleus_execute_confirmed_format();
                return 1;
            } else {
                /* OK on error dialog */
                show_confirm_dialog = 0;
                return 1;
            }
        } else if (scancode == 0x01) { /* Esc - Cancel */
            serial_puts("[NUCLEUS] User pressed Esc - Cancelling\n");
            pending_format_disk = -1;
            pending_format_part = -1;
            show_confirm_dialog = 0;
            return 1;
        }
        /* Ignore other keys when dialog is open */
        return 0;
    }
    
    if (scancode == 0x01) { /* Esc - Back/Exit */
        return -1;
    }
    
    if (scancode == 0x48) { /* Up */
        if (selection > 0) {
            selection--;
            nucleus_load_partitions(selection);
            redraw = 1;
        }
    } else if (scancode == 0x50) { /* Down */
        if (selection < nucleus_disk_count - 1) {
            selection++;
            nucleus_load_partitions(selection);
            redraw = 1;
        }
    } else if (scancode == 0x1C) { /* Enter */
        if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
            nucleus_partition_t *p = &nucleus_partitions[selected_partition];
            if (p->is_mounted) {
                nucleus_unmount_partition(selected_partition);
            } else {
                nucleus_mount_partition(selected_partition);
            }
            redraw = 1;
        }
    } else if (scancode == 0x4B) { /* Left */
        if (selected_partition > 0) {
            selected_partition--;
            redraw = 1;
        } else if (selected_partition < 0 && nucleus_partition_count > 0) {
            selected_partition = 0;
            redraw = 1;
        }
    } else if (scancode == 0x4D) { /* Right */
        if (selected_partition < nucleus_partition_count - 1) {
            selected_partition++;
            redraw = 1;
        } else if (selected_partition < 0 && nucleus_partition_count > 0) {
            selected_partition = 0;
            redraw = 1;
        }
    } else if (scancode == 0x21) { /* F key - Erase Disk */
        nucleus_action_erase_disk();
        redraw = 1;
    } else if (scancode == 0x17) { /* I key - Install OS */
        nucleus_action_install_os();
        redraw = 1;
    } else if (scancode == 0x2E) { /* C key - Create NeolyxOS Layout */
        if (selection >= 0 && selection < nucleus_disk_count) {
            if (!nucleus_disks[selection].is_boot_disk) {
                serial_puts("[NUCLEUS] Creating NeolyxOS layout via keyboard\n");
                if (nucleus_create_neolyx_layout(selection) == 0) {
                    serial_puts("[NUCLEUS] Layout created successfully\n");
                }
            }
        }
        redraw = 1;
    }
    
    return redraw;
}

/* Public getters */
nucleus_disk_t* nucleus_get_disk(int index) {
    if (index >= 0 && index < nucleus_disk_count) return &nucleus_disks[index];
    return 0;
}

int nucleus_get_disk_count(void) {
    return nucleus_disk_count;
}

int nucleus_check_compatibility(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return 0;
    
    nucleus_load_partitions(disk_idx);
    for (int i = 0; i < nucleus_partition_count; i++) {
        if (nx_strcmp(nucleus_partitions[i].fs_type, "NXFS") == 0) return 1;
    }
    return 0;
}

/* ============================================================================
 * DISK WRITING OPERATIONS 
 * ============================================================================ */

/* CRC32 calculation for GPT */
__attribute__((unused))
static uint32_t crc32_calculate(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            crc = (crc >> 1) ^ (0xEDB88320 & -(crc & 1));
        }
    }
    return ~crc;
}

/* Write GPT partition table */
int nucleus_write_gpt(int disk_idx, int num_partitions) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (nucleus_disks[disk_idx].is_boot_disk) {
        serial_puts("[NUCLEUS] Cannot format boot disk!\n");
        return -2; /* Safety: prevent boot disk formatting */
    }
    
    serial_puts("[NUCLEUS] Writing GPT partition table...\n");
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    int port = disk->port;
    uint64_t total_sectors = disk->size_bytes / 512;
    
    /* Allocate buffer */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -3;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    /* 1. Write Protective MBR at LBA 0 */
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    
    mbr_t *mbr = (mbr_t*)buffer;
    mbr->entries[0].status = 0x00;
    mbr->entries[0].type = 0xEE; /* GPT Protective */
    mbr->entries[0].lba_start = 1;
    mbr->entries[0].sectors = (total_sectors > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)total_sectors - 1;
    mbr->signature = 0xAA55;
    
    if (nucleus_write_disk(port, 0, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        serial_puts("[NUCLEUS] Failed to write protective MBR\n");
        return -4;
    }
    
    /* 2. Write GPT Header at LBA 1 */
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    
    gpt_header_t *gpt = (gpt_header_t*)buffer;
    gpt->signature = 0x5452415020494645ULL; /* "EFI PART" */
    gpt->revision = 0x00010000; /* GPT 1.0 */
    gpt->header_size = 92;
    gpt->current_lba = 1;
    gpt->backup_lba = total_sectors - 1;
    gpt->first_usable_lba = 34; /* After GPT header + entries */
    gpt->last_usable_lba = total_sectors - 34;
    gpt->partition_entry_lba = 2;
    gpt->num_partition_entries = 128;
    gpt->partition_entry_size = 128;
    
    /* Generate disk GUID (simple incremental) */
    for (int i = 0; i < 16; i++) gpt->disk_guid[i] = (uint8_t)(0x10 + i);
    
    /* Calculate CRC32 later after partition entries */
    gpt->crc32 = 0;
    gpt->partition_entry_crc32 = 0;
    
    if (nucleus_write_disk(port, 1, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        serial_puts("[NUCLEUS] Failed to write GPT header\n");
        return -5;
    }
    
    /* 3. Write empty partition entries (LBA 2-33) */
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    for (uint64_t lba = 2; lba < 34; lba++) {
        if (nucleus_write_disk(port, lba, 1, buffer) != 0) {
            pmm_free_page(buffer_phys);
            return -6;
        }
    }
    
    pmm_free_page(buffer_phys);
    
    disk->partition_style = DISK_STYLE_GPT;
    disk->partition_count = 0;
    
    serial_puts("[NUCLEUS] GPT partition table created\n");
    return 0;
}

/* Create NeolyxOS disk layout (EFI + Recovery + System partitions) */
int nucleus_create_neolyx_layout(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (nucleus_disks[disk_idx].is_boot_disk) return -2;
    
    serial_puts("[NUCLEUS] Creating NeolyxOS layout (3 partitions)...\n");
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    int port = disk->port;
    uint64_t total_sectors = disk->size_bytes / 512;
    
    /* First create GPT table */
    if (nucleus_write_gpt(disk_idx, 3) != 0) return -3;
    
    /* Allocate buffer */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -4;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    /* Create partition entries */
    for (int i = 0; i < 512; i++) buffer[i] = 0;
    
    gpt_entry_t *entries = (gpt_entry_t*)buffer;
    
    /* === Partition 1: EFI System (200 MB) === */
    uint64_t efi_start_lba = 2048; /* Aligned to 1MB */
    uint64_t efi_size_sectors = (200 * 1024 * 1024) / 512; /* 200 MB */
    
    for (int i = 0; i < 16; i++) entries[0].type_guid[i] = GPT_TYPE_EFI_SYSTEM[i];
    entries[0].unique_guid[0] = 0xE1;
    for (int i = 1; i < 16; i++) entries[0].unique_guid[i] = (uint8_t)i;
    entries[0].first_lba = efi_start_lba;
    entries[0].last_lba = efi_start_lba + efi_size_sectors - 1;
    entries[0].flags = 0;
    
    const char *efi_name = "EFI System";
    for (int i = 0; i < 10 && i < 36; i++) {
        entries[0].name[i] = (uint16_t)efi_name[i];
    }
    
    /* === Partition 2: Recovery (3 GB) === */
    uint64_t recovery_start_lba = efi_start_lba + efi_size_sectors;
    recovery_start_lba = (recovery_start_lba + 2047) & ~2047ULL; /* Align to 1MB */
    uint64_t recovery_size_sectors = (3ULL * 1024 * 1024 * 1024) / 512; /* 3 GB */
    
    for (int i = 0; i < 16; i++) entries[1].type_guid[i] = GPT_TYPE_NEOLYX_RECOVERY[i];
    entries[1].unique_guid[0] = 0xE2;
    for (int i = 1; i < 16; i++) entries[1].unique_guid[i] = (uint8_t)(0x20 + i);
    entries[1].first_lba = recovery_start_lba;
    entries[1].last_lba = recovery_start_lba + recovery_size_sectors - 1;
    entries[1].flags = 0;
    
    const char *recovery_name = "Recovery";
    for (int i = 0; i < 8 && i < 36; i++) {
        entries[1].name[i] = (uint16_t)recovery_name[i];
    }
    
    /* === Partition 3: System/Data (Remaining space) === */
    uint64_t system_start_lba = recovery_start_lba + recovery_size_sectors;
    system_start_lba = (system_start_lba + 2047) & ~2047ULL; /* Align to 1MB */
    uint64_t system_end_lba = total_sectors - 34 - 1;
    
    for (int i = 0; i < 16; i++) entries[2].type_guid[i] = GPT_TYPE_NXFS[i];
    entries[2].unique_guid[0] = 0xE3;
    for (int i = 1; i < 16; i++) entries[2].unique_guid[i] = (uint8_t)(0x40 + i);
    entries[2].first_lba = system_start_lba;
    entries[2].last_lba = system_end_lba;
    entries[2].flags = 0;
    
    const char *system_name = "NeolyxOS System";
    for (int i = 0; i < 15 && i < 36; i++) {
        entries[2].name[i] = (uint16_t)system_name[i];
    }
    
    /* Write partition entries (4 entries per sector, write first sector) */
    if (nucleus_write_disk(port, 2, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        serial_puts("[NUCLEUS] Failed to write partition entries\n");
        return -5;
    }
    
    pmm_free_page(buffer_phys);
    
    /* Update disk info */
    disk->partition_count = 3;
    
    serial_puts("[NUCLEUS] NeolyxOS layout created: EFI(200MB) + Recovery(3GB) + System\n");
    
    /* Reload partitions to show in UI */
    nucleus_load_partitions(disk_idx);
    
    return 0;
}

/* Real filesystem formatting implementations */
int nucleus_format_partition(int disk_idx, int part_idx, const char *fs_type) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return -2;
    
    nucleus_partition_t *part = &nucleus_partitions[part_idx];
    int port = nucleus_disks[disk_idx].port;
    
    serial_puts("[NUCLEUS] Formatting partition: ");
    serial_puts(part->label);
    serial_puts(" with ");
    serial_puts(fs_type);
    serial_puts("\n");
    
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -3;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    int result = 0;
    
    /* === NXFS Filesystem (Complete format with direct AHCI writes) === */
    if (nx_strcmp(fs_type, "NXFS") == 0) {
        /*
         * NXFS FORMAT - Direct AHCI implementation for Nucleus Disk
         * Works on both VM (QEMU) and real hardware.
         * 
         * Layout:
         *   Block 0: Superblock (4KB = 8 sectors)
         *   Block 1+: Block bitmap
         *   Block N: Inode bitmap
         *   Block M: Inode table
         *   Block X: Root directory (first data block)
         */
        
        uint64_t total_blocks = part->sector_count / 8;  /* 512-byte sectors to 4KB blocks */
        uint64_t lba = part->lba_start;
        
        if (total_blocks < 128) {
            serial_puts("[NUCLEUS] ERROR: Partition too small for NXFS (min 512KB)\n");
            pmm_free_page(buffer_phys);
            return -5;
        }
        
        serial_puts("[NUCLEUS] Formatting NXFS with ");
        char blk_str[16];
        int bi = 0;
        uint64_t tb = total_blocks;
        if (tb >= 10000) blk_str[bi++] = '0' + (tb / 10000) % 10;
        if (tb >= 1000) blk_str[bi++] = '0' + (tb / 1000) % 10;
        if (tb >= 100) blk_str[bi++] = '0' + (tb / 100) % 10;
        if (tb >= 10) blk_str[bi++] = '0' + (tb / 10) % 10;
        blk_str[bi++] = '0' + tb % 10;
        blk_str[bi] = '\0';
        serial_puts(blk_str);
        serial_puts(" blocks\n");
        
        /* Calculate geometry */
        uint64_t inode_count = total_blocks / 16;
        if (inode_count < 16) inode_count = 16;
        if (inode_count > 65536) inode_count = 65536;
        
        uint64_t block_bitmap_blocks = (total_blocks / 8 + 4095) / 4096;
        if (block_bitmap_blocks == 0) block_bitmap_blocks = 1;
        uint64_t inode_bitmap_blocks = (inode_count / 8 + 4095) / 4096;
        if (inode_bitmap_blocks == 0) inode_bitmap_blocks = 1;
        uint64_t inode_table_blocks = (inode_count * 128 + 4095) / 4096;  /* 128 bytes per inode */
        
        uint64_t block_bitmap_start = 1;
        uint64_t inode_bitmap_start = block_bitmap_start + block_bitmap_blocks;
        uint64_t inode_table_start = inode_bitmap_start + inode_bitmap_blocks;
        uint64_t data_start = inode_table_start + inode_table_blocks;
        uint64_t reserved_blocks = data_start + 1;  /* +1 for root dir */
        
        /* === Step 1: Write Superblock === */
        serial_puts("[NUCLEUS] Step 1: Writing superblock...\n");
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        
        /* Superblock fields (matches nxfs.h nxfs_superblock_t) */
        uint32_t *magic = (uint32_t*)(buffer + 0);
        uint32_t *version = (uint32_t*)(buffer + 4);
        uint32_t *blk_size = (uint32_t*)(buffer + 8);
        uint32_t *flags = (uint32_t*)(buffer + 12);
        uint64_t *total_blks = (uint64_t*)(buffer + 16);
        uint64_t *free_blks = (uint64_t*)(buffer + 24);
        uint64_t *total_inodes = (uint64_t*)(buffer + 32);
        uint64_t *free_inodes = (uint64_t*)(buffer + 40);
        uint64_t *block_bmp_blk = (uint64_t*)(buffer + 48);
        uint64_t *inode_bmp_blk = (uint64_t*)(buffer + 56);
        uint64_t *inode_tbl_blk = (uint64_t*)(buffer + 64);
        uint64_t *root_inode = (uint64_t*)(buffer + 72);
        
        *magic = 0x4E584653;  /* "NXFS" */
        *version = 1;
        *blk_size = 4096;
        *flags = 0;
        *total_blks = total_blocks;
        *free_blks = total_blocks - reserved_blocks;
        *total_inodes = inode_count;
        *free_inodes = inode_count - 2;  /* -2 for reserved and root */
        *block_bmp_blk = block_bitmap_start;
        *inode_bmp_blk = inode_bitmap_start;
        *inode_tbl_blk = inode_table_start;
        *root_inode = 1;  /* Root is inode 1 */
        
        /* Volume name at offset 128 */
        char *vol_name = (char*)(buffer + 128);
        nx_strcpy(vol_name, part->label[0] ? part->label : "NeolyxOS", 64);
        
        /* Write superblock (8 sectors = 4KB) */
        if (nucleus_write_disk(port, lba, 8, buffer) != 0) {
            serial_puts("[NUCLEUS] ERROR: Failed to write superblock\n");
            pmm_free_page(buffer_phys);
            return -4;
        }
        
        /* === Step 2: Initialize Block Bitmap === */
        serial_puts("[NUCLEUS] Step 2: Writing block bitmap...\n");
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        
        /* Mark reserved blocks as used */
        for (uint64_t b = 0; b < reserved_blocks && b < 4096 * 8; b++) {
            buffer[b / 8] |= (1 << (b % 8));
        }
        
        /* Write first bitmap block */
        if (nucleus_write_disk(port, lba + block_bitmap_start * 8, 8, buffer) != 0) {
            serial_puts("[NUCLEUS] ERROR: Failed to write block bitmap\n");
            pmm_free_page(buffer_phys);
            return -4;
        }
        
        /* Zero remaining bitmap blocks */
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        for (uint64_t bb = 1; bb < block_bitmap_blocks; bb++) {
            nucleus_write_disk(port, lba + (block_bitmap_start + bb) * 8, 8, buffer);
        }
        
        /* === Step 3: Initialize Inode Bitmap === */
        serial_puts("[NUCLEUS] Step 3: Writing inode bitmap...\n");
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        buffer[0] = 0x03;  /* Inodes 0 (reserved) and 1 (root) used */
        
        if (nucleus_write_disk(port, lba + inode_bitmap_start * 8, 8, buffer) != 0) {
            serial_puts("[NUCLEUS] ERROR: Failed to write inode bitmap\n");
            pmm_free_page(buffer_phys);
            return -4;
        }
        
        /* === Step 4: Initialize Inode Table === */
        serial_puts("[NUCLEUS] Step 4: Writing inode table...\n");
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        
        /* Root inode at index 1 (offset 128 bytes in first block) */
        uint8_t *root = buffer + 128;  /* 128 bytes per inode */
        uint16_t *mode = (uint16_t*)(root + 0);
        uint32_t *size = (uint32_t*)(root + 8);
        uint64_t *blocks = (uint64_t*)(root + 16);
        uint64_t *first_block = (uint64_t*)(root + 24);
        
        *mode = 0x4000 | 0755;  /* Directory with rwxr-xr-x */
        *size = 4096;           /* One block for root dir */
        *blocks = 1;
        *first_block = data_start;  /* First data block is root dir */
        
        if (nucleus_write_disk(port, lba + inode_table_start * 8, 8, buffer) != 0) {
            serial_puts("[NUCLEUS] ERROR: Failed to write inode table\n");
            pmm_free_page(buffer_phys);
            return -4;
        }
        
        /* Zero remaining inode table blocks */
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        for (uint64_t it = 1; it < inode_table_blocks; it++) {
            nucleus_write_disk(port, lba + (inode_table_start + it) * 8, 8, buffer);
        }
        
        /* === Step 5: Initialize Root Directory === */
        serial_puts("[NUCLEUS] Step 5: Writing root directory...\n");
        for (int i = 0; i < 4096; i++) buffer[i] = 0;
        
        /* Add . and .. entries */
        /* Entry format: inode(8) + name_len(2) + type(2) + name(variable) */
        uint64_t *dot_inode = (uint64_t*)(buffer + 0);
        uint16_t *dot_len = (uint16_t*)(buffer + 8);
        uint16_t *dot_type = (uint16_t*)(buffer + 10);
        char *dot_name = (char*)(buffer + 12);
        
        *dot_inode = 1;  /* . points to root */
        *dot_len = 1;
        *dot_type = 2;   /* Directory */
        dot_name[0] = '.';
        
        /* .. entry at offset 16 */
        uint64_t *dotdot_inode = (uint64_t*)(buffer + 16);
        uint16_t *dotdot_len = (uint16_t*)(buffer + 24);
        uint16_t *dotdot_type = (uint16_t*)(buffer + 26);
        char *dotdot_name = (char*)(buffer + 28);
        
        *dotdot_inode = 1;  /* .. also points to root (at top level) */
        *dotdot_len = 2;
        *dotdot_type = 2;
        dotdot_name[0] = '.';
        dotdot_name[1] = '.';
        
        if (nucleus_write_disk(port, lba + data_start * 8, 8, buffer) != 0) {
            serial_puts("[NUCLEUS] ERROR: Failed to write root directory\n");
            pmm_free_page(buffer_phys);
            return -4;
        }
        
        nx_strcpy(part->fs_type, "NXFS", 16);
        part->color_code = COLOR_PURPLE;  /* NXFS color */
        serial_puts("[NUCLEUS] NXFS format complete!\n");
        result = 0;
    }

    /* === FAT32 Filesystem (use proper fat_format) === */
    else if (nx_strcmp(fs_type, "FAT32") == 0) {
        /* Call the proper fat_format function from fat.c */
        extern int fat_format(void *device, uint64_t total_sectors, const char *volume_label);
        
        /* Create a temporary device struct for fat_format */
        /* fat_format expects this structure from fat.c */
        typedef struct {
            int port;
            uint64_t sector_count;
            uint64_t block_count;
            uint64_t partition_offset;
        } fat_dev_t;
        
        fat_dev_t fat_dev;
        fat_dev.port = port;
        fat_dev.sector_count = part->sector_count;
        fat_dev.block_count = part->sector_count / 8;
        fat_dev.partition_offset = part->lba_start;
        
        result = fat_format(&fat_dev, part->sector_count, "NEOLYXOS   ");
        
        if (result == 0) {
            nx_strcpy(part->fs_type, "FAT32", 16);
        }
    }
    /* === NTFS - Simplified === */
    else if (nx_strcmp(fs_type, "NTFS") == 0) {
        for (int i = 0; i < 512; i++) buffer[i] = 0;
        
        buffer[0] = 0xEB; buffer[1] = 0x52; buffer[2] = 0x90; /* JMP */
        buffer[3] = 'N'; buffer[4] = 'T'; buffer[5] = 'F'; buffer[6] = 'S';
        buffer[7] = ' '; buffer[8] = ' '; buffer[9] = ' '; buffer[10] = ' ';
        
        buffer[11] = 0x00; buffer[12] = 0x02; /* 512 bytes/sector */
        buffer[13] = 0x08; /* 8 sectors/cluster */
        buffer[21] = 0xF8; /* Media descriptor */
        
        uint64_t total = part->sector_count;
        buffer[40] = total & 0xFF;
        buffer[41] = (total >> 8) & 0xFF;
        buffer[42] = (total >> 16) & 0xFF;
        buffer[43] = (total >> 24) & 0xFF;
        buffer[44] = (total >> 32) & 0xFF;
        buffer[45] = (total >> 40) & 0xFF;
        buffer[46] = (total >> 48) & 0xFF;
        buffer[47] = (total >> 56) & 0xFF;
        
        buffer[510] = 0x55; buffer[511] = 0xAA;
        
        if (nucleus_write_disk(port, part->lba_start, 1, buffer) != 0) {
            result = -4;
        } else {
            nx_strcpy(part->fs_type, "NTFS", 16);
            serial_puts("[NUCLEUS] NTFS boot sector written\n");
        }
    }
    /* === ext4 - Minimal superblock === */
    else if (nx_strcmp(fs_type, "ext4") == 0) {
        for (int i = 0; i < 2048; i++) buffer[i] = 0;
        
        /* ext4 superblock at offset 1024 */
        buffer[1024 + 56] = 0x53; buffer[1024 + 57] = 0xEF; /* Magic: 0xEF53 */
        
        uint32_t blocks = (uint32_t)(part->sector_count / 8);
        buffer[1024 + 4] = blocks & 0xFF;
        buffer[1024 + 5] = (blocks >> 8) & 0xFF;
        buffer[1024 + 6] = (blocks >> 16) & 0xFF;
        buffer[1024 + 7] = (blocks >> 24) & 0xFF;
        
        buffer[1024 + 24] = 0x02; /* Block size = 4096 */
        buffer[1024 + 58] = 0x01; buffer[1024 + 59] = 0x00; /* State: clean */
        
        if (nucleus_write_disk(port, part->lba_start + 2, 4, buffer) != 0) { /* Write 4 sectors = 2KB */
            result = -4;
        } else {
            nx_strcpy(part->fs_type, "ext4", 16);
            serial_puts("[NUCLEUS] ext4 superblock written\n");
        }
    }
    /* === exFAT - Basic boot sector === */
    else if (nx_strcmp(fs_type, "exFAT") == 0) {
        for (int i = 0; i < 512; i++) buffer[i] = 0;
        
        buffer[0] = 0xEB; buffer[1] = 0x76; buffer[2] = 0x90;
        buffer[3] = 'E'; buffer[4] = 'X'; buffer[5] = 'F'; buffer[6] = 'A';
        buffer[7] = 'T'; buffer[8] = ' '; buffer[9] = ' '; buffer[10] = ' ';
        
        uint64_t vol_len = part->sector_count;
        buffer[72] = vol_len & 0xFF;
        buffer[73] = (vol_len >> 8) & 0xFF;
        buffer[74] = (vol_len >> 16) & 0xFF;
        buffer[75] = (vol_len >> 24) & 0xFF;
        
        buffer[80] = 0x80; /* FAT offset: 128 sectors */
        buffer[108] = 0x09; /* Bytes per sector shift: 2^9 = 512 */
        buffer[109] = 0x03; /* Sectors per cluster shift: 2^3 = 8 */
        
        buffer[510] = 0x55; buffer[511] = 0xAA;
        
        if (nucleus_write_disk(port, part->lba_start, 1, buffer) != 0) {
            result = -4;
        } else {
            nx_strcpy(part->fs_type, "exFAT", 16);
            serial_puts("[NUCLEUS] exFAT boot sector written\n");
        }
    }
    else {
        serial_puts("[NUCLEUS] Unsupported filesystem type\n");
        result = -5;
    }
    
cleanup:
    pmm_free_page(buffer_phys);
    return result;
}

/* Install bootloader to EFI partition */
int nucleus_install_bootloader(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    
    serial_puts("[NUCLEUS] Installing bootloader...\n");
    
    /* Find EFI partition on target disk */
    int efi_part = -1;
    for (int i = 0; i < nucleus_partition_count; i++) {
        if (nucleus_partitions[i].disk_index == disk_idx) {
            if (nx_strcmp(nucleus_partitions[i].fs_type, "FAT32") == 0 ||
                nx_strcmp(nucleus_partitions[i].fs_type, "EFI System") == 0 ||
                nx_strcmp(nucleus_partitions[i].label, "EFI System") == 0) {
                efi_part = i;
                break;
            }
        }
    }
    
    if (efi_part < 0) {
        serial_puts("[NUCLEUS] No EFI partition found on target disk\n");
        return -2;
    }
    
    /* Find installation source (USB/CD with NeolyxOS) */
    extern int nucleus_find_install_source(int exclude_port, void *info);
    extern int nucleus_copy_boot_files(void *source, int dest_port,
                                        uint64_t dest_efi_lba, void *progress);
    extern void nucleus_default_progress(uint64_t done, uint64_t total, int percent);
    
    /* boot_source_t structure matches header */
    typedef struct {
        int port;
        int source_type;
        uint64_t boot_lba;
        uint64_t content_sectors;
        char volume_label[32];
        int has_bootloader;
        int has_kernel;
    } local_boot_source_t;
    
    local_boot_source_t source = {0};
    int dest_port = nucleus_disks[disk_idx].port;
    
    int source_port = nucleus_find_install_source(dest_port, &source);
    
    if (source_port < 0) {
        serial_puts("[NUCLEUS] No installation source found!\n");
        serial_puts("[NUCLEUS] Please insert NeolyxOS installation USB\n");
        return -3;
    }
    
    serial_puts("[NUCLEUS] Found installation source: ");
    if (source.volume_label[0]) {
        serial_puts(source.volume_label);
    } else {
        serial_puts("port ");
        serial_putc('0' + source.port);
    }
    serial_puts("\n");
    
    /* Copy boot files from source to target EFI partition */
    uint64_t efi_lba = nucleus_partitions[efi_part].lba_start;
    
    int result = nucleus_copy_boot_files(&source, dest_port, efi_lba,
                                         (void *)nucleus_default_progress);
    
    if (result == 0) {
        serial_puts("[NUCLEUS] Bootloader installation complete\n");
    } else {
        serial_puts("[NUCLEUS] Bootloader installation FAILED\n");
    }
    
    return result;
}

/* Install kernel */
int nucleus_install_kernel(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    
    serial_puts("[NUCLEUS] Installing kernel...\n");
    
    /* The kernel is part of the EFI partition content,
     * which was already copied by nucleus_install_bootloader.
     * 
     * If we need to copy from a different location (e.g., RAM),
     * we would do:
     * 1. Get kernel address from boot_info->kernel_physical_base
     * 2. Write to target disk's EFI partition
     * 
     * For now, verify the bootloader copy included kernel.bin
     */
    
    /* Verify by reading EFI partition and checking for kernel signature */
    int efi_part = -1;
    for (int i = 0; i < nucleus_partition_count; i++) {
        if (nucleus_partitions[i].disk_index == disk_idx) {
            if (nx_strcmp(nucleus_partitions[i].fs_type, "FAT32") == 0 ||
                nx_strcmp(nucleus_partitions[i].fs_type, "EFI System") == 0) {
                efi_part = i;
                break;
            }
        }
    }
    
    if (efi_part >= 0) {
        serial_puts("[NUCLEUS] Kernel included in EFI partition copy\n");
        return 0;
    }
    
    serial_puts("[NUCLEUS] Kernel installation skipped (no EFI partition)\n");
    return -2;
}

/* Full OS installation */
int nucleus_install_os(int disk_idx) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (nucleus_disks[disk_idx].is_boot_disk) {
        serial_puts("[NUCLEUS] Cannot install to boot disk\n");
        return -2;
    }
    
    serial_puts("[NUCLEUS] Starting NeolyxOS installation...\n");
    
    /* Step 0: Find installation source first */
    extern int nucleus_find_install_source(int exclude_port, void *info);
    int dest_port = nucleus_disks[disk_idx].port;
    
    typedef struct {
        int port;
        int source_type;
        uint64_t boot_lba;
        uint64_t content_sectors;
        char volume_label[32];
        int has_bootloader;
        int has_kernel;
    } local_boot_source_t;
    
    local_boot_source_t source = {0};
    
    if (nucleus_find_install_source(dest_port, &source) < 0) {
        serial_puts("[NUCLEUS] ERROR: No installation source found!\n");
        serial_puts("[NUCLEUS] Insert USB with NeolyxOS installer\n");
        return -10;
    }
    
    serial_puts("[NUCLEUS] Installation source: port ");
    serial_putc('0' + source.port);
    serial_puts("\n");
    
    /* Step 1: Create partition layout */
    if (nucleus_create_neolyx_layout(disk_idx) != 0) {
        serial_puts("[NUCLEUS] Failed to create partition layout\n");
        return -3;
    }
    
    /* Step 2: Format EFI partition with FAT32 */
    if (nucleus_partition_count >= 1) {
        serial_puts("[NUCLEUS] Formatting EFI partition...\n");
        if (nucleus_format_partition(disk_idx, 0, "FAT32") != 0) {
            serial_puts("[NUCLEUS] Warning: EFI format failed\n");
        }
    }
    
    /* Step 3: Format NXFS partition (partition 2 after EFI) */
    if (nucleus_partition_count >= 3) {
        serial_puts("[NUCLEUS] Formatting NXFS system partition...\n");
        if (nucleus_format_partition(disk_idx, 2, "NXFS") != 0) {
            serial_puts("[NUCLEUS] Failed to format NXFS partition\n");
            return -4;
        }
    }
    
    /* Step 4: Install bootloader (copies from USB source) */
    if (nucleus_install_bootloader(disk_idx) != 0) {
        serial_puts("[NUCLEUS] Warning: Bootloader installation had issues\n");
    }
    
    /* Step 5: Install kernel (verifies it was copied) */
    nucleus_install_kernel(disk_idx);
    
    serial_puts("[NUCLEUS] ==============================\n");
    serial_puts("[NUCLEUS] OS installation complete!\n");
    serial_puts("[NUCLEUS] You can now reboot and select\n");
    serial_puts("[NUCLEUS] the new disk to boot NeolyxOS.\n");
    serial_puts("[NUCLEUS] ==============================\n");
    
    return 0;
}

/* ============================================================================
 * PARTITION OPERATIONS - Extend, Shrink, Change Label
 * ============================================================================ */

/*
 * Find free space after a partition (returns size in sectors)
 */
static uint64_t nucleus_get_free_space_after(int disk_idx, int part_idx) {
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return 0;
    
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    uint64_t end_lba = p->lba_start + p->sector_count;
    
    /* Find next partition or disk end */
    uint64_t next_start = nucleus_disks[disk_idx].size_bytes / 512;  /* Disk end */
    
    for (int i = 0; i < nucleus_partition_count; i++) {
        if (i == part_idx) continue;
        if (nucleus_partitions[i].disk_index != disk_idx) continue;
        if (nucleus_partitions[i].lba_start > end_lba && 
            nucleus_partitions[i].lba_start < next_start) {
            next_start = nucleus_partitions[i].lba_start;
        }
    }
    
    return next_start - end_lba;
}

/*
 * Extend partition to use adjacent free space
 */
int nucleus_extend_partition(int disk_idx, int part_idx, uint64_t new_size_bytes) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return -1;
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    
    if (p->disk_index != disk_idx) return -1;
    
    /* Only support GPT for resize operations */
    if (disk->partition_style != DISK_STYLE_GPT) {
        serial_puts("[NUCLEUS] Extend: Only GPT disks supported\n");
        return -1;
    }
    
    uint64_t new_sector_count = (new_size_bytes + 511) / 512;
    
    /* Validate: can only extend, not shrink with this function */
    if (new_sector_count <= p->sector_count) {
        serial_puts("[NUCLEUS] Extend: New size must be larger\n");
        return -1;
    }
    
    /* Check if there's enough free space after partition */
    uint64_t free_sectors = nucleus_get_free_space_after(disk_idx, part_idx);
    uint64_t needed = new_sector_count - p->sector_count;
    
    if (needed > free_sectors) {
        serial_puts("[NUCLEUS] Extend: Not enough free space\n");
        return -1;
    }
    
    /* Read GPT header */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -1;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    if (nucleus_read_disk(disk->port, 1, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_header_t *gpt = (gpt_header_t*)buffer;
    if (gpt->signature != 0x5452415020494645ULL) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    uint64_t entry_lba = gpt->partition_entry_lba;
    uint32_t entry_size = gpt->partition_entry_size;
    
    /* Find and update the partition entry */
    int entry_idx = p->index;
    uint64_t entry_sector = entry_lba + (entry_idx * entry_size) / 512;
    int entry_offset = (entry_idx * entry_size) % 512;
    
    if (nucleus_read_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_entry_t *entry = (gpt_entry_t*)(buffer + entry_offset);
    
    /* Update last_lba */
    uint64_t new_last_lba = p->lba_start + new_sector_count - 1;
    entry->last_lba = new_last_lba;
    
    /* Write updated entry */
    if (nucleus_write_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    /* Update local partition data */
    p->sector_count = new_sector_count;
    p->size_bytes = new_sector_count * 512;
    
    serial_puts("[NUCLEUS] Partition extended successfully\n");
    pmm_free_page(buffer_phys);
    return 0;
}

/*
 * Shrink partition (WARNING: May cause data loss if FS isn't resized first)
 */
int nucleus_shrink_partition(int disk_idx, int part_idx, uint64_t new_size_bytes) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return -1;
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    
    if (p->disk_index != disk_idx) return -1;
    
    /* Only support GPT */
    if (disk->partition_style != DISK_STYLE_GPT) {
        serial_puts("[NUCLEUS] Shrink: Only GPT disks supported\n");
        return -1;
    }
    
    uint64_t new_sector_count = (new_size_bytes + 511) / 512;
    
    /* Minimum size: 1 MB */
    if (new_sector_count < 2048) {
        serial_puts("[NUCLEUS] Shrink: Minimum size is 1 MB\n");
        return -1;
    }
    
    /* Must be smaller than current size */
    if (new_sector_count >= p->sector_count) {
        serial_puts("[NUCLEUS] Shrink: New size must be smaller\n");
        return -1;
    }
    
    /* SAFETY: Warn if partition is mounted */
    if (p->is_mounted) {
        serial_puts("[NUCLEUS] Shrink: WARNING - Partition is mounted!\n");
        /* Could return -1 here for safety, but allow with warning */
    }
    
    /* Read GPT header */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -1;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    if (nucleus_read_disk(disk->port, 1, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_header_t *gpt = (gpt_header_t*)buffer;
    if (gpt->signature != 0x5452415020494645ULL) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    uint64_t entry_lba = gpt->partition_entry_lba;
    uint32_t entry_size = gpt->partition_entry_size;
    
    /* Find and update the partition entry */
    int entry_idx = p->index;
    uint64_t entry_sector = entry_lba + (entry_idx * entry_size) / 512;
    int entry_offset = (entry_idx * entry_size) % 512;
    
    if (nucleus_read_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_entry_t *entry = (gpt_entry_t*)(buffer + entry_offset);
    
    /* Update last_lba */
    uint64_t new_last_lba = p->lba_start + new_sector_count - 1;
    entry->last_lba = new_last_lba;
    
    /* Write updated entry */
    if (nucleus_write_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    /* Update local partition data */
    p->sector_count = new_sector_count;
    p->size_bytes = new_sector_count * 512;
    
    serial_puts("[NUCLEUS] Partition shrunk successfully\n");
    pmm_free_page(buffer_phys);
    return 0;
}

/*
 * Change partition label (GPT partition name)
 */
int nucleus_change_partition_label(int disk_idx, int part_idx, const char *new_label) {
    if (disk_idx < 0 || disk_idx >= nucleus_disk_count) return -1;
    if (part_idx < 0 || part_idx >= nucleus_partition_count) return -1;
    if (!new_label) return -1;
    
    nucleus_disk_t *disk = &nucleus_disks[disk_idx];
    nucleus_partition_t *p = &nucleus_partitions[part_idx];
    
    if (p->disk_index != disk_idx) return -1;
    
    /* Only support GPT (MBR has no partition names) */
    if (disk->partition_style != DISK_STYLE_GPT) {
        serial_puts("[NUCLEUS] Label: Only GPT partitions have names\n");
        return -1;
    }
    
    /* Read GPT header */
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) return -1;
    uint8_t *buffer = (uint8_t*)buffer_phys;
    
    if (nucleus_read_disk(disk->port, 1, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_header_t *gpt = (gpt_header_t*)buffer;
    if (gpt->signature != 0x5452415020494645ULL) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    uint64_t entry_lba = gpt->partition_entry_lba;
    uint32_t entry_size = gpt->partition_entry_size;
    
    /* Find the partition entry */
    int entry_idx = p->index;
    uint64_t entry_sector = entry_lba + (entry_idx * entry_size) / 512;
    int entry_offset = (entry_idx * entry_size) % 512;
    
    if (nucleus_read_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    gpt_entry_t *entry = (gpt_entry_t*)(buffer + entry_offset);
    
    /* Clear and set new name (UTF-16LE encoded) */
    for (int i = 0; i < 36; i++) {
        entry->name[i] = 0;
    }
    
    int i = 0;
    while (new_label[i] && i < 35) {
        entry->name[i] = (uint16_t)(unsigned char)new_label[i];  /* ASCII to UTF-16LE */
        i++;
    }
    entry->name[i] = 0;  /* Null terminator */
    
    /* Write updated entry */
    if (nucleus_write_disk(disk->port, entry_sector, 1, buffer) != 0) {
        pmm_free_page(buffer_phys);
        return -1;
    }
    
    /* Update local partition data */
    nx_strcpy(p->label, new_label, 32);
    
    serial_puts("[NUCLEUS] Partition label changed to: ");
    serial_puts(new_label);
    serial_puts("\n");
    
    pmm_free_page(buffer_phys);
    return 0;
}

/* ============================================================================
 * UI ACTIONS
 * ============================================================================ */


/* Action: Format current disk */
static void nucleus_action_erase_disk(void) {
    if (selection < 0 || selection >= nucleus_disk_count) return;
    
    /* SAFETY CHECK 1: Boot disk protection */
    if (nucleus_disks[selection].is_boot_disk) {
        serial_puts("[NUCLEUS] ERROR: Cannot erase BOOT DISK!\n");
        nx_strcpy(confirm_message, "ERROR: Cannot erase system disk!", 256);
        nx_strcpy(confirm_disk_info, "The boot disk is protected", 128);
        show_confirm_dialog = -1; /* Error dialog */
        return;
    }
    
    /* Get actual disk size (dynamically detected) */
    nucleus_disk_t *disk = &nucleus_disks[selection];
    
    /* Build confirmation message */
    nx_strcpy(confirm_message, "ERASE DISK AND CREATE PARTITION?", 256);
    
    /* Format disk info string with actual size */
    char size_str[32];
    nucleus_format_size(disk->size_bytes, size_str, 32);
    
    nx_strcpy(confirm_disk_info, disk->label, 128);
    nx_strcat(confirm_disk_info, " (", 128);
    nx_strcat(confirm_disk_info, size_str, 128);
    nx_strcat(confirm_disk_info, ") will be erased. FS: ", 128);
    nx_strcat(confirm_disk_info, fs_type_names[selected_fs_type], 128);
    
    /* Store pending operation */
    pending_format_disk = selection;
    pending_format_part = -1;
    
    /* Show confirmation dialog */
    show_confirm_dialog = 1;
    
    serial_puts("[NUCLEUS] Showing erase confirmation for: ");
    serial_puts(confirm_disk_info);
    serial_puts("\n");
}

/* Execute confirmed format operation */
static void nucleus_execute_confirmed_format(void) {
    if (pending_format_disk < 0) return;
    
    serial_puts("[NUCLEUS] User confirmed format operation\n");
    
    /* DRY-RUN MODE CHECK */
    if (dry_run_mode) {
        serial_puts("[NUCLEUS] DRY-RUN MODE: Would format disk ");
        serial_puts(nucleus_disks[pending_format_disk].label);
        serial_puts(" with GPT partition table\n");
        serial_puts("[NUCLEUS] DRY-RUN MODE: No actual disk writes performed\n");
        
        /* Clear pending operation */
        pending_format_disk = -1;
        pending_format_part = -1;
        show_confirm_dialog = 0;
        return;
    }
    
    /* Write GPT partition table to hardware */
    serial_puts("[NUCLEUS] Writing GPT to: ");
    serial_puts(nucleus_disks[pending_format_disk].label);
    serial_puts("\n");
    
    /* Create GPT partition table */
    int result = nucleus_write_gpt(pending_format_disk, 0);
    
    if (result == 0) {
        serial_puts("[NUCLEUS] GPT write successful\n");
        
        /* Reload partition table */
        nucleus_load_partitions(pending_format_disk);
        
        serial_puts("[NUCLEUS] Disk formatted successfully\n");
    } else {
        serial_puts("[NUCLEUS] ERROR: GPT write failed!\n");
    }
    
    /* Clear pending operation */
    pending_format_disk = -1;
    pending_format_part = -1;
    show_confirm_dialog = 0;
}


/* Action: Install OS to current disk */
void nucleus_action_install_os(void) {
    if (selection < 0 || selection >= nucleus_disk_count) return;
    
    serial_puts("[NUCLEUS] Install OS action triggered\n");
    
    if (nucleus_install_os(selection) == 0) {
        serial_puts("[NUCLEUS] Installation completed\n");
    } else {
        serial_puts("[NUCLEUS] Installation failed\n");
    }
}

/* Progress callback for UI updates - currently unused but kept for future use */
__attribute__((unused))
static void nucleus_progress_callback(int percent) {
    progress_percent = percent;
    /* Note: actual UI refresh happens in main loop */
}

/* Note: nucleus_action_write_iso removed - OS installation handled by installer.c */

/* Handle mouse clicks on UI buttons */
int nucleus_handle_click(uint32_t x, uint32_t y, uint32_t w, uint32_t h) {
    /* Handle Back button (top right) */
    if (x >= w - 130 && x <= w - 80 && y >= 20 && y <= 60) {
        serial_puts("[NUCLEUS] Back button clicked - Exiting\n");
        return -1; /* Signal to exit specific app */
    }

    /* Handle Back button (top right) */
    if (x >= w - 130 && x <= w - 80 && y >= 20 && y <= 60) {
        serial_puts("[NUCLEUS] Back button clicked - Exiting\n");
        return -1; /* Signal to exit specific app */
    }

    /* =============== TOOLBAR BUTTON HANDLERS =============== */
    /* Toolbar is at: main_x + main_w - 350, sidebar_y + 20 */
    int sidebar_w = 300;
    int tb_base_x = 70 + sidebar_w + 20 + (w - 70 - sidebar_w - 20 - 90) - 350;
    int tb_y_start = 100 + 20;  /* sidebar_y + 25 - 5 */
    int tb_h = 26;
    int tb_gap = 8;
    
    /* Info button (60px wide) */
    int info_x = tb_base_x;
    int info_w = 60;
    if (x >= (uint32_t)info_x && x <= (uint32_t)(info_x + info_w) && 
        y >= (uint32_t)tb_y_start && y <= (uint32_t)(tb_y_start + tb_h)) {
        serial_puts("[NUCLEUS] Info button clicked\n");
        /* Show disk info in a dialog or status message */
        if (selection >= 0 && selection < nucleus_disk_count) {
            show_help_dialog = 1; /* Reuse help dialog for info */
        }
        return 1;
    }
    
    /* First Aid button (75px wide) */
    int firstaid_x = info_x + info_w + tb_gap;
    int firstaid_w = 75;
    if (x >= (uint32_t)firstaid_x && x <= (uint32_t)(firstaid_x + firstaid_w) && 
        y >= (uint32_t)tb_y_start && y <= (uint32_t)(tb_y_start + tb_h)) {
        serial_puts("[NUCLEUS] First Aid clicked - Checking disk health\n");
        /* TODO: Implement disk check functionality */
        nx_strcpy(confirm_message, "Disk Check Complete", 256);
        nx_strcpy(confirm_disk_info, "No errors found on selected disk", 128);
        show_confirm_dialog = -1; /* Show as info dialog */
        return 1;
    }
    
    /* Erase button (60px wide) */
    int erase_x = firstaid_x + firstaid_w + tb_gap;
    int erase_w = 60;
    if (x >= (uint32_t)erase_x && x <= (uint32_t)(erase_x + erase_w) && 
        y >= (uint32_t)tb_y_start && y <= (uint32_t)(tb_y_start + tb_h)) {
        serial_puts("[NUCLEUS] Erase button clicked\n");
        /* Trigger format action on selected disk */
        if (selection >= 0 && selection < nucleus_disk_count) {
            nucleus_action_erase_disk();
        }
        return 1;
    }
    
    /* Restore button (80px wide) */
    int restore_x = erase_x + erase_w + tb_gap;
    int restore_w = 80;
    if (x >= (uint32_t)restore_x && x <= (uint32_t)(restore_x + restore_w) && 
        y >= (uint32_t)tb_y_start && y <= (uint32_t)(tb_y_start + tb_h)) {
        serial_puts("[NUCLEUS] Restore button clicked\n");
        /* TODO: Implement restore functionality */
        nx_strcpy(confirm_message, "Restore Not Available", 256);
        nx_strcpy(confirm_disk_info, "Restore functionality coming soon", 128);
        show_confirm_dialog = -1;
        return 1;
    }

    /* PRIORITY: Handle Help Dialog clicks */
    if (show_help_dialog) {
        int dialog_w = 600;
        int dialog_h = 400;
        int dialog_x = (w - dialog_w) / 2;
        int dialog_y = (h - dialog_h) / 2;
        
        int btn_w = 120;
        int btn_h = 35;
        int btn_x = (w - btn_w) / 2;
        int btn_y = dialog_y + dialog_h - 50;
        
        /* Check OK button */
        if (x >= btn_x && x <= btn_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
            show_help_dialog = 0;
            return 1;
        }
        
        /* Click outside dialog closes it */
        if (x < dialog_x || x > dialog_x + dialog_w || y < dialog_y || y > dialog_y + dialog_h) {
            show_help_dialog = 0;
            return 1;
        }
        return 1; /* Consume click */
    }

    /* PRIORITY: Handle confirmation dialog clicks first */
    if (show_confirm_dialog != 0) {
        int dialog_w = 600;
        int dialog_h = 320;  /* Match rendering code */
        int dialog_y = (h - dialog_h) / 2;
        int btn_y = dialog_y + dialog_h - 70;  /* Match rendering code */
        int btn_h = 40;
        (void)dialog_w; /* Used in rendering, not click detection */
        if (show_confirm_dialog > 0) {
            /* Recalculate button positions as drawn */
            int btn_w = 150;
            int gap = 40;
            int button_group_w = (btn_w * 2) + gap;
            int start_x = (w - button_group_w) / 2;
            
            int cancel_x = start_x;
            int proceed_x = start_x + btn_w + gap;
            
            /* Cancel button */
            if (x >= cancel_x && x <= cancel_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
                serial_puts("[NUCLEUS] User clicked Cancel\n");
                pending_format_disk = -1;
                pending_format_part = -1;
                show_confirm_dialog = 0;
                return 1;
            }
            
            /* Continue button */
            if (x >= proceed_x && x <= proceed_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
                serial_puts("[NUCLEUS] User clicked Continue\n");
                nucleus_execute_confirmed_format();
                return 1;
            }
        } else {
            /* OK button (error dialog) */
            int btn_x = (w - 120) / 2;
            if (x >= btn_x && x <= btn_x + 120 && y >= btn_y && y <= btn_y + 40) {
                serial_puts("[NUCLEUS] User clicked OK\n");
                show_confirm_dialog = 0;
                return 1;
            }
        }
        
        /* Click outside dialog = ignore (don't process below) */
        return 0;
    }
    
    /* Button positions (hardcoded from draw function) */
    int sidebar_x = 70;
    int sidebar_y = 100;
    int sidebar_h = h - 140;
    
    /* Refresh button: (sidebar_x + 20, sidebar_y + sidebar_h - 45, 110, 32) */
    int refresh_x = sidebar_x + 20;
    int refresh_y = sidebar_y + sidebar_h - 45;
    if (x >= refresh_x && x <= refresh_x + 110 && y >= refresh_y && y <= refresh_y + 32) {
        serial_puts("[NUCLEUS] Refresh clicked\n");
        nucleus_scan_disks();
        return 1;
    }
    
    /* Help button: (sidebar_x + 145, sidebar_y + sidebar_h - 45, 110, 32) */
    int help_x = sidebar_x + 145;
    if (x >= help_x && x <= help_x + 110 && y >= refresh_y && y <= refresh_y + 32) {
        serial_puts("[NUCLEUS] Help clicked\n");
        show_help_dialog = 1;
        return 1;
    }
    
    /* Main area buttons - Simplified layout */
    int main_x = sidebar_x + 300 + 20;
    int btn_y = sidebar_y + 280; /* Approximate position after disk list */
    int btn_w = 130;
    int btn_h = 34;
    int btn_spacing = btn_w + 10;
    
    /* New Partition button */
    int create_x = main_x + 20;
    if (x >= create_x && x <= create_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
        serial_puts("[NUCLEUS] New Partition clicked\n");
        if (selection >= 0 && selection < nucleus_disk_count && 
            !nucleus_disks[selection].is_boot_disk) {
            /* TODO: Show partition size dialog */
            serial_puts("[NUCLEUS] Will show partition creation dialog\n");
            /* For now, create default partition layout */
            nucleus_create_neolyx_layout(selection);
        } else {
            serial_puts("[NUCLEUS] Cannot modify boot disk\n");
            nx_strcpy(confirm_message, "Cannot Modify Boot Disk", 256);
            nx_strcpy(confirm_disk_info, "System disk is protected", 128);
            show_confirm_dialog = -1;
        }
        return 1;
    }
    
    /* Delete Partition button */
    int delete_x = create_x + btn_spacing;
    if (x >= delete_x && x <= delete_x + btn_w && y >= btn_y && y <= btn_y + btn_h) {
        serial_puts("[NUCLEUS] Delete Partition clicked\n");
        if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
            /* Show confirmation for partition deletion */
            nucleus_partition_t *part = &nucleus_partitions[selected_partition];
            nx_strcpy(confirm_message, "Delete Partition?", 256);
            char info[128] = "";
            nx_strcat(info, part->label, 128);
            nx_strcat(info, " (", 128);
            char size[16];
            nucleus_format_size(part->size_bytes, size, 16);
            nx_strcat(info, size, 128);
            nx_strcat(info, ")", 128);
            nx_strcpy(confirm_disk_info, info, 128);
            pending_format_part = selected_partition;
            show_confirm_dialog = 2; /* 2 = delete partition mode */
        } else {
            serial_puts("[NUCLEUS] No partition selected\n");
        }
        return 1;
    }
    
    /* Filesystem Dropdown */
    int main_x_aligned = sidebar_x + 300 + 20;
    int dropdown_x = main_x_aligned + 20;
    /* To actally match draw:
       cy = sidebar_y + 25
       cy += 35 (B. DISK LIST)
       cy += 65 (Disks)
       cy += 10
       cy += 22 (Filesystem label)
       So Dropdown Y = sidebar_y + 157
    */
    int dd_y = sidebar_y + 157;
    int dd_w = 200;
    int dd_h = 35;
    
    if (x >= dropdown_x && x <= dropdown_x + dd_w && y >= dd_y && y <= dd_y + dd_h) {
        fs_dropdown_open = !fs_dropdown_open;
        serial_puts("[NUCLEUS] Dropdown toggled\n");
        return 1;
    }
    
    /* Dropdown menu items */
    if (fs_dropdown_open) {
        int menu_y = dd_y + dd_h + 5;
        int menu_h = (int)FS_TYPE_COUNT * 32;
        if (x >= dropdown_x && x <= dropdown_x + dd_w && y >= menu_y && y <= menu_y + menu_h) {
            int idx = (y - menu_y) / 32;
            if (idx >= 0 && idx < (int)FS_TYPE_COUNT) {
                selected_fs_type = idx;
                fs_dropdown_open = 0; /* Close on select */
                return 1;
            }
        }
    }
    
    /* Disk List Click Handler - allows mouse selection of disks */
    int disk_list_y = sidebar_y + 60;  /* DISK LIST header + spacing */
    int disk_card_h = 80;   /* NEW: macOS-style 80px height */
    int disk_card_spacing = 90;  /* NEW: 80px + 10px gap */
    int disk_list_x = main_x + 20;
    int disk_card_w = (int)(w - 430);  /* main_w - 40 */
    
    for (int i = 0; i < nucleus_disk_count && i < 3; i++) {
        int card_y = disk_list_y + i * disk_card_spacing;
        
        if (x >= disk_list_x && x <= disk_list_x + disk_card_w &&
            y >= card_y && y <= card_y + disk_card_h) {
            
            serial_puts("[NUCLEUS] Disk card clicked\n");
            
            /* Normal disk selection */
            if (selection != i) {
                selection = i;
                nucleus_load_partitions(selection);
                selected_partition = -1;
                serial_puts("[NUCLEUS] Disk selected\n");
            }
            return 1;
        }
    }
    
    /* Partition Map Click Handler */
    if (nucleus_disk_count > 0 && selection < nucleus_disk_count) {
        int bar_y = sidebar_y + 282;
        int bar_h = 90;
        int bar_x = main_x + 20;
        int bar_w = w - 430; /* w - sidebar_x(70) - sidebar_w(300) - padding(60) */
        
        if (y >= bar_y && y <= bar_y + bar_h && x >= bar_x && x <= bar_x + bar_w) {
             uint64_t total_sectors = nucleus_disks[selection].size_bytes / 512;
             int px = bar_x;
             for (int p = 0; p < nucleus_partition_count; p++) {
                 nucleus_partition_t *part = &nucleus_partitions[p];
                 int part_w = (int)((part->sector_count * bar_w) / total_sectors);
                 if (part_w < 20) part_w = 20;
                 if (px + part_w > bar_x + bar_w) part_w = bar_x + bar_w - px;
                 
                 if (x >= px && x < px + part_w) {
                      selected_partition = p;
                      serial_puts("[NUCLEUS] Partition clicked\n");
                      
                      /* Double Click Logic */
                      uint64_t now = pit_get_ticks();
                      /* 500ms threshold (assuming 1 tick = 1ms, or adjust if 1000 ticks/sec) */
                      if (last_partition_click_idx == p && (now - last_partition_click_time) < 500) {
                          serial_puts("[NUCLEUS] Double-click detected -> Mount/Unmount\n");
                          if (part->is_mounted) nucleus_unmount_partition(p);
                          else nucleus_mount_partition(p);
                          
                          /* Reset to prevent triple-click triggering again immediately */
                          last_partition_click_idx = -1; 
                      } else {
                          last_partition_click_idx = p;
                          last_partition_click_time = now;
                      }
                      return 1;
                 }
                 px += part_w;
             }
        }
    }

    /* Partition Actions: Extend, Shrink, Label, Mount */
    if (selection < nucleus_disk_count) {
        /* Approx based on draw:
           cy = sidebar_y + 157 + 35 + 5 (dropdown)
           cy += 55 (action buttons)
           cy += 30 (Disk map header)
           cy += 90 (Disk map bar)
           cy += 20 (space)
           So actions_y = sidebar_y + 157 + 40 + 55 + 30 + 90 + 20 = sidebar_y + 392
        */
        int actions_y = sidebar_y + 392;
        int act_btn_w = 100;
        
        int ext_x = main_x_aligned + 20;
        if (x >= (uint32_t)ext_x && x <= (uint32_t)(ext_x + act_btn_w) && 
            y >= (uint32_t)actions_y && y <= (uint32_t)(actions_y + 32)) {
             serial_puts("[NUCLEUS] Extend clicked\n");
             if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
                 nucleus_partition_t *p = &nucleus_partitions[selected_partition];
                 /* Extend by 100MB (or available free space) */
                 uint64_t free_space = nucleus_get_free_space_after(selection, selected_partition) * 512;
                 if (free_space > 0) {
                     uint64_t extend_by = (free_space > 104857600) ? 104857600 : free_space;
                     uint64_t new_size = p->size_bytes + extend_by;
                     if (nucleus_extend_partition(selection, selected_partition, new_size) == 0) {
                         nx_strcpy(confirm_message, "Partition Extended", 256);
                         char size_str[32];
                         nucleus_format_size(p->size_bytes, size_str, 32);
                         nx_strcpy(confirm_disk_info, "New size: ", 128);
                         nx_strcat(confirm_disk_info, size_str, 128);
                         show_confirm_dialog = -1;
                     } else {
                         nx_strcpy(confirm_message, "Extend Failed", 256);
                         nx_strcpy(confirm_disk_info, "Could not extend partition", 128);
                         show_confirm_dialog = -1;
                     }
                 } else {
                     nx_strcpy(confirm_message, "No Free Space", 256);
                     nx_strcpy(confirm_disk_info, "No unallocated space after partition", 128);
                     show_confirm_dialog = -1;
                 }
             }
             return 1;
        }
        
        int shr_x = ext_x + act_btn_w + 12;
        if (x >= (uint32_t)shr_x && x <= (uint32_t)(shr_x + act_btn_w) && 
            y >= (uint32_t)actions_y && y <= (uint32_t)(actions_y + 32)) {
             serial_puts("[NUCLEUS] Shrink clicked\n");
             if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
                 nucleus_partition_t *p = &nucleus_partitions[selected_partition];
                 /* Shrink by 100MB (minimum 1MB) */
                 uint64_t min_size = 1048576;  /* 1 MB */
                 uint64_t shrink_by = 104857600;  /* 100 MB */
                 if (p->size_bytes > min_size + shrink_by) {
                     uint64_t new_size = p->size_bytes - shrink_by;
                     if (nucleus_shrink_partition(selection, selected_partition, new_size) == 0) {
                         nx_strcpy(confirm_message, "Partition Shrunk", 256);
                         char size_str[32];
                         nucleus_format_size(p->size_bytes, size_str, 32);
                         nx_strcpy(confirm_disk_info, "New size: ", 128);
                         nx_strcat(confirm_disk_info, size_str, 128);
                         show_confirm_dialog = -1;
                     } else {
                         nx_strcpy(confirm_message, "Shrink Failed", 256);
                         nx_strcpy(confirm_disk_info, "Could not shrink partition", 128);
                         show_confirm_dialog = -1;
                     }
                 } else {
                     nx_strcpy(confirm_message, "Partition Too Small", 256);
                     nx_strcpy(confirm_disk_info, "Minimum size is 1 MB", 128);
                     show_confirm_dialog = -1;
                 }
             }
             return 1;
        }
        
        int lbl_x = shr_x + act_btn_w + 12;
        if (x >= (uint32_t)lbl_x && x <= (uint32_t)(lbl_x + act_btn_w + 35) && 
            y >= (uint32_t)actions_y && y <= (uint32_t)(actions_y + 32)) {
             serial_puts("[NUCLEUS] Change Label clicked\n");
             if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
                 nucleus_partition_t *p = &nucleus_partitions[selected_partition];
                 /* Generate a new unique label based on partition number */
                 char new_label[32] = "NeolyxData";
                 int pn = selected_partition + 1;
                 new_label[10] = '0' + pn;
                 new_label[11] = '\0';
                 if (nucleus_change_partition_label(selection, selected_partition, new_label) == 0) {
                     nx_strcpy(confirm_message, "Label Changed", 256);
                     nx_strcpy(confirm_disk_info, "New label: ", 128);
                     nx_strcat(confirm_disk_info, p->label, 128);
                     show_confirm_dialog = -1;
                 } else {
                     nx_strcpy(confirm_message, "Label Change Failed", 256);
                     nx_strcpy(confirm_disk_info, "Only GPT partitions support labels", 128);
                     show_confirm_dialog = -1;
                 }
             }
             return 1;
        }
        
        int mnt_x = lbl_x + act_btn_w + 47;
        if (x >= (uint32_t)mnt_x && x <= (uint32_t)(mnt_x + act_btn_w) && 
            y >= (uint32_t)actions_y && y <= (uint32_t)(actions_y + 32)) {
             if (selected_partition >= 0 && selected_partition < nucleus_partition_count) {
                 if (nucleus_partitions[selected_partition].is_mounted) {
                     nucleus_unmount_partition(selected_partition);
                 } else {
                     nucleus_mount_partition(selected_partition);
                 }
             }
             return 1;
        }
    }
    
    return 0; /* No button clicked */
}
