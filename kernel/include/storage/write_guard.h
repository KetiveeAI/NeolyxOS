/*
 * NeolyxOS Storage Write Guard
 *
 * A boot-wide switch that makes every disk driver refuse writes. The UEFI
 * loader turns it on (boot flag NEOLYX_BOOT_FLAG_WRITE_PROTECT, see
 * include/video/bootlog.h) for hardware smoke tests, so booting NeolyxOS on
 * a machine with other operating systems installed cannot change its disks.
 *
 * Once enabled it stays on until the machine restarts: there is no call to
 * turn it off.
 *
 * Every hardware write path checks it before issuing a command:
 * ahci_write (via ahci_issue_command), nvme_write and ata_write_sectors.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_WRITE_GUARD_H
#define NEOLYX_WRITE_GUARD_H

#include <stdint.h>

/** storage_write_guard_enable - Refuse all disk writes from now on. Logged. */
void storage_write_guard_enable(void);

/** storage_write_guard_active - Nonzero once writes are refused. */
int storage_write_guard_active(void);

/**
 * storage_write_allowed - Ask before issuing a write to a disk.
 * @driver: short driver name for the log, e.g. "AHCI"
 * @lba:    first sector of the write
 * @count:  sectors
 *
 * Returns 1 if the write may proceed, 0 if it must be refused. Refusals are
 * logged (the first few in full, then a running count).
 */
int storage_write_allowed(const char *driver, uint64_t lba, uint32_t count);

#endif /* NEOLYX_WRITE_GUARD_H */
