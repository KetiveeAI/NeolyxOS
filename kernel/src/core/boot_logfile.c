/*
 * NeolyxOS Boot Log File
 *
 * See include/core/boot_logfile.h for the rules. The file holds a header
 * line, then the newest part of the kernel log that fits, padded with
 * spaces and a final newline to exactly BOOT_LOGFILE_SIZE bytes.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "core/boot_logfile.h"
#include "fs/vfs.h"
#include "mm/kheap.h"
#include "video/bootlog.h"

extern void serial_puts(const char *s);

static uint32_t lf_append(char *buf, uint32_t at, uint32_t cap, const char *s) {
    while (s && *s && at < cap) buf[at++] = *s++;
    return at;
}

int boot_logfile_save(const char *reason) {
    vfs_node_t st;
    if (vfs_stat(BOOT_LOGFILE_PATH, &st) != VFS_OK) {
        serial_puts("[BOOTLOG] " BOOT_LOGFILE_PATH " not found on the boot volume; log not saved\n");
        return -1;
    }
    if (st.type != VFS_FILE || st.size != BOOT_LOGFILE_SIZE) {
        serial_puts("[BOOTLOG] " BOOT_LOGFILE_PATH " is not a preallocated 64 KiB file; log not saved\n");
        return -1;
    }

    char *buf = (char *)kmalloc(BOOT_LOGFILE_SIZE);
    if (!buf) {
        serial_puts("[BOOTLOG] ERROR: no memory for the log file buffer\n");
        return -2;
    }

    /* Header line, then the log, then padding */
    uint32_t at = 0;
    at = lf_append(buf, at, BOOT_LOGFILE_SIZE, "NeolyxOS kernel log (");
    at = lf_append(buf, at, BOOT_LOGFILE_SIZE, reason ? reason : "saved");
    at = lf_append(buf, at, BOOT_LOGFILE_SIZE, ")\n");
    uint32_t room = BOOT_LOGFILE_SIZE - at - 1;     /* keep the final newline */
    at += bootlog_copy(buf + at, room);
    while (at < BOOT_LOGFILE_SIZE - 1) buf[at++] = ' ';
    buf[BOOT_LOGFILE_SIZE - 1] = '\n';

    /* Write in place: no create, no truncate, same size */
    int rc = 0;
    vfs_file_t *f = vfs_open(BOOT_LOGFILE_PATH, VFS_O_WRONLY);
    if (!f) {
        serial_puts("[BOOTLOG] ERROR: cannot open " BOOT_LOGFILE_PATH " for writing\n");
        rc = -3;
    } else {
        int64_t w = vfs_write(f, buf, BOOT_LOGFILE_SIZE);
        if (w != (int64_t)BOOT_LOGFILE_SIZE) {
            serial_puts("[BOOTLOG] ERROR: writing " BOOT_LOGFILE_PATH " failed\n");
            rc = -3;
        }
        if (vfs_close(f) != VFS_OK && rc == 0) {
            serial_puts("[BOOTLOG] ERROR: closing " BOOT_LOGFILE_PATH " failed\n");
            rc = -3;
        }
    }
    kfree(buf);

    if (rc == 0) {
        serial_puts("[BOOTLOG] Kernel log saved to " BOOT_LOGFILE_PATH " (");
        serial_puts(reason ? reason : "saved");
        serial_puts(")\n");
    }
    return rc;
}
