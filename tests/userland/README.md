# NeolyxOS Userland Programs

This directory contains user-space programs that run on NeolyxOS.

## Directory Structure

```
userland/
├── hello/          # Hello world example
│   ├── hello.c     # Source code
│   ├── Makefile    # Build script
│   └── linker.ld   # Linker script
│
├── libnx/          # NeolyxOS C library (future)
│   └── ...
│
└── bin/            # Compiled binaries (future)
    └── ...
```

## Building Programs

```bash
cd hello
make
```

## How It Works

User programs:
1. Start at `_start` (entry point at 0x400000)
2. Use syscalls to interact with the kernel
3. Call `exit()` to terminate cleanly

## Syscall Interface

Programs use the `syscall` instruction with:
- `rax` = syscall number
- `rdi` = arg1
- `rsi` = arg2  
- `rdx` = arg3
- `r10` = arg4
- `r8`  = arg5

Return value comes back in `rax`.

## Available Syscalls

| Number | Name | Description |
|--------|------|-------------|
| 1 | `sys_exit` | Exit process |
| 2 | `sys_sleep` | Sleep for milliseconds |
| 3 | `sys_time` | Get system time |
| 4 | `sys_yield` | Yield to scheduler |
| 5 | `sys_getpid` | Get process ID |
| 6 | `sys_getppid` | Get parent process ID |
| 7 | `sys_fork` | Fork process |
| 8 | `sys_exec` | Execute program |
| 9 | `sys_wait` | Wait for child |
| 20 | `sys_open` | Open file |
| 21 | `sys_close` | Close file |
| 22 | `sys_read` | Read from file |
| 24 | `sys_write` | Write to file |
| 25 | `sys_seek` | Seek in file |
| 26 | `sys_stat` | Get file info |
| 30 | `sys_readdir` | Read directory |
| 31 | `sys_mkdir` | Create directory |
| 32 | `sys_rmdir` | Remove file/directory |
| 33 | `sys_chdir` | Change directory |
| 34 | `sys_getcwd` | Get current directory |

## Running Programs

User programs can be loaded via:
```c
// From kernel or parent process
process_exec("/bin/hello", argv, envp);
```

Or from shell (when implemented):
```
$ /bin/hello
Hello from NeolyxOS userspace!
```

## Memory Layout

```
0x400000 - 0x4FFFFF   .text (code)
0x500000 - 0x5FFFFF   .rodata (constants)
0x600000 - 0x6FFFFF   .data (initialized data)
0x700000 - 0x7FFFFF   .bss (zero-initialized)
0x7FFFFF - down       Stack (grows down)
```

---

Copyright (c) 2025 KetiveeAI
