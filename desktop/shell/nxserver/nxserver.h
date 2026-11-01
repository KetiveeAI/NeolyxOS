/*
 * nxserver - the window server inside the desktop process (week 3, steps 2 and 3)
 *
 * Spec: docs/desktop/proposals/nxserver-window-protocol.md (approved by the
 * project owner on 2026-11-01). Listens on port "nx.display", speaks window
 * protocol 1.0 (sdk/nxwindow/nxwin_proto.h) and shows each client window as
 * an ordinary shell window whose content is the client's shared-memory
 * buffer, mapped read-only and drawn without a copy.
 *
 * The shell calls these from its frame loop and from its window actions.
 * Nothing here blocks except nxserver_wait, which the frame loop uses to
 * sleep until the next frame tick or the next client message.
 *
 * Commits damage the screen through desktop_damage_window_content
 * (desktop_shell.h), so a frame redraws only what clients changed.
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

/* Once per frame tick, after the frame reached the screen (or the tick drew
 * nothing): FRAME_DONE for every commit since the last tick, then flush
 * queued events. A commit always damages the screen, so a visible window's
 * commit is drawn in the tick that answers it. */
void nxserver_frame_presented(void);

/*
 * Sleep until the port or a client channel has something to read, or
 * timeout_ms passes. Messages are not handled here (nxserver_poll does).
 * @return > 0 woken by a message, 0 timeout, -1 not listening or obj.wait
 *         failed (the caller sleeps instead; logged once)
 */
int nxserver_wait(uint32_t timeout_ms);

/* 1 if shell window shell_id shows a client's buffer. Such a window's
 * content changes only by COMMIT, so the shell need not redraw it each
 * frame the way it does for windows drawn by the shell's own views. */
int nxserver_owns_window(uint32_t shell_id);

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
