# NeolyxOS Bug Fix Documentation: GPF During Desktop Rendering

**Date:** January 8, 2026  
**Severity:** Critical (System Crash)  
**Component:** Kernel - GDT/SYSRET  
**Status:** RESOLVED

---

## Summary

The NeolyxOS desktop crashed with a General Protection Fault (GPF) after rendering 1-2 frames. The crash occurred during interrupt return (`iretq`) when the CPU attempted to load an invalid code segment selector.

## Symptoms

- Desktop renders initial frame, then crashes
- GPF exception with error code `0x28`
- Faulting instruction: `iretq` at RIP `0x110A1A`
- Serial log shows interrupt firing with `CS=0x2B` (invalid)

## Root Cause

The SYSRET instruction was loading an invalid CS selector pointing to the TSS descriptor instead of user code.

### Technical Details

SYSRET in 64-bit mode calculates:
```
CS = (IA32_STAR[63:48] + 16) | 3
SS = (IA32_STAR[63:48] + 8) | 3
```

With the original GDT layout:
```
Entry 0: Null (0x00)
Entry 1: Kernel Code (0x08)
Entry 2: Kernel Data (0x10)
Entry 3: User Code (0x18)      ← SYSRET expected this at 0x28!
Entry 4: User Data (0x20)
Entry 5-6: TSS (0x28)          ← SYSRET was targeting this!
```

With `STAR[63:48] = 0x18`:
- CS = (0x18 + 16) | 3 = 0x28 | 3 = **0x2B** (TSS!)
- SS = (0x18 + 8) | 3 = 0x23 (correct)

## Solution

Reorganized the GDT to place User Code 64-bit at the offset SYSRET expects:

```
Entry 0: Null (0x00)
Entry 1: Kernel Code (0x08)
Entry 2: Kernel Data (0x10)
Entry 3: User Code 32-bit placeholder (0x18)
Entry 4: User Data (0x20)
Entry 5: User Code 64-bit (0x28)    ← Now SYSRET finds valid code
Entry 6-7: TSS (0x30)               ← Moved
```

Now with `STAR[63:48] = 0x18`:
- CS = (0x18 + 16) | 3 = 0x28 | 3 = **0x2B** (valid User Code 64)
- SS = (0x18 + 8) | 3 = 0x23 (User Data)

## Files Modified

| File | Change |
|------|--------|
| `kernel/src/arch/x86_64/gdt.c` | GDT layout: User Code 64 at entry 5, TSS at entries 6-7, GDT_ENTRIES=8 |
| `kernel/src/arch/x86_64/arch.S` | User CS selector: `0x1B → 0x2B` in `enter_user_mode` |
| `kernel/src/core/context.S` | User CS selector: `0x1B → 0x2B` in `enter_usermode` |

## Verification

After fix, desktop runs stably with continuous frame rendering:
```
[DESKTOP] Frame
[FLIP] called
[DESKTOP] Initial render complete
[FLIP] called
[FLIP] called   ← No crash, stable operation
```

## Lessons Learned

1. **SYSRET segment arithmetic matters**: The GDT layout must account for SYSRET's fixed offsets (+16 for CS, +8 for SS)
2. **ISR debug tracing is essential**: Adding `[ISR] Vector X CS=Y` logging immediately revealed the wrong CS value
3. **x86 segment selectors**: The low 2 bits are RPL (Ring Privilege Level), so 0x28 | 3 = 0x2B

## References

- Intel SDM Vol. 2B: SYSRET instruction
- AMD64 Architecture Programmer's Manual Vol. 2: System Programming

---

*For future OS developers: Always verify your GDT layout works with SYSCALL/SYSRET before enabling userspace. The segment math is not intuitive.*
