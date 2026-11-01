/*
 * NeolyxOS - Desktop preview harness (host only)
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NX_PREVIEW_H
#define NX_PREVIEW_H

#include <stdint.h>
#include "../../include/nxsyscall.h"

#define PREVIEW_W 1280
#define PREVIEW_H 800

void preview_machine_init(void);
uint32_t preview_frame_count(void);
/* Presents so far (fb.flip and fb.flip_rects); idle ticks present nothing. */
uint32_t preview_flip_count(void);
const uint32_t *preview_framebuffer(void);

/* Called after every frame tick (drawn or skipped) with its 1-based number. */
void preview_on_flip(uint32_t frame);
/* Deliver at most max scripted events due at this frame. */
int preview_input_poll(input_event_t *ev, int max, uint32_t frame, uint64_t now_ms);
int preview_verbose(void);

#endif /* NX_PREVIEW_H */
