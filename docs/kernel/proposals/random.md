# Proposal: kernel randomness, `random.get` (syscall 57)

Status: **proposal for the Beta ABI batch (decision D2), not implemented.**
Stream: S6 (secure web client). Registry: syscall **57**, ABI flag
**0x0200** (`/mnt/project-files/beta-plan/parallel-workstreams.md`, section 4).
Audited against `main` at 18512d2 (2026-11-01).

## Why

TLS, package signing and every key the OS makes need unpredictable bytes.
A TLS 1.3 handshake alone takes a 32-byte client random and a fresh X25519
private key. If an attacker can guess them, the connection's keys can be
recomputed and HTTPS protects nothing.

Today NeolyxOS has no such source:

| What | Where | Problem |
| --- | --- | --- |
| `net_random32()` | `kernel/src/net/network.c` | TSC mixed with a xorshift state. The comment says it is not cryptographic. Used for DHCP xids, DNS ids, TCP sequence numbers and ports. |
| Installer GUIDs | `kernel/src/core/installer.c` ("pseudo-random GUID") | Not a secret, but predictable. |
| RDRAND | `platform_detect.c` and `zen_init.c` only *detect* it | Nothing reads it. |
| Userspace | none | No syscall returns random bytes. Programs would have to roll their own from `time`, which is guessable. |

## Proposed ABI

### Syscall

```c
/* kernel/include/core/syscall_table.def, inside the S6 block */
/* --- S6: secure web client --- */
NX_SYSCALL(57, "random.get", sys_random_get_impl, 3, PRIV_USER, 0)
```

```c
int64_t random.get(void *buf, uint64_t len, uint32_t flags);
```

- Fills `buf[0..len)` with bytes from the kernel CSPRNG and returns `len`.
  A successful call always fills the whole buffer. There are no short
  reads, so a caller never sees a partly filled key.
- `len` is 1 to `NX_RANDOM_MAX` (4096). Larger requests loop in the caller.
  4096 is enough to seed any userspace generator many times over.
- `flags`:
  - `0`: if the generator is not seeded yet, wait until it is. The kernel
    seeds it during boot, before the first user process starts (see
    "Seeding"), so in practice this never waits.
  - `NX_RANDOM_NONBLOCK` (0x1): return `-EAGAIN` instead of waiting.
  - Any other bit gives `-EINVAL`, so flags can be added later.
- Errors (existing codes only, no new errno values):
  - `-EINVAL`: `len` is 0 or more than 4096, or an unknown flag is set.
  - `-EFAULT`: `buf` is not writable user memory for `len` bytes. The kernel
    checks it with `syscall_validate_ptr(buf, len, 1)` and writes nothing.
  - `-EAGAIN`: not seeded and `NX_RANDOM_NONBLOCK` is set.
- `PRIV_USER`, no `NX_FLAG_AUDIT`. The call is frequent and harmless.
  Seeding events and source failures are logged instead (see "Logging").

### Header `kernel/include/core/nx_random.h` (new, public)

```c
#define NX_ABI_FLAG_RANDOM   0x0200u   /* random.get (57) is available */
#define NX_RANDOM_MAX        4096u     /* largest len per call */
#define NX_RANDOM_NONBLOCK   0x1u      /* -EAGAIN instead of waiting for the seed */
```

Programs check `NX_ABI_FLAG_RANDOM` in `abi.query` before calling, as the plan
requires, and never compare the minor version. The header lives in `core/`
because `sdk/` and the userland tests include only `kernel/include/core`, the
same reason `nx_net.h` lives there.

### Kernel-internal API (not ABI)

```c
/* kernel/include/security/random.h */
int  random_get_bytes(void *buf, size_t len);   /* 0 or -EAGAIN before seeding */
void random_add_event(uint32_t source, uint64_t value);  /* cheap, IRQ-safe */
int  random_is_seeded(void);
```

Other kernel code that needs secrets should call `random_get_bytes()`.
The networking stream owns `net_random32()`. Once this lands I will ask that
thread to switch TCP initial sequence numbers and DNS ids to it. That is a
change in their files, made by them.

## Design

The generator is built only from primitives in `lib/nxcrypto`, which pass the
published test vectors on the host before the kernel uses them. The kernel
compiles those source files with its own flags, through an append-only edit
to `kernel/Makefile`.

### Entropy sources

| Source | How it is read | Credit toward the 256-bit seed |
| --- | --- | --- |
| RDSEED | `CPUID.(EAX=7,ECX=0):EBX[18]`; up to 10 retries while `CF=0` | 64 bits per good read |
| RDRAND | `CPUID.01H:ECX[30]`; up to 10 retries while `CF=0` (Intel's guidance) | 32 bits per good read (half, because it is itself a DRBG output) |
| CPU timing jitter | TSC deltas around a fixed memory-walk loop, 64 samples per byte | 1 bit per 64 samples, a deliberately low estimate |
| Device events (later) | `random_add_event()` from interrupt paths: e1000 RX, keyboard, mouse, AHCI completion | 0. They are mixed in but never counted |

Every value is mixed in, whatever its credit. Credit only decides when the
generator counts as seeded. A machine without RDRAND or RDSEED, such as QEMU
TCG with the default CPU model, still seeds from jitter, just more slowly
(the target is under 200 ms in the VM; I will measure it and record the time
in the PR).

**Health checks**, before a value is mixed or credited:

- RDRAND or RDSEED returns `CF=0` 10 times in a row: that source is disabled
  for this boot and the event is logged.
- The same 64-bit value twice in a row, or all ones (a known AMD firmware
  fault): the source is disabled and the event is logged.
- Jitter where the deltas show fewer than 2 distinct values per 64 samples:
  that block gets no credit.

### Pool and output

- **Pool**: a SHA-512 context. Every input is absorbed as
  `source id || length || value`.
- **Seed**: when credited entropy reaches 256 bits, `key = SHA-256(pool
  digest || "NX-RANDOM-SEED" || counter)` keys the output generator.
- **Output**: ChaCha20 (RFC 8439) keystream under that key, with *fast key
  erasure*. Each request first draws 32 bytes that replace the key, then
  produces the caller's bytes. Leaking the kernel's state later therefore
  reveals nothing about earlier output.
- **Reseed**: every 60 seconds, or every 1 MiB of output, whichever is first,
  new pool output is mixed into the key (new key = SHA-256(old key || pool
  digest)). Fresh RDSEED or RDRAND values are added at each reseed.
- **Concurrency**: one CPU, and the kernel is not preemptible
  (`kernel-shm-ipc.md`), so syscalls never interleave. `random_add_event()`
  runs from interrupt handlers and only appends to a small ring that
  is mixed at the next request, so the pool is never touched from an IRQ.
  When SMP arrives, a spinlock around the generator is enough.
- **Memory hygiene**: key, ChaCha20 state and temporaries are wiped with
  `nxc_memzero()` after use. Output goes straight into the user buffer
  through the existing user-copy path. No secret stays on the kernel stack.

### Seeding during boot

`random_init()` runs once, after `platform_detect()` and the PIT are up and
before the first user process is spawned. It mixes the TSC, the boot info
memory map and the RTC time (all uncredited), reads RDSEED and RDRAND, and
collects jitter until 256 bits are credited. If this takes more than 2
seconds, it logs a warning and keeps collecting. User processes still start;
calls with flags = 0 wait, and calls with `NX_RANDOM_NONBLOCK` return
`-EAGAIN`. The system never proceeds as "seeded" when it is not.

A firmware source (`EFI_RNG_PROTOCOL` read by the boot loader and passed in
boot info) would add a strong early seed, but it changes the boot flow and
the boot info struct. It is left to the platform stream (S9), after the beta.

### Logging (serial, `[RANDOM]` prefix)

- Once at seeding: sources found, bits credited per source, time taken.
- Every source failure from the health checks.
- Every reseed at debug level only, since it is frequent and not critical.

No random output is ever logged.

## Tests

- **Host** (`tests/host/crypto_random_test.c`): the generator logic runs
  outside the kernel with fake sources. It checks fast key erasure (output
  before a state capture cannot be recomputed from that state), reseed
  triggers, health-check rejection (a stuck source, all ones, `CF=0`), and
  that output never repeats over 64 MiB. ChaCha20 and SHA-512 themselves are
  covered by their RFC 8439 and NIST vectors.
- **QEMU** (`tests/userland/randtest`): `abi.query` reports the flag. Each
  invalid argument gets its documented error (`len` 0 and 4097, an unknown
  flag, an unmapped buffer, a read-only buffer). Two 4096-byte reads differ.
  A 1 MiB sample passes simple frequency and runs checks (sanity only, not
  proof of randomness). Runs once with `-cpu max` (RDRAND and RDSEED) and once
  with the default TCG CPU (jitter only). Each run prints `[RANDTEST] RESULT:
  PASS`. Logs go to `/mnt/project-files/beta-plan/S6/`.

## Compatibility

- Number 57 has never been declared or registered (`syscall.h`,
  `syscall_table.def`), so nothing changes meaning.
- New header, new flag bit, no change to any existing struct or call.
- The minor version bump is part of the batch (1.5), made by S10.

## What I am asking the owner to approve

1. Syscall 57 `random.get(buf, len, flags)` with the semantics above.
2. `nx_random.h` with `NX_ABI_FLAG_RANDOM` 0x0200, `NX_RANDOM_MAX` 4096, and
   `NX_RANDOM_NONBLOCK`.
3. The kernel compiling `lib/nxcrypto` sources (SHA-2 and ChaCha20) into
   the kernel image, through an append-only edit to `kernel/Makefile`.
