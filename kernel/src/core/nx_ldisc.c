/*
 * NeolyxOS PTY line discipline (kernel-internal). See core/nx_ldisc.h.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_ldisc.h"

#define REC_LEN_MAX 65535u

static int is_canon(const nx_ldisc_t *ld) {
    return (ld->mode & NX_LDISC_CANON) != 0;
}

/* ---- Record queue (canonical mode) ---- */

static int rec_push(nx_ldisc_t *ld, uint32_t len) {
    if (ld->rec_count >= NX_LDISC_RECS || len > REC_LEN_MAX) return -1;
    uint32_t idx = (ld->rec_head + ld->rec_count) % NX_LDISC_RECS;
    ld->rec[idx] = (uint16_t)len;
    ld->rec_count++;
    return 0;
}

static void rec_pop(nx_ldisc_t *ld) {
    if (ld->rec_count == 0) return;
    ld->rec_head = (ld->rec_head + 1) % NX_LDISC_RECS;
    ld->rec_count--;
}

static void rec_clear(nx_ldisc_t *ld) {
    ld->rec_head = 0;
    ld->rec_count = 0;
}

/* ---- Echo ---- */

/* Queue echo bytes all-or-nothing, so the screen never shows half a "^C" */
static void echo_bytes(nx_ldisc_t *ld, const uint8_t *b, uint32_t n) {
    if (!(ld->mode & NX_LDISC_ECHO)) return;
    if (nx_ring_free(&ld->out) < n) {
        ld->echo_dropped += n;
        return;
    }
    nx_ring_write(&ld->out, b, n);
}

static void echo_newline(nx_ldisc_t *ld) {
    static const uint8_t crlf[2] = { '\r', '\n' };
    if (ld->mode & NX_LDISC_ONLCR) echo_bytes(ld, crlf, 2);
    else echo_bytes(ld, crlf + 1, 1);
}

/* Control characters other than tab echo as "^X" in canonical mode */
static int echo_as_caret(uint8_t c) {
    return (c < 0x20 && c != '\t' && c != '\n') || c == NX_LDISC_CH_DEL;
}

static void echo_char(nx_ldisc_t *ld, uint8_t c) {
    if (c == '\n') {
        echo_newline(ld);
    } else if (is_canon(ld) && echo_as_caret(c)) {
        uint8_t caret[2] = { '^', (uint8_t)(c == NX_LDISC_CH_DEL ? '?' : c + 0x40) };
        echo_bytes(ld, caret, 2);
    } else {
        echo_bytes(ld, &c, 1);
    }
}

static void echo_erase(nx_ldisc_t *ld, uint32_t columns) {
    static const uint8_t erase[3] = { '\b', ' ', '\b' };
    for (uint32_t i = 0; i < columns; i++) echo_bytes(ld, erase, 3);
}

/* ---- Line editing (canonical mode) ---- */

/* Remove the last character (a whole UTF-8 sequence) from the line and echo
 * its erasure. Returns 0 when the line was already empty. */
static int erase_one(nx_ldisc_t *ld) {
    if (ld->line_len == 0) return 0;
    uint32_t end = ld->line_len;
    while (ld->line_len > 0 && (ld->line[ld->line_len - 1] & 0xC0) == 0x80) {
        ld->line_len--;                         /* continuation bytes */
    }
    if (ld->line_len > 0) {
        ld->line_len--;                         /* lead byte or ASCII */
    }
    uint8_t first = ld->line[ld->line_len];
    /* A caret-echoed control character takes two columns */
    echo_erase(ld, (end - ld->line_len == 1 && echo_as_caret(first)) ? 2 : 1);
    return 1;
}

/* Move the line (len bytes) into the slave queue as one record.
 * Returns 0, or -1 if it does not fit (nothing changes). */
static int push_line(nx_ldisc_t *ld) {
    if (nx_ring_free(&ld->in) < ld->line_len || ld->rec_count >= NX_LDISC_RECS) {
        return -1;
    }
    nx_ring_write(&ld->in, ld->line, ld->line_len);
    rec_push(ld, ld->line_len);
    ld->line_len = 0;
    return 0;
}

/* ---- Public API ---- */

int nx_ldisc_init(nx_ldisc_t *ld, uint8_t *in_buf, uint32_t in_cap,
                  uint8_t *out_buf, uint32_t out_cap, uint32_t mode) {
    if (!ld || !in_buf || !out_buf) return -1;
    if (in_cap == 0 || out_cap == 0 || in_cap > REC_LEN_MAX) return -1;
    nx_ring_init(&ld->in, in_buf, in_cap);
    nx_ring_init(&ld->out, out_buf, out_cap);
    ld->mode = mode & NX_LDISC_MODE_ALL;
    rec_clear(ld);
    ld->line_len = 0;
    ld->echo_dropped = 0;
    ld->input_dropped = 0;
    return 0;
}

int nx_ldisc_set_mode(nx_ldisc_t *ld, uint32_t mode) {
    if (!ld || (mode & ~NX_LDISC_MODE_ALL)) return -1;
    int was_canon = is_canon(ld);
    int now_canon = (mode & NX_LDISC_CANON) != 0;

    if (was_canon && !now_canon) {
        /* Queued lines become plain bytes; the line being edited follows
         * them as far as it fits (the rest is lost and counted). */
        rec_clear(ld);
        uint32_t moved = nx_ring_write(&ld->in, ld->line, ld->line_len);
        ld->input_dropped += ld->line_len - moved;
        ld->line_len = 0;
    } else if (!was_canon && now_canon) {
        rec_clear(ld);
        uint32_t queued = nx_ring_used(&ld->in);
        if (queued > 0) rec_push(ld, queued);   /* in.cap <= 65535, always fits */
    }
    ld->mode = mode;
    return 0;
}

uint32_t nx_ldisc_input(nx_ldisc_t *ld, const uint8_t *src, uint32_t n,
                        uint32_t *events) {
    uint32_t ev = 0;
    uint32_t i = 0;
    if (!ld || !src) {
        if (events) *events = 0;
        return 0;
    }

    for (; i < n; i++) {
        uint8_t c = src[i];

        if ((ld->mode & NX_LDISC_ICRNL) && c == '\r') c = '\n';

        if ((ld->mode & NX_LDISC_ISIG) && c == NX_LDISC_CH_INTR) {
            /* Discard everything typed but not yet read, show ^C */
            ld->line_len = 0;
            nx_ring_clear(&ld->in);
            rec_clear(ld);
            static const uint8_t caret_c[2] = { '^', 'C' };
            echo_bytes(ld, caret_c, 2);
            echo_newline(ld);
            ev |= NX_LDISC_EV_INTR;
            continue;
        }

        if (!is_canon(ld)) {
            if (nx_ring_put(&ld->in, c) == 0) break;    /* full: retry later */
            echo_char(ld, c);
            continue;
        }

        switch (c) {
        case NX_LDISC_CH_BS:
        case NX_LDISC_CH_DEL:
            erase_one(ld);
            break;
        case NX_LDISC_CH_KILL:
            while (erase_one(ld)) { }
            break;
        case NX_LDISC_CH_EOF:
            if (ld->line_len == 0) {
                if (rec_push(ld, 0) != 0) goto stop;    /* EOF mark */
            } else if (push_line(ld) != 0) {
                goto stop;                              /* line without newline */
            }
            break;
        case '\n':
            /* line_len < LINE_MAX always holds, so the newline has room */
            ld->line[ld->line_len++] = '\n';
            if (push_line(ld) != 0) {
                ld->line_len--;                         /* not consumed */
                goto stop;
            }
            echo_newline(ld);
            break;
        default:
            if (ld->line_len >= NX_LDISC_LINE_MAX - 1) {
                ld->input_dropped++;                    /* keep room for '\n' */
                break;
            }
            ld->line[ld->line_len++] = c;
            echo_char(ld, c);
            break;
        }
    }
stop:
    if (events) *events = ev;
    return i;
}

int32_t nx_ldisc_read(nx_ldisc_t *ld, uint8_t *dst, uint32_t n) {
    if (!ld || !dst || n == 0) return NX_LDISC_EMPTY;

    if (!is_canon(ld)) {
        if (nx_ring_used(&ld->in) == 0) return NX_LDISC_EMPTY;
        if (n > 0x7FFFFFFFu) n = 0x7FFFFFFFu;
        return (int32_t)nx_ring_read(&ld->in, dst, n);
    }

    if (ld->rec_count == 0) return NX_LDISC_EMPTY;
    uint32_t front = ld->rec[ld->rec_head];
    if (front == 0) {
        rec_pop(ld);                                    /* end-of-file mark */
        return 0;
    }
    uint32_t take = front < n ? front : n;
    uint32_t got = nx_ring_read(&ld->in, dst, take);
    ld->rec[ld->rec_head] = (uint16_t)(front - got);
    if (front - got == 0) rec_pop(ld);
    return (int32_t)got;
}

int nx_ldisc_readable(const nx_ldisc_t *ld) {
    if (!ld) return 0;
    if (is_canon(ld)) return ld->rec_count > 0;
    return nx_ring_used(&ld->in) > 0;
}

uint32_t nx_ldisc_output(nx_ldisc_t *ld, const uint8_t *src, uint32_t n) {
    if (!ld || !src) return 0;
    uint32_t i = 0;
    for (; i < n; i++) {
        if ((ld->mode & NX_LDISC_ONLCR) && src[i] == '\n') {
            if (nx_ring_free(&ld->out) < 2) break;
            static const uint8_t crlf[2] = { '\r', '\n' };
            nx_ring_write(&ld->out, crlf, 2);
        } else if (nx_ring_put(&ld->out, src[i]) == 0) {
            break;
        }
    }
    return i;
}

uint32_t nx_ldisc_read_output(nx_ldisc_t *ld, uint8_t *dst, uint32_t n) {
    if (!ld) return 0;
    return nx_ring_read(&ld->out, dst, n);
}

uint32_t nx_ldisc_output_used(const nx_ldisc_t *ld) {
    return ld ? nx_ring_used(&ld->out) : 0;
}

uint32_t nx_ldisc_output_room(const nx_ldisc_t *ld) {
    return ld ? nx_ring_free(&ld->out) : 0;
}
