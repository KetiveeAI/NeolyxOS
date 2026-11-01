/*
 * nxlibc - FILE streams: opening, buffering, reading, writing, seeking.
 *
 * A stream is one of three kinds:
 *   FK_FD    a kernel file descriptor
 *   FK_LOG   the kernel debug log (debug.print): output only, used for
 *            stdout/stderr when the process has no descriptors 1/2
 *   FK_NULL  always at end of file, discards output: stdin without fd 0
 *
 * Buffering. Every stream has a buffer used for one direction at a time.
 * _IOFBF writes when the buffer is full, _IOLBF also after a newline,
 * _IONBF at the end of every library call. Reading from a stream that is
 * line buffered or unbuffered first flushes stdout, so prompts appear
 * before input is read.
 *
 * Switching between reading and writing without fflush/fseek in between
 * is undefined in ISO C; nxlibc handles it anyway (it repositions the
 * kernel offset and drops the read buffer).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <nx/sys.h>
#include "../internal.h"

enum { FK_FD = 0, FK_LOG = 1, FK_NULL = 2 };

#define F_READ      0x0001u
#define F_WRITE     0x0002u
#define F_APPEND    0x0004u     /* every write goes to the end */
#define F_EOF       0x0010u
#define F_ERR       0x0020u
#define F_OWNBUF    0x0040u     /* buffer was malloc'd by the library */
#define F_STATIC    0x0080u     /* stdin/stdout/stderr: never freed */
#define F_STDFD     0x0100u     /* std stream on fd 0-2: fall back to the log on EBADF */
#define F_USED      0x0200u     /* I/O happened: setvbuf no longer allowed */

#define SMALL_BUF   64

struct __nxc_file {
    int            kind;
    int            fd;
    unsigned       flags;
    int            bufmode;     /* _IOFBF, _IOLBF, _IONBF */
    unsigned char *buf;
    size_t         bufsize;
    size_t         rpos, rend;  /* read window: buf[rpos..rend) unread */
    size_t         wlen;        /* buf[0..wlen) waiting to be written */
    int            ungot;       /* pushed-back character, or -1 */
    struct __nxc_file *next;    /* list of open streams */
    unsigned char  small[SMALL_BUF];    /* used when no buffer can be had */
};

static unsigned char g_outbuf[BUFSIZ];
static unsigned char g_errbuf[256];
static unsigned char g_inbuf[BUFSIZ];

static struct __nxc_file g_stdin = {
    .kind = FK_NULL, .fd = -1, .flags = F_READ | F_STATIC, .bufmode = _IOLBF,
    .buf = g_inbuf, .bufsize = sizeof(g_inbuf), .ungot = -1,
};
static struct __nxc_file g_stdout = {
    .kind = FK_LOG, .fd = -1, .flags = F_WRITE | F_STATIC, .bufmode = _IOLBF,
    .buf = g_outbuf, .bufsize = sizeof(g_outbuf), .ungot = -1,
};
static struct __nxc_file g_stderr = {
    .kind = FK_LOG, .fd = -1, .flags = F_WRITE | F_STATIC, .bufmode = _IONBF,
    .buf = g_errbuf, .bufsize = sizeof(g_errbuf), .ungot = -1,
};

FILE *const stdin = &g_stdin;
FILE *const stdout = &g_stdout;
FILE *const stderr = &g_stderr;

static FILE *g_open;            /* streams from fopen, newest first */

/* ============ Set-up ============ */

void __nxc_stdio_init(void) {
    if (nxsys_abi_has(NXSYS_ABI_FLAG_FILEIO)) {
        /* The kernel gives standard descriptors; a missing one reads as
         * EOF (stdin) or falls back to the debug log (stdout/stderr). */
        g_stdin.kind = FK_FD;
        g_stdin.fd = 0;
        g_stdin.flags |= F_STDFD;
        g_stdout.kind = FK_FD;
        g_stdout.fd = 1;
        g_stdout.flags |= F_STDFD;
        g_stderr.kind = FK_FD;
        g_stderr.fd = 2;
        g_stderr.flags |= F_STDFD;
    }
}

int __nxc_stderr_is_log(void) {
    return g_stderr.kind == FK_LOG;
}

/* ============ Low-level output ============ */

/* Sends bytes to the debug log; NUL bytes are dropped (debug.print stops at them) */
static void log_write(const unsigned char *data, size_t n) {
    char piece[256];
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (data[i] == '\0') continue;
        piece[k++] = (char)data[i];
        if (k == sizeof(piece) - 1 || data[i] == '\n') {
            piece[k] = '\0';
            nxsys_debug_print(piece);
            k = 0;
        }
    }
    if (k) {
        piece[k] = '\0';
        nxsys_debug_print(piece);
    }
}

/* Writes all n bytes to the stream's target. 0 or -1 (errno, F_ERR set). */
static int raw_write(FILE *f, const unsigned char *data, size_t n) {
    if (f->kind == FK_LOG) {
        log_write(data, n);
        return 0;
    }
    if (f->kind == FK_NULL) return 0;
    if (f->flags & F_APPEND) {
        long r = nxsys_seek(f->fd, 0, NXSYS_SEEK_END);
        if (r < 0) {
            __nxc_file_ret(r, NXSYS_SEEK);
            f->flags |= F_ERR;
            return -1;
        }
    }
    while (n > 0) {
        long r = nxsys_write(f->fd, data, n);
        if (r < 0) {
            int err = nxsys_file_errno(r, NXSYS_WRITE);
            if (err == EBADF && (f->flags & F_STDFD)) {
                /* no standard descriptor: this stream goes to the log */
                f->kind = FK_LOG;
                f->fd = -1;
                f->flags &= ~(F_STDFD | F_APPEND);
                log_write(data, n);
                return 0;
            }
            errno = err;
            f->flags |= F_ERR;
            return -1;
        }
        if (r == 0) {
            /* the kernel made no progress: report instead of spinning */
            errno = EIO;
            f->flags |= F_ERR;
            return -1;
        }
        data += r;
        n -= (size_t)r;
    }
    return 0;
}

static int flush_write(FILE *f) {
    if (f->wlen == 0) return 0;
    size_t n = f->wlen;
    f->wlen = 0;
    return raw_write(f, f->buf, n);
}

/* Drops buffered input, moving the kernel offset back to the logical
 * position. 0 or -1 (errno set). */
static int drop_read(FILE *f) {
    size_t unread = f->rend - f->rpos + (f->ungot >= 0 ? 1u : 0u);
    f->rpos = f->rend = 0;
    f->ungot = -1;
    if (unread && f->kind == FK_FD) {
        long r = nxsys_seek(f->fd, -(int64_t)unread, NXSYS_SEEK_CUR);
        if (r < 0) {
            __nxc_file_ret(r, NXSYS_SEEK);
            f->flags |= F_ERR;
            return -1;
        }
    }
    return 0;
}

static void ensure_buffer(FILE *f) {
    if (f->buf) return;
    if (f->bufmode != _IONBF) {
        f->buf = (unsigned char *)malloc(BUFSIZ);
        if (f->buf) {
            f->bufsize = BUFSIZ;
            f->flags |= F_OWNBUF;
            return;
        }
    }
    f->buf = f->small;
    f->bufsize = sizeof(f->small);
}

/* Library-internal write of n bytes through the buffer; returns bytes accepted */
size_t __nxc_stream_write(FILE *f, const void *data, size_t n) {
    if (!f || !(f->flags & F_WRITE)) {
        if (f) f->flags |= F_ERR;
        errno = EBADF;
        return 0;
    }
    f->flags |= F_USED;
    if (f->rend > f->rpos || f->ungot >= 0) {
        if (drop_read(f) != 0) return 0;
    }
    ensure_buffer(f);
    const unsigned char *p = (const unsigned char *)data;
    size_t done = 0;
    /* Large writes with an empty buffer go straight through */
    if (f->wlen == 0 && n >= f->bufsize && f->kind == FK_FD) {
        if (raw_write(f, p, n) != 0) return 0;
        return n;
    }
    while (done < n) {
        size_t room = f->bufsize - f->wlen;
        if (room == 0) {
            if (flush_write(f) != 0) return done;
            room = f->bufsize;
        }
        size_t k = (n - done < room) ? n - done : room;
        memcpy(f->buf + f->wlen, p + done, k);
        f->wlen += k;
        done += k;
    }
    if (f->bufmode == _IOLBF && memchr(p, '\n', n)) {
        if (flush_write(f) != 0) return done;
    }
    return done;
}

/* End of a library output call: unbuffered streams flush now */
static int end_call(FILE *f) {
    if (f->bufmode == _IONBF) return flush_write(f);
    return 0;
}

/* ============ Low-level input ============ */

/* Refills the read buffer. 1 if data is available, 0 at EOF, -1 on error. */
static int refill(FILE *f) {
    if (f->wlen && flush_write(f) != 0) return -1;
    if (f->bufmode != _IOFBF && f != stdout) fflush(stdout);
    ensure_buffer(f);
    f->rpos = f->rend = 0;
    if (f->kind != FK_FD) {
        f->flags |= F_EOF;
        return 0;
    }
    long r = nxsys_read(f->fd, f->buf, f->bufsize);
    if (r < 0) {
        int err = nxsys_file_errno(r, NXSYS_READ);
        if (err == EBADF && (f->flags & F_STDFD)) {
            /* no standard input descriptor: end of file */
            f->kind = FK_NULL;
            f->fd = -1;
            f->flags = (f->flags & ~F_STDFD) | F_EOF;
            return 0;
        }
        errno = err;
        f->flags |= F_ERR;
        return -1;
    }
    if (r == 0) {
        f->flags |= F_EOF;
        return 0;
    }
    f->rend = (size_t)r;
    return 1;
}

static int getc_inner(FILE *f) {
    if (f->ungot >= 0) {
        int c = f->ungot;
        f->ungot = -1;
        return c;
    }
    if (f->rpos >= f->rend) {
        if (refill(f) <= 0) return EOF;
    }
    return f->buf[f->rpos++];
}

static int check_read(FILE *f) {
    if (!f || !(f->flags & F_READ)) {
        if (f) f->flags |= F_ERR;
        errno = EBADF;
        return -1;
    }
    f->flags |= F_USED;
    if (f->wlen && flush_write(f) != 0) return -1;
    return 0;
}

/* ============ Opening and closing ============ */

/* Parses an fopen mode; returns kernel open flags and stream flags, or -1 */
static int parse_mode(const char *mode, uint32_t *oflags, unsigned *fflags, int *excl) {
    if (!mode) return -1;
    int plus = 0, x = 0;
    char base = mode[0];
    if (base != 'r' && base != 'w' && base != 'a') return -1;
    for (const char *m = mode + 1; *m; m++) {
        if (*m == '+') plus = 1;
        else if (*m == 'b') continue;           /* no text/binary difference */
        else if (*m == 'x') x = 1;
        else return -1;
    }
    if (x && base != 'w') return -1;
    *excl = x;
    switch (base) {
    case 'r':
        *oflags = plus ? NXSYS_O_RDWR : NXSYS_O_RDONLY;
        *fflags = F_READ | (plus ? F_WRITE : 0);
        break;
    case 'w':
        *oflags = (plus ? NXSYS_O_RDWR : NXSYS_O_WRONLY) | NXSYS_O_CREATE | NXSYS_O_TRUNC;
        *fflags = F_WRITE | (plus ? F_READ : 0);
        break;
    default: /* 'a' */
        *oflags = (plus ? NXSYS_O_RDWR : NXSYS_O_WRONLY) | NXSYS_O_CREATE | NXSYS_O_APPEND;
        *fflags = F_WRITE | F_APPEND | (plus ? F_READ : 0);
        break;
    }
    return 0;
}

/* Opens path for a stream; returns the fd or -1 (errno set) */
static int open_path(const char *path, const char *mode, unsigned *fflags) {
    uint32_t oflags;
    int excl;
    if (!path || parse_mode(mode, &oflags, fflags, &excl) != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t len = strnlen(path, NXSYS_PATH_MAX);
    if (len == 0) {
        errno = ENOENT;
        return -1;
    }
    if (len >= NXSYS_PATH_MAX) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (excl) {
        /* "wx": fail if the file exists. The kernel has no exclusive-create
         * flag yet, so this check and the open are two steps. */
        nxsys_stat_t st;
        if (nxsys_stat(path, &st) == 0) {
            errno = EEXIST;
            return -1;
        }
    }
    long fd = nxsys_open(path, oflags, 0644);
    if (fd < 0) {
        __nxc_file_ret(fd, NXSYS_OPEN);
        return -1;
    }
    return (int)fd;
}

static void stream_reset(FILE *f, int fd, unsigned fflags) {
    f->kind = FK_FD;
    f->fd = fd;
    f->flags = fflags | (f->flags & (F_STATIC | F_OWNBUF));
    f->rpos = f->rend = f->wlen = 0;
    f->ungot = -1;
}

FILE *fopen(const char *restrict path, const char *restrict mode) {
    unsigned fflags;
    int fd = open_path(path, mode, &fflags);
    if (fd < 0) return NULL;
    FILE *f = (FILE *)calloc(1, sizeof(FILE));
    if (!f) {
        nxsys_close(fd);
        errno = ENOMEM;
        return NULL;
    }
    stream_reset(f, fd, fflags);
    f->bufmode = _IOFBF;
    f->buf = NULL;
    f->bufsize = 0;
    f->next = g_open;
    g_open = f;
    return f;
}

/* Flushes and releases the stream's descriptor; keeps the FILE. 0 or EOF. */
static int stream_close_fd(FILE *f) {
    int rc = 0;
    if (f->wlen && flush_write(f) != 0) rc = EOF;
    if (f->kind == FK_FD && f->fd >= 0) {
        long r = nxsys_close(f->fd);
        if (r < 0) {
            __nxc_file_ret(r, NXSYS_CLOSE);
            rc = EOF;
        }
    }
    f->fd = -1;
    f->rpos = f->rend = 0;
    f->ungot = -1;
    return rc;
}

static void unlink_stream(FILE *f) {
    for (FILE **pp = &g_open; *pp; pp = &(*pp)->next) {
        if (*pp == f) {
            *pp = f->next;
            return;
        }
    }
}

int fclose(FILE *f) {
    if (!f) {
        errno = EBADF;
        return EOF;
    }
    int rc = stream_close_fd(f);
    if (f->flags & F_STATIC) {
        /* a standard stream: it stays usable as an empty/log stream */
        f->kind = (f == stdin) ? FK_NULL : FK_LOG;
        f->flags &= ~(F_STDFD | F_APPEND | F_EOF | F_ERR);
        return rc;
    }
    unlink_stream(f);
    if (f->flags & F_OWNBUF) free(f->buf);
    free(f);
    return rc;
}

FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict f) {
    if (!f) {
        errno = EBADF;
        return NULL;
    }
    if (!path) {
        /* changing the mode of the open file is not supported */
        stream_close_fd(f);
        errno = EINVAL;
        return NULL;
    }
    stream_close_fd(f);
    unsigned fflags;
    int fd = open_path(path, mode, &fflags);
    if (fd < 0) {
        if (!(f->flags & F_STATIC)) {
            unlink_stream(f);
            if (f->flags & F_OWNBUF) free(f->buf);
            free(f);
        } else {
            f->kind = (f == stdin) ? FK_NULL : FK_LOG;
        }
        return NULL;
    }
    stream_reset(f, fd, fflags);
    return f;
}

/* ============ Buffering ============ */

int fflush(FILE *f) {
    if (!f) {
        __nxc_stdio_flush_all();
        return 0;
    }
    if (f->wlen && flush_write(f) != 0) return EOF;
    if (f->rend > f->rpos || f->ungot >= 0) {
        /* ISO C leaves fflush on input undefined; drop the read-ahead */
        if (drop_read(f) != 0) return EOF;
    }
    return 0;
}

void __nxc_stdio_flush_all(void) {
    for (FILE *f = g_open; f; f = f->next) {
        if (f->wlen) flush_write(f);
    }
    if (g_stdout.wlen) flush_write(&g_stdout);
    if (g_stderr.wlen) flush_write(&g_stderr);
}

int setvbuf(FILE *restrict f, char *restrict buf, int mode, size_t size) {
    if (!f || (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) {
        errno = EINVAL;
        return -1;
    }
    if (f->flags & F_USED) {
        errno = EINVAL;     /* only allowed before the first I/O */
        return -1;
    }
    if (buf && size > 0) {
        if (f->flags & F_OWNBUF) free(f->buf);
        f->flags &= ~F_OWNBUF;
        f->buf = (unsigned char *)buf;
        f->bufsize = size;
    } else if (size > 0 && mode != _IONBF && !(f->flags & F_STATIC)) {
        unsigned char *nb = (unsigned char *)malloc(size);
        if (!nb) {
            errno = ENOMEM;
            return -1;
        }
        if (f->flags & F_OWNBUF) free(f->buf);
        f->buf = nb;
        f->bufsize = size;
        f->flags |= F_OWNBUF;
    }
    f->bufmode = mode;
    return 0;
}

void setbuf(FILE *restrict f, char *restrict buf) {
    setvbuf(f, buf, buf ? _IOFBF : _IONBF, BUFSIZ);
}

/* ============ Character and block I/O ============ */

int fgetc(FILE *f) {
    if (check_read(f) != 0) return EOF;
    return getc_inner(f);
}

int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }

int ungetc(int c, FILE *f) {
    if (c == EOF || !f || !(f->flags & F_READ) || f->ungot >= 0) return EOF;
    if (f->wlen && flush_write(f) != 0) return EOF;
    f->ungot = (unsigned char)c;
    f->flags &= ~F_EOF;
    return (unsigned char)c;
}

char *fgets(char *restrict s, int n, FILE *restrict f) {
    if (!s || n <= 0) {
        errno = EINVAL;
        return NULL;
    }
    if (check_read(f) != 0) return NULL;
    int i = 0;
    while (i < n - 1) {
        int c = getc_inner(f);
        if (c == EOF) break;
        s[i++] = (char)c;
        if (c == '\n') break;
    }
    if (i == 0 && n > 1) return NULL;   /* EOF or error before any character */
    s[i] = '\0';
    return s;
}

int fputc(int c, FILE *f) {
    unsigned char ch = (unsigned char)c;
    if (__nxc_stream_write(f, &ch, 1) != 1) return EOF;
    if (end_call(f) != 0) return EOF;
    return ch;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int putchar(int c) { return fputc(c, stdout); }

int fputs(const char *restrict s, FILE *restrict f) {
    size_t n = strlen(s);
    if (__nxc_stream_write(f, s, n) != n) return EOF;
    if (end_call(f) != 0) return EOF;
    return 0;
}

int puts(const char *s) {
    size_t n = strlen(s);
    if (__nxc_stream_write(stdout, s, n) != n) return EOF;
    if (__nxc_stream_write(stdout, "\n", 1) != 1) return EOF;
    if (end_call(stdout) != 0) return EOF;
    return 0;
}

size_t fread(void *restrict buf, size_t size, size_t count, FILE *restrict f) {
    if (size == 0 || count == 0) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        return 0;
    }
    if (check_read(f) != 0) return 0;
    unsigned char *p = (unsigned char *)buf;
    size_t want = size * count, got = 0;
    if (f->ungot >= 0) {
        p[got++] = (unsigned char)f->ungot;
        f->ungot = -1;
    }
    while (got < want) {
        size_t avail = f->rend - f->rpos;
        if (avail) {
            size_t k = (want - got < avail) ? want - got : avail;
            memcpy(p + got, f->buf + f->rpos, k);
            f->rpos += k;
            got += k;
            continue;
        }
        if (f->kind == FK_FD && want - got >= f->bufsize && f->bufsize) {
            /* large read: straight into the caller's buffer */
            long r = nxsys_read(f->fd, p + got, want - got);
            if (r < 0) {
                __nxc_file_ret(r, NXSYS_READ);
                f->flags |= F_ERR;
                break;
            }
            if (r == 0) {
                f->flags |= F_EOF;
                break;
            }
            got += (size_t)r;
            continue;
        }
        if (refill(f) <= 0) break;
    }
    return got / size;
}

size_t fwrite(const void *restrict buf, size_t size, size_t count, FILE *restrict f) {
    if (size == 0 || count == 0) return 0;
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        return 0;
    }
    size_t n = size * count;
    size_t done = __nxc_stream_write(f, buf, n);
    if (done == n && end_call(f) != 0) done = 0;
    return done / size;
}

int __nxc_vfprintf_locked(FILE *f, const char *fmt, va_list ap) {
    if (!f || !(f->flags & F_WRITE)) {
        if (f) f->flags |= F_ERR;
        errno = EBADF;
        return -1;
    }
    nxc_sink_t s = { f, NULL, 0, 0, 0, 0 };
    int r = __nxc_vformat(&s, fmt, ap);
    if (end_call(f) != 0 && r >= 0) r = -1;
    return r;
}

/* ============ Positioning ============ */

int fseek(FILE *f, long offset, int whence) {
    if (!f) {
        errno = EBADF;
        return -1;
    }
    if (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) {
        errno = EINVAL;
        return -1;
    }
    if (f->kind != FK_FD) {
        errno = ESPIPE;
        return -1;
    }
    if (f->wlen && flush_write(f) != 0) return -1;
    if (whence == SEEK_CUR) {
        /* relative to the logical position, not the read-ahead */
        offset -= (long)(f->rend - f->rpos) + (f->ungot >= 0 ? 1 : 0);
    }
    f->rpos = f->rend = 0;
    f->ungot = -1;
    long r = nxsys_seek(f->fd, offset, whence);
    if (r < 0) {
        __nxc_file_ret(r, NXSYS_SEEK);
        return -1;
    }
    f->flags &= ~F_EOF;
    return 0;
}

long ftell(FILE *f) {
    if (!f) {
        errno = EBADF;
        return -1;
    }
    if (f->kind != FK_FD) {
        errno = ESPIPE;
        return -1;
    }
    long k = nxsys_seek(f->fd, 0, NXSYS_SEEK_CUR);
    if (k < 0) {
        __nxc_file_ret(k, NXSYS_SEEK);
        return -1;
    }
    long pos = k - (long)(f->rend - f->rpos) - (f->ungot >= 0 ? 1 : 0) + (long)f->wlen;
    if (f->flags & F_APPEND && f->wlen) {
        /* pending appended data lands at the end */
        long end = nxsys_seek(f->fd, 0, NXSYS_SEEK_END);
        if (end >= 0) {
            nxsys_seek(f->fd, k, NXSYS_SEEK_SET);
            pos = end + (long)f->wlen;
        }
    }
    if (pos < 0) {
        errno = EIO;
        return -1;
    }
    return pos;
}

void rewind(FILE *f) {
    if (fseek(f, 0, SEEK_SET) == 0 && f) f->flags &= ~F_ERR;
}

int fgetpos(FILE *restrict f, fpos_t *restrict pos) {
    if (!pos) {
        errno = EINVAL;
        return -1;
    }
    long p = ftell(f);
    if (p < 0) return -1;
    pos->__off = p;
    return 0;
}

int fsetpos(FILE *f, const fpos_t *pos) {
    if (!pos) {
        errno = EINVAL;
        return -1;
    }
    return fseek(f, (long)pos->__off, SEEK_SET);
}

/* ============ Errors ============ */

void clearerr(FILE *f) {
    if (f) f->flags &= ~(F_EOF | F_ERR);
}

int feof(FILE *f) { return f ? (f->flags & F_EOF) != 0 : 0; }
int ferror(FILE *f) { return f ? (f->flags & F_ERR) != 0 : 0; }

/* ============ Files by name ============ */

int remove(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    return (int)__nxc_file_ret(nxsys_rmdir(path), NXSYS_RMDIR) < 0 ? -1 : 0;
}

int rename(const char *oldpath, const char *newpath) {
    if (!oldpath || !newpath) {
        errno = EINVAL;
        return -1;
    }
    return (int)__nxc_file_ret(nxsys_rename(oldpath, newpath), NXSYS_RENAME) < 0 ? -1 : 0;
}

/* ============ scanf support ============ */

int __nxc_stream_getc(FILE *f) {
    if (check_read(f) != 0) return EOF;
    return getc_inner(f);
}

int __nxc_stream_ungetc(int c, FILE *f) {
    return ungetc(c, f);
}
