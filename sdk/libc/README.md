# nxlibc, the NeolyxOS C library

nxlibc is the C library for NeolyxOS programs. It is written from scratch for
NeolyxOS and runs on NeolyxOS's own system calls. It gives C programs the
ISO C11 library they expect (strings, memory, formatted I/O, files, time,
math) and `<nx/sys.h>`, a typed wrapper for every registered kernel syscall.

It is not POSIX. There is no `unistd.h`, `fork`, signals, `errno` values
borrowed from another system, or file descriptors that behave like another
system's. Where NeolyxOS behaves differently, the library follows NeolyxOS.

Version: 0.1.0 (`NXLIBC_VERSION`, `nxc_version()`).

## Building a program

Any x86_64 GCC on the development machine (Linux, or Windows with MSYS2)
works; no cross-compiler is needed. Copy `examples/hello/` to start:

```make
NXLIBC_DIR := /path/to/neolyx-os/sdk/libc
include $(NXLIBC_DIR)/nxlibc.mk

hello: hello.o
	$(LD) $(NXLIBC_LDFLAGS) -o $@ $(NXLIBC_CRT0) hello.o $(NXLIBC_LIB)

%.o: %.c
	$(CC) $(NXLIBC_CFLAGS) -O2 -c $< -o $@
```

| Variable | Value |
|---|---|
| `NXLIBC_CFLAGS` | `-std=c11 -m64 -ffreestanding -fno-pie -fno-stack-protector -mno-red-zone -fno-asynchronous-unwind-tables -nostdinc -I sdk/libc/include -isystem <gcc include>` |
| `NXLIBC_LDFLAGS` | `-nostdlib -static -no-pie -z noexecstack -T sdk/libc/crt/nxapp.ld` |
| `NXLIBC_CRT0` | `sdk/libc/build/crt0.o` (link first) |
| `NXLIBC_LIB` | `sdk/libc/build/libnxc.a` (link after your objects) |

`make -C sdk/libc` builds the library. Programs are static ELF files linked
at 0x1000000, the start of every process's private address space. Install
one in `/apps` on the boot disk and it appears in the desktop's app drawer.

`-nostdinc` keeps the host's C headers out. Besides nxlibc's own headers,
only the compiler's freestanding headers are used: `stddef.h`, `stdint.h`,
`stdarg.h`, `stdbool.h`, `stdalign.h`, `stdnoreturn.h`, `float.h`,
`iso646.h`.

Do not link nxlibc together with `desktop/lib/libc_stubs.c`,
`desktop/lib/nx_malloc.c` or `sdk/crt/rx_mem_neolyx.c`: they define the
same functions. A second heap allocator on the same `brk` is detected and
ends the program with a clear message.

## Public headers

These headers and the names in them are nxlibc's public API (proposed for
the Beta ABI batch: `docs/sdk/proposals/nxlibc-public-headers.md`).

| Header | Contents |
|---|---|
| `assert.h` | `assert`, `static_assert` |
| `ctype.h` | `is*` classes, `tolower`, `toupper` (ASCII; bytes 128-255 are in no class) |
| `errno.h` | `errno` and the NeolyxOS error numbers (the kernel's, plus `EDOM`, `EOVERFLOW`, `EILSEQ`) |
| `inttypes.h` | `PRI*`/`SCN*` macros, `imaxabs`, `imaxdiv`, `strtoimax`, `strtoumax` |
| `limits.h` | integer limits, `MB_LEN_MAX`, `NXC_PATH_MAX` |
| `math.h` | trig, hyperbolic, exp/log family, `pow`, `sqrt`, `cbrt`, `hypot`, rounding, `fmod`, `frexp`/`ldexp`/`modf`/`scalbn`, classification macros; `float` forms of each |
| `setjmp.h` | `setjmp`, `longjmp` |
| `stdio.h` | `FILE` streams, `fopen`... `fclose`, `printf`/`scanf` families, `remove`, `rename`, `perror` |
| `stdlib.h` | allocator, `strto*`/`ato*`, `qsort`, `bsearch`, `rand`, `abs`/`div` families, `exit`, `atexit`, `quick_exit`, `_Exit`, `abort`, `getenv`, `system` |
| `string.h` | `mem*`, `str*`, `strerror`, plus `strnlen`, `strdup`, `strndup`, `memccpy`, `strtok_r` |
| `time.h` | `time`, `clock`, `difftime`, `mktime`, `gmtime(_r)`, `localtime(_r)`, `strftime`, `asctime`, `ctime`, `timespec_get` |
| `nx/sys.h` | `nxsys_*`: one function per registered syscall, the kernel structures (`nxsys_*_t`), syscall numbers (`NXSYS_*`), ABI flags |
| `nx/libc.h` | nxlibc extras: `nxc_version`, heap statistics and checks, `nxc_debugf` (print to the kernel log) |

Not provided: wide characters and multibyte conversion (`wchar.h`,
`uchar.h`, `mblen`...), `locale.h`, `signal.h`, threads, `complex.h`,
`fenv.h`, `tmpfile`/`tmpnam`. `system()` reports `ENOSYS` until there is a
shell to run.

## How it behaves on NeolyxOS

**Start-up.** `crt0` receives argc/argv (and envp, when the kernel passes
one) from the kernel, sets a known floating-point state (x87 64-bit
precision, round to nearest; SSE `MXCSR` 0x1F80), runs constructors and
calls `main`. Returning from `main` calls `exit`.

**Standard streams.** Today's kernel starts a program with no open
descriptors, so `stdout` and `stderr` write to the kernel debug log (the
serial console) and `stdin` is at end of file. When the kernel sets
`NXSYS_ABI_FLAG_FILEIO` (stream S3), they become descriptors 0, 1 and 2,
with the log as the fallback if a descriptor is not open. `stdout` is line
buffered, `stderr` unbuffered.

**Errors.** Library functions report errors in `errno` with NeolyxOS's own
numbers (`errno.h`). The kernel returns `-errno`; before the FILEIO flag,
some file syscalls return the VFS's internal codes instead, and nxlibc
translates them (`nxsys_file_errno`). `nxsys_*` wrappers themselves return
the kernel's raw value and never touch `errno`.

**Features that need newer kernels** test the ABI flag (`nxsys_abi_has()`)
and fail with `ENOSYS` when it is missing. They never guess.

**Files.** Paths are at most 255 bytes (`FILENAME_MAX` 256), and a
process can have 16 files open (`FOPEN_MAX`). Apps may write only under
`/tmp`, `/Users` and `/home` (the kernel's System Integrity Protection).

**Memory.** `malloc` and friends use a heap on the kernel's `brk` (64 MB per
process today). Every block carries a checked header: freeing a pointer
twice, freeing a pointer `malloc` did not return, or a damaged header logs
the address and ends the program with status 134 (`abort`). Free memory at
the top of the heap goes back to the kernel. `realloc(p, 0)` returns a
minimal block and keeps `p` valid until it is freed.

**printf** formats floating point exactly (every digit of `%.330f` is the
true decimal value) with round-half-even, and has `%a`. `%n` is refused
(`EINVAL`, logged): it turns format-string bugs into memory writes.
**strtod** is correctly rounded for any input length.

**Math** is computed in x87 extended precision and rounded once. Measured
against a long double reference (`tests/host/libc_math_test.c`): every
function is within 0.51 ulp and was correctly rounded on all 100,000 random
arguments per function. Errors set `errno` (`EDOM`, `ERANGE`).

**Time** is UTC only (`localtime` is `gmtime`). `time()` returns -1 when
the kernel's real-time clock is not set. `clock()` counts milliseconds
since the program started (`CLOCKS_PER_SEC` 1000). `strftime` writes years
with at least four digits (ISO 8601).

## Tests

```sh
make -C sdk/libc check-host     # all host tests, twice: kernel ABI 1.4 and the FILEIO ABI
```

The host tests link the same library objects that go into `libnxc.a`;
only `src/nx/syscall.c` is replaced, by `tests/host/libc_host_sys.c`, which
answers each syscall the way the NeolyxOS kernel does using Linux system
calls. No host C library is linked into them.

| Test | Covers |
|---|---|
| `libc_abi_check` | every `nx/sys.h` struct size, field offset, constant and syscall number, and every errno value, against the kernel's own headers |
| `libc_printf_test` | 159 formatting cases, truncation, errors |
| `libc_strtod_test` | hard rounding cases, specials, 200,000 round trips |
| `libc_malloc_test` | 200,000-operation stress with consistency checks, out of memory, trimming, misuse aborts |
| `libc_stdio_test` | every `fopen` mode, positioning, buffering, running out of descriptors, `scanf` |
| `libc_math_test` | accuracy and ISO special cases (host program, long double reference) |
| `libc_time_test` | calendar against an independent implementation, `mktime` normalisation, `strftime` |
| `libc_misc_test` | strings, ctype, qsort, strtol, setjmp, atexit |

On NeolyxOS: `tests/userland/libctest` (install as `/apps/libctest`)
prints `[LIBCTEST] RESULT: PASS`; `examples/hello` and
`tests/userland/ticker` are nxlibc programs too.

## Layout

```
include/        public headers
crt/crt0.S      _start
crt/nxapp.ld    program layout (0x1000000)
src/nx/         syscall wrappers (the only code that enters the kernel), ABI flags
src/start/      start-up, debug log
src/stdio/      streams, printf, scanf
src/stdlib/     allocator, conversions, qsort, exit
src/math/       extended-precision kernels; constants.h from tools/gen_math_constants.py
src/...         string, ctype, errno, time, setjmp, assert
examples/hello  template program
```
