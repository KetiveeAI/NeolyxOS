/*
 * NeolyxOS PTY line discipline (kernel-internal)
 *
 * The terminal rules between a PTY master (Terminal.app) and its slave (the
 * shell and its jobs), as specified in section 8 of
 * docs/kernel/proposals/beta-process-abi.md:
 *
 *   master write -> nx_ldisc_input()   keyboard input: CR/LF mapping, line
 *                                       editing, echo, Ctrl-C detection
 *   slave read   -> nx_ldisc_read()    whole lines (canonical) or raw bytes
 *   slave write  -> nx_ldisc_output()  program output, "\n" -> "\r\n"
 *   master read  -> nx_ldisc_read_output()
 *
 * This file only moves bytes between two rings over caller storage. It does
 * not block, allocate, end processes or touch user memory: the PTY syscalls
 * (core/nx_pty.c, after owner sign-off) do that around it. Kernel and host
 * unit tests (tests/host/nx_ldisc_test.c) build the same code.
 *
 * Not part of the user ABI. The mode bit values equal the proposed
 * NX_PTY_* values so the syscall layer can pass them through unchanged.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_LDISC_H
#define NEOLYX_NX_LDISC_H

#include <stdint.h>
#include "core/nx_ring.h"

#define NX_LDISC_LINE_MAX   1024    /* canonical line, including its newline */
#define NX_LDISC_RECS       64      /* complete lines/EOF marks waiting for the slave */

/* Mode bits */
#define NX_LDISC_ECHO       0x01u   /* echo input to the master */
#define NX_LDISC_CANON      0x02u   /* line editing; reads return whole lines */
#define NX_LDISC_ISIG       0x04u   /* Ctrl-C reports NX_LDISC_EV_INTR */
#define NX_LDISC_ONLCR      0x08u   /* output and echo: "\n" -> "\r\n" */
#define NX_LDISC_ICRNL      0x10u   /* input: "\r" -> "\n" */
#define NX_LDISC_MODE_ALL   0x1Fu
#define NX_LDISC_MODE_DEFAULT NX_LDISC_MODE_ALL

/* Events reported by nx_ldisc_input() */
#define NX_LDISC_EV_INTR    0x1u    /* Ctrl-C seen with ISIG: end the foreground job */

/* nx_ldisc_read() result when nothing is ready (caller blocks or -EAGAIN) */
#define NX_LDISC_EMPTY      (-1)

/* Control characters */
#define NX_LDISC_CH_INTR    0x03    /* Ctrl-C */
#define NX_LDISC_CH_EOF     0x04    /* Ctrl-D */
#define NX_LDISC_CH_BS      0x08    /* Backspace */
#define NX_LDISC_CH_KILL    0x15    /* Ctrl-U */
#define NX_LDISC_CH_DEL     0x7F    /* Delete (Backspace on most keyboards) */

typedef struct {
    uint32_t  mode;                         /* NX_LDISC_* mode bits */
    nx_ring_t in;                           /* bytes the slave can read */
    nx_ring_t out;                          /* bytes the master can read */
    /* Canonical mode: lengths of the complete records queued in `in`, oldest
     * first. A length of 0 is an end-of-file mark (Ctrl-D on an empty line).
     * Invariant: the lengths add up to in.count. Empty in raw mode. */
    uint16_t  rec[NX_LDISC_RECS];
    uint32_t  rec_head;
    uint32_t  rec_count;
    uint8_t   line[NX_LDISC_LINE_MAX];      /* line being edited (canonical) */
    uint32_t  line_len;
    uint32_t  echo_dropped;                 /* echo bytes lost: master output full */
    uint32_t  input_dropped;                /* characters refused: line full */
} nx_ldisc_t;

/**
 * Initialise a line discipline over two caller-provided buffers.
 * @param in_buf   storage for slave input, in_cap bytes (at most 65535)
 * @param out_buf  storage for master output, out_cap bytes
 * @param mode     NX_LDISC_* bits; unknown bits are ignored
 * @return 0, or -1 for a NULL argument or a capacity of 0 or above 65535
 */
int nx_ldisc_init(nx_ldisc_t *ld, uint8_t *in_buf, uint32_t in_cap,
                  uint8_t *out_buf, uint32_t out_cap, uint32_t mode);

/**
 * Change the mode. Leaving canonical mode makes the queued lines and the
 * line being edited plain bytes (end-of-file marks are dropped); entering it
 * makes the bytes already queued one complete record.
 * @return 0, or -1 for unknown bits (mode unchanged)
 */
int nx_ldisc_set_mode(nx_ldisc_t *ld, uint32_t mode);

/**
 * Feed keyboard input (master write).
 * Consumes bytes in order and stops at the first byte that does not fit
 * (slave input full, or too many queued lines); the caller retries the rest
 * after the slave reads. Characters typed past the line limit are consumed
 * and dropped (counted in input_dropped). Echo never blocks input: echo that
 * does not fit in the master output is dropped (counted in echo_dropped).
 *
 * @param events  out: NX_LDISC_EV_* seen while consuming (may be NULL)
 * @return bytes consumed
 */
uint32_t nx_ldisc_input(nx_ldisc_t *ld, const uint8_t *src, uint32_t n,
                        uint32_t *events);

/**
 * Slave read. In canonical mode returns at most one record: the rest of the
 * oldest line (a short buffer gets the line in pieces), or 0 for an
 * end-of-file mark. In raw mode returns what is queued.
 * @param n  capacity of dst, must be > 0
 * @return bytes copied, 0 for end of file, NX_LDISC_EMPTY when nothing is
 *         ready, or NX_LDISC_EMPTY for a bad argument
 */
int32_t nx_ldisc_read(nx_ldisc_t *ld, uint8_t *dst, uint32_t n);

/** 1 when nx_ldisc_read() would return data or end of file. */
int nx_ldisc_readable(const nx_ldisc_t *ld);

/**
 * Program output (slave write). Applies ONLCR. Stops at the first byte that
 * does not fit; with ONLCR a "\n" is taken only when both bytes fit.
 * @return bytes consumed
 */
uint32_t nx_ldisc_output(nx_ldisc_t *ld, const uint8_t *src, uint32_t n);

/** Master read: remove up to n bytes of output and echo. @return bytes */
uint32_t nx_ldisc_read_output(nx_ldisc_t *ld, uint8_t *dst, uint32_t n);

/** Bytes queued for the master. */
uint32_t nx_ldisc_output_used(const nx_ldisc_t *ld);

/** Bytes of output room left. */
uint32_t nx_ldisc_output_room(const nx_ldisc_t *ld);

#endif /* NEOLYX_NX_LDISC_H */
