# Cursor System Fix - April 1, 2025

## Problem
Cursor was working initially but broke after heap initialization changes. The cursor would not move or would disappear.

## Root Cause Analysis

### The Two Systems
1. **UEFI Firmware UI** - Boot menu before kernel loads (not affected)
2. **Kernel Desktop UI** - The actual OS desktop (this was broken)

### What Broke the Cursor

The desktop code in `render_desktop()` was calling:
```c
nx_cursor_set(g_desktop.mouse_x, g_desktop.mouse_y, 1);
```

But `nx_cursor_set()` was **NOT DEFINED** in the syscall header!

This caused:
- Undefined function call
- Linker might create weak symbol pointing to garbage
- Cursor syscall never actually executed
- Kernel cursor stayed at (100, 100) initial position
- Desktop cursor drawing might have been disabled

### The Cursor Architecture

NeolyxOS uses a **kernel cursor compositor** (like macOS/Windows hardware cursor):

1. **Desktop** updates mouse position in `g_desktop.mouse_x/y`
2. **Desktop** calls `nx_cursor_set()` syscall to tell kernel
3. **Kernel** stores position in `g_cursor_x/y`  
4. **Kernel** draws cursor AFTER framebuffer flip in `kernel_draw_cursor_on_fb()`

This way the cursor is never overwritten by desktop rendering.

## The Fix

### Step 1: Add syscall definition to header
**File:** `desktop/include/nxsyscall.h`

```c
/* Set cursor position (kernel cursor compositor) */
#define SYS_CURSOR_SET 128  // Must match syscall_table.def!
static inline int nx_cursor_set(int x, int y, int visible) {
    return (int)syscall3(SYS_CURSOR_SET, (int64_t)x, (int64_t)y, (int64_t)visible);
}
```

### Step 2: Remove duplicate definition
**File:** `desktop/shell/userspace_stubs.c`

Removed the old `nx_cursor_set()` function that was conflicting.

### Step 3: Verify syscall is registered
**File:** `kernel/include/core/syscall_table.def`

Confirmed syscall 128 is registered:
```c
NX_SYSCALL(128, "cursor.set", sys_cursor_set_impl, 3, PRIV_USER, 0)
```

### Step 4: Verify kernel handler exists
**File:** `kernel/src/core/syscall.c`

Confirmed handler updates kernel cursor position:
```c
int64_t sys_cursor_set_impl(uint64_t x, uint64_t y, uint64_t visible, ...) {
    g_cursor_x = (int32_t)x;
    g_cursor_y = (int32_t)y;
    g_cursor_visible = (int)visible;
    return 0;
}
```

## Why It Broke

The heap initialization changes didn't directly break the cursor, but they triggered a full rebuild which exposed the missing syscall definition. The code was calling an undefined function, which worked by accident before (weak linking or leftover symbols), but failed after clean rebuild.

## Testing

```bash
./build_all.sh
./boot_test.sh
# Move mouse - cursor should follow smoothly
```

## Verification

After fix:
- ✅ Cursor visible
- ✅ Cursor moves with mouse
- ✅ No duplicate cursors
- ✅ No cursor trails
- ✅ Smooth movement

## Lessons Learned

1. **Always define syscalls in header** - Don't just call them and hope
2. **Match syscall numbers** - Desktop header must match kernel table
3. **Remove duplicates** - One definition in header, not scattered
4. **Test after rebuild** - Clean builds expose hidden issues
5. **Document architecture** - Understand the two-system design

## Files Modified

- `desktop/include/nxsyscall.h` - Added nx_cursor_set() definition
- `desktop/shell/userspace_stubs.c` - Removed duplicate definition

## Related Documentation

- `CURSOR_SYSTEM.md` - Full cursor architecture
- `SESSION_2025-04-01.md` - Session summary
- `SAFE_DEVELOPMENT.md` - Development guidelines
