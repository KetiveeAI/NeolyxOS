/*
 * NeolyxOS RAM Filesystem Header
 * 
 * Simple in-memory filesystem for initial userspace binaries.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_RAMFS_H
#define NEOLYX_RAMFS_H

#include "fs/vfs.h"

/**
 * Initialize the ramfs driver.
 * Call before mounting.
 */
void ramfs_init(void);

/**
 * Mount ramfs at root ("/").
 * This provides /bin/hello and other embedded binaries.
 * 
 * @return 0 on success, negative on error
 */
int ramfs_mount_root(void);

#endif /* NEOLYX_RAMFS_H */
