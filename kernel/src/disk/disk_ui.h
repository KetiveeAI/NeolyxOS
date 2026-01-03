/*
 * NUCLEUS - NeolyxOS Disk Utility
 * 
 * System disk management and NXFS formatting utility
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NUCLEUS_DISK_UI_H
#define NUCLEUS_DISK_UI_H

#include <stdint.h>

/* Disk info structure */
typedef struct {
    uint8_t  id;
    char     name[32];
    uint64_t size_mb;
    uint8_t  type;       /* 0=HDD, 1=SSD, 2=USB, 3=NVMe */
    uint8_t  partitions;
    uint8_t  selected;
} disk_info_t;

/* Disk types */
#define DISK_TYPE_HDD   0
#define DISK_TYPE_SSD   1
#define DISK_TYPE_USB   2
#define DISK_TYPE_NVME  3

/* UI Functions */
void disk_ui_init(void);
void disk_ui_draw_table(disk_info_t *disks, int count, int selected);
void disk_ui_draw_header(void);
void disk_ui_draw_footer(void);
int  disk_ui_handle_input(void);
void disk_ui_show_format_dialog(disk_info_t *disk);
void disk_ui_show_progress(const char *message, int percent);

/* NUCLEUS launcher - call from boot menu */
void nucleus_launch(void);

#endif /* NUCLEUS_DISK_UI_H */
