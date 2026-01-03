/*
 * NeolyxOS Recovery Menu
 * 
 * Recovery options menu triggered by F2 key during boot
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYXOS_RECOVERY_H
#define NEOLYXOS_RECOVERY_H

#include <stdint.h>

/* Recovery menu options */
#define RECOVERY_OPTION_CONTINUE     0  /* Continue normal boot */
#define RECOVERY_OPTION_RECOVER      1  /* Recover NeolyxOS */
#define RECOVERY_OPTION_DISK_UTILITY 2  /* Disk Utility */
#define RECOVERY_OPTION_HELP         3  /* Help / Documentation */
#define RECOVERY_OPTION_FRESH_INSTALL 4 /* Fresh Install (reinstall) */
#define RECOVERY_OPTION_COUNT        5

/* Recovery menu state */
typedef struct {
    int selected;
    int triggered;          /* Was F2 pressed? */
    int f2_press_count;     /* Number of F2 presses detected */
    uint64_t fb_addr;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_pitch;
} RecoveryMenu;

/* Initialize recovery menu */
void recovery_init(RecoveryMenu *menu, uint64_t fb_addr, 
                   uint32_t width, uint32_t height, uint32_t pitch);

/* Check for F2 key during boot (call repeatedly) */
int recovery_check_f2(RecoveryMenu *menu);

/* Draw recovery menu */
void recovery_draw(RecoveryMenu *menu);

/* Handle input and return selected option */
int recovery_run(RecoveryMenu *menu);

/* Should we show recovery menu? (F2 pressed during splash) */
int recovery_should_show(RecoveryMenu *menu);

#endif /* NEOLYXOS_RECOVERY_H */
