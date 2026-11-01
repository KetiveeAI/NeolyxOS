/*
 * NeolyxOS Boot Log File
 *
 * Saves the kernel log (the in-memory boot log ring, kernel/src/video/
 * bootlog.c) to \EFI\NEOLYX\boot.log on the boot volume, so a machine
 * without a serial port leaves a readable log behind.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_BOOT_LOGFILE_H
#define NEOLYX_BOOT_LOGFILE_H

#include <stdint.h>

/* Path of the log file on the volume mounted at / (the ESP) */
#define BOOT_LOGFILE_PATH   "/EFI/NEOLYX/boot.log"

/* The file is created by the image build (rebuild_image.sh) with exactly
 * this size and is only ever overwritten in place. */
#define BOOT_LOGFILE_SIZE   65536u

/**
 * boot_logfile_save - Overwrite the boot log file with the current log.
 * @reason: short text for the first line, e.g. "desktop started"
 *
 * Safety: the kernel never creates, grows, shrinks or renames the file.
 * It only writes when the file already exists with exactly
 * BOOT_LOGFILE_SIZE bytes, so the file system's allocation never changes;
 * the data clusters are rewritten and the directory entry is rewritten
 * with the same values. Must be called from a context that may do disk
 * I/O (not from an interrupt or fault handler).
 *
 * Returns 0 on success, -1 if the file is missing or has another size
 * (nothing is written), -2 on out of memory, -3 on a write error.
 * Every outcome is logged.
 */
int boot_logfile_save(const char *reason);

#endif /* NEOLYX_BOOT_LOGFILE_H */
