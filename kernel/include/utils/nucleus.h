#ifndef UTILS_NUCLEUS_H
#define UTILS_NUCLEUS_H

#include <stdint.h>

typedef enum {
    DISK_CONN_SATA,
    DISK_CONN_NVME,
    DISK_CONN_USB,
    DISK_CONN_VIRTIO
} nucleus_conn_type_t;

typedef enum {
    DISK_STYLE_UNKNOWN,
    DISK_STYLE_MBR,
    DISK_STYLE_GPT,
    DISK_STYLE_RAW
} nucleus_part_style_t;

typedef enum {
    FS_TYPE_NXFS,
    FS_TYPE_FAT32,
    FS_TYPE_NTFS,
    FS_TYPE_EXT4,
    FS_TYPE_EXFAT,
    FS_TYPE_COUNT
} nucleus_fs_type_t;

typedef struct {
    int index;
    char name[8];    /* e.g. "sda" */
    char model[48];  /* e.g. "Samsung SSD..." */
    char label[32];  /* Disk Label */
    uint64_t size_bytes;
    int partition_count;
    int is_boot_disk;
    int port;        /* AHCI Port Number */
    nucleus_conn_type_t connection_type;
    nucleus_part_style_t partition_style;
} nucleus_disk_t;

void nucleus_init(void);

void nucleus_scan_disks(void);

void nucleus_draw(volatile uint32_t *fb, uint32_t w, uint32_t h, uint32_t pitch);

int nucleus_handle_input(uint8_t scancode);

/* Accessors for other views */
int nucleus_get_disk_count(void);
nucleus_disk_t* nucleus_get_disk(int index);
int nucleus_check_compatibility(int disk_idx);

/* Disk Writing Operations */
int nucleus_read_disk(int port, uint64_t lba, uint32_t count, void *buffer);
int nucleus_write_disk(int port, uint64_t lba, uint32_t count, const void *buffer);
int nucleus_write_gpt(int disk_idx, int num_partitions);
int nucleus_write_mbr(int disk_idx, int num_partitions);
int nucleus_format_partition(int disk_idx, int part_idx, const char *fs_type);
int nucleus_create_neolyx_layout(int disk_idx);

/* OS Installation */
int nucleus_install_bootloader(int disk_idx);
int nucleus_install_kernel(int disk_idx);
int nucleus_install_os(int disk_idx);

/* UI Actions */
int nucleus_handle_click(uint32_t x, uint32_t y, uint32_t w, uint32_t h);
void nucleus_action_format_disk(void);
void nucleus_action_install_os(void);

/* Partition Operations */
int nucleus_extend_partition(int disk_idx, int part_idx, uint64_t new_size_bytes);
int nucleus_shrink_partition(int disk_idx, int part_idx, uint64_t new_size_bytes);
int nucleus_change_partition_label(int disk_idx, int part_idx, const char *new_label);

#endif
