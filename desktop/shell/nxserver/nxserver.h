/*
 * nxserver - the window server inside the desktop process (week 3, step 2)
 *
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md (approved by the
 * project owner on 2026-11-01). Listens on port "nx.display", speaks window
 * protocol 1.0 (sdk/nxwindow/nxwin_proto.h) and shows each client window as
 * an ordinary shell window whose content is the client's shared-memory
 * buffer, mapped read-only and drawn without a copy.
 *
 * The shell calls these from its frame loop and from its window actions.
 * Nothing here blocks: every syscall it makes returns at once.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NXSERVER_H
#define NXSERVER_H

#include <stdint.h>

/*
 * Listen on "nx.display". Call once, after the shell's windows and NXUI are
 * initialized. On failure the reason is logged, the desktop keeps running
 * without client windows, and every other nxserver call does nothing.
 * @return 0, or a negative errno
 */
int nxserver_init(uint32_t screen_w, uint32_t screen_h);

/*
 * Once per frame, after input and before rendering: accept connections,
 * handle every client's requests, send CONFIGURE for windows the user
 * resized, tiled or (un)focused, and route pointer motion, enter and leave.
 */
void nxserver_poll(void);

/* Once per frame, after the frame reached the screen: FRAME_DONE for every
 * commit drawn in it, then flush queued events. */
void nxserver_frame_presented(void);

/*
 * The user asked to close shell window shell_id (close button, NX+W, dock).
 * @return 1 if it is a live client window (CLOSE_REQUEST was sent and the
 *         shell must not close it), 0 if the shell should close it itself
 */
int nxserver_close_requested(uint32_t shell_id);

/* The shell destroyed window shell_id: release its buffers if it was a
 * client window. Safe to call for any id. */
void nxserver_window_destroyed(uint32_t shell_id);

#endif /* NXSERVER_H */
