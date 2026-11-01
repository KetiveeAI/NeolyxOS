# Proposal: nxlibc public headers (Beta ABI batch)

Stream: S4 (nxlibc). Status: proposed for the Beta ABI batch (proposals due
2026-11-05, owner decision bundle 2026-11-06). Code: `sdk/libc/`.

nxlibc adds **no syscalls, no ABI flags and no kernel changes**. What needs
sign-off is the library's own public API: the header list below, the names
in it, and eight behaviour decisions (section 3). Programs built against it
(S2, S5, S6, S7) depend on these names from 2026-11-12.

## 1. Header list

Standard headers (ISO C11 subset, NeolyxOS error numbers):

| Header | Names |
|---|---|
| `assert.h` | `assert`, `static_assert` |
| `ctype.h` | `isalnum isalpha isblank iscntrl isdigit isgraph islower isprint ispunct isspace isupper isxdigit tolower toupper` |
| `errno.h` | `errno`; `EPERM`..`ETIMEDOUT` with the kernel's values, `EDOM` 33, `EOVERFLOW` 75, `EILSEQ` 84 (library only), the networking codes agreed with N (98, 101, 104, 106, 107, 111, 113) |
| `inttypes.h` | `PRI*`, `SCN*`, `imaxabs imaxdiv strtoimax strtoumax` |
| `limits.h` | ISO limits, `NXC_PATH_MAX` 256 |
| `math.h` | `acos asin atan atan2 cos sin tan sincos cosh sinh tanh exp exp2 expm1 log log10 log2 log1p frexp ldexp scalbn modf cbrt fabs hypot pow sqrt ceil floor trunc round lround llround fmod copysign nan fmax fmin fdim` and each `...f` form; `isnan isinf isfinite signbit fpclassify`, `HUGE_VAL INFINITY NAN`, `M_*` constants |
| `setjmp.h` | `jmp_buf setjmp longjmp` |
| `stdio.h` | `FILE fpos_t stdin stdout stderr EOF BUFSIZ FOPEN_MAX FILENAME_MAX _IOFBF _IOLBF _IONBF SEEK_*`; `fopen freopen fclose fflush setvbuf setbuf fgetc getc getchar ungetc fgets fputc putc putchar fputs puts fread fwrite fseek ftell rewind fgetpos fsetpos clearerr feof ferror perror remove rename`; `printf fprintf sprintf snprintf vprintf vfprintf vsprintf vsnprintf scanf fscanf sscanf vscanf vfscanf vsscanf` |
| `stdlib.h` | `malloc calloc realloc free aligned_alloc atof atoi atol atoll strtod strtof strtold strtol strtoll strtoul strtoull rand srand qsort bsearch abs labs llabs div ldiv lldiv exit atexit quick_exit at_quick_exit _Exit abort getenv system`; `RAND_MAX` 0x7FFFFFFF |
| `string.h` | `memcpy memmove memset memcmp memchr strcpy strncpy strcat strncat strcmp strncmp strcoll strxfrm strchr strrchr strstr strspn strcspn strpbrk strtok strlen strerror`; extensions `strnlen strdup strndup memccpy strtok_r` |
| `time.h` | `time_t` (64-bit) `clock_t struct tm struct timespec CLOCKS_PER_SEC` (1000) `TIME_UTC`; `time clock difftime mktime gmtime gmtime_r localtime localtime_r strftime asctime ctime timespec_get` |

From the compiler, unchanged: `stddef.h stdint.h stdarg.h stdbool.h
stdalign.h stdnoreturn.h float.h iso646.h`.

NeolyxOS headers:

| Header | Names |
|---|---|
| `nx/sys.h` | `NXSYS_<NAME>` for every syscall in `syscall_table.def` (the registry name, upper case, `.` as `_`); `long nxsys_<name>(...)` wrappers returning the kernel's raw value (no `errno`); mirrored structures `nxsys_*_t`; `NXSYS_ABI_FLAG_*` with the registry's bits; `nxsys_abi_flags()`, `nxsys_abi_has()`, `nxsys_file_errno()`, `nxsys_call()` |
| `nx/libc.h` | `NXLIBC_VERSION`, `nxc_version`, `nxc_heap_stats_t nxc_heap_stats nxc_heap_check nxc_heap_trim`, `nxc_debugf nxc_vdebugf` |

The `nxsys_` prefix keeps clear of `desktop/include/nxsyscall.h`, whose
inline `nx_*` helpers predate nxlibc. Every mirrored structure, constant,
syscall number and errno value is compared with the kernel's own headers by
`tests/host/libc_abi_check.c` on every test run; it caught `net.if_list`
moving to 133 (PR #46) the first time it ran on current `main`.

Not in the beta: wide characters and multibyte conversion, `locale.h`,
`signal.h`, threads, `complex.h`, `fenv.h`, `tmpfile`/`tmpnam`, any POSIX
header. Adding headers later is compatible; removing or changing a name
above after the beta is not.

## 2. Link interface

`sdk/libc/nxlibc.mk` defines `NXLIBC_CFLAGS`, `NXLIBC_LDFLAGS`,
`NXLIBC_CRT0`, `NXLIBC_LIB`. Programs are static ELF, linked at 0x1000000 by
`crt/nxapp.ld` (the layout the kernel already loads). Entry: `_start` in
`crt0.o` takes argc/argv in RDI/RSI and envp inline after `argv[argc]`
(S3's spawn_ex layout), and calls `int main(int, char **, char **)`.

## 3. Decisions for the owner

Each has a recommended answer, which is what the code does today.

1. **errno numbers are NeolyxOS's own**, taken from the kernel
   (`ERANGE` is 40, not another system's 34). Library-only codes use
   numbers the kernel promised to keep free. *Recommended: yes.*
2. **stdout/stderr go to the kernel debug log** until the kernel sets
   `NX_ABI_FLAG_FILEIO`; then descriptors 1 and 2, falling back to the log
   if they are not open. `stdin` reads end of file until then.
   *Recommended: yes.*
3. **`%n` is refused** by the `printf` family (returns -1, `EINVAL`,
   logged). It is the format-string-attack primitive and nothing in the
   tree uses it. *Recommended: refuse.*
4. **Heap misuse ends the program**: double free, freeing a foreign
   pointer, or a damaged block header logs the address and calls `abort()`
   (exit status 134). *Recommended: yes; continuing would corrupt memory.*
5. **`realloc(p, 0)`** returns a minimal block and leaves `p` owned by the
   caller (never frees). ISO C leaves this implementation-defined.
   *Recommended: yes; it is the choice that cannot cause a double free.*
6. **`atoi`/`atol` clamp** out-of-range input to the type's limits instead
   of undefined behaviour. *Recommended: yes.*
7. **`time()` returns -1** when the kernel's clock is not set (today
   `rtc.unix` returns a negative number because `rtc_init` is never
   called; reported to S9). *Recommended: yes, as ISO C specifies.*
8. **Time is UTC only**; `localtime` equals `gmtime` until there is a time
   zone setting. `strftime` writes years with four digits (ISO 8601).
   *Recommended: yes for the beta.*

## 4. Testing

`make -C sdk/libc check-host` runs eight host suites against today's
kernel ABI and against the FILEIO ABI. In QEMU, `tests/userland/libctest`
prints `[LIBCTEST] RESULT: PASS`, `sdk/libc/examples/hello` (printf and
fopen) runs from `/apps`, and `tests/userland/ticker` is relinked on nxlibc.
Logs: `/mnt/project-files/beta-plan/S4/`.
