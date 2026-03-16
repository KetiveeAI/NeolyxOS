# Desktop Render Crash Analysis & Fix

## 1. The Issue
The desktop environment enters a **boot loop** immediately after printing `[DESKTOP] Rendering frame...`.
-   It does not reach the deeper rendering logic.
-   It crashes silently (no kernel panic, just process restart).

## 2. Root Cause Analysis
The crash is caused by **unhandled SSE/Floating-Point instructions** in userspace.

1.  **Code Triggers:** The `render_desktop` function in `desktop_shell.c` uses `float` variables (e.g., `float dt`).
2.  **Compiler Behavior:** The current `desktop/Makefile` uses `-O2` but **does not disable SSE** (`-mno-sse`). GCC generation targeting x86_64 defaults to using SSE registers (`xmm0`, etc.) for floating point math.
3.  **Kernel Deficiency:** Code analysis of `kernel/src/arch/x86_64/idt.c` and `kernel/kernel_main.c` reveals:
    *   The kernel **does not enable** FPU/SSE features (no `d_enable_sse` or `fninit`).
    *   The kernel **does not handle** `Exception 7 (#NM - Device Not Available)` or `Exception 19 (#XM - SIMD Exception)`.
    *   Cr0/Cr4 control registers are not configured for userspace FPU usage.

**Result:** When the desktop calculates `float dt`, the CPU executes an SSE instruction. Since the kernel hasn't enabled SSE/FPU, the CPU throws an exception (#NM or #UD). The kernel doesn't handle this exception, leading to a process crash. The init system then restarts the desktop, causing the loop.

## 3. Recommended Solution (Userspace-Only)
Since the kernel FPU support is complex to implement properly (requires context saving/restoring), the simplest fix is to **prevent the compiler from using SSE instructions** in the desktop environment.

### Action: Modify `desktop/Makefile`
Add the following flags to `CFLAGS` to force software floating-point emulation or prevent SSE usage.

**File:** `desktop/Makefile`

```diff
 CFLAGS = -Wall -Wextra -std=c11 -O2 \
          -ffreestanding -fno-stack-protector \
          -mno-red-zone \
+         -mno-sse -mno-mmx -msoft-float \
          $(INCLUDES)
```

**Why this works:**
-   `-mno-sse -mno-mmx`: Tells GCC not to generate SSE/MMX instructions.
-   `-msoft-float`: Tells GCC to use library calls for floating point math instead of hardware instructions (or integer equivalents if available).

## 4. Alternative Solution (Kernel-Level)
If hardware floating point performance is required later, the kernel must be updated to:
1.  Enable SSE in `CR0` and `CR4` registers during boot.
2.  Implement `FXSAVE` / `FXRSTOR` in the context switch routine to save userspace FPU state.
3.  Handle Exception #7 (`#NM`) to lazily load FPU state (optional) or ensure it's always enabled.
