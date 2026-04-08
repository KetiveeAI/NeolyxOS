# NeolyxOS Cursor System

## Current Implementation (Working but Fragile)

The cursor system currently has TWO implementations that can conflict:

### 1. Kernel Cursor Compositor
**Location:** `kernel/src/core/syscall.c` (line ~857)
**Function:** `kernel_draw_cursor_on_fb()`
**When:** Called after every `sys_fb_flip` syscall
**Position:** Stored in kernel globals `g_cursor_x`, `g_cursor_y`
**Update:** Via `sys_cursor_set` syscall (not currently used by desktop)

### 2. Desktop Cursor
**Location:** `desktop/shell/desktop_shell.c` (line ~481)
**Function:** `desktop_draw_cursor()`
**When:** Called by desktop during rendering
**Position:** Tracked in desktop's mouse event handler
**Update:** Directly from mouse input events

## Why It's Fragile

Any code changes can break the cursor because:

1. **Double Drawing**: Both kernel and desktop draw cursors
2. **No Synchronization**: Positions can be out of sync
3. **Timing Issues**: Kernel cursor drawn after desktop's backbuffer flip
4. **Memory Access**: Both access framebuffer, potential race conditions

## Current Working State

The cursor works because:
- Desktop draws cursor in its rendering loop
- Kernel cursor compositor is active but may be drawing at same position
- No conflicts happen to be occurring with current timing

## Recommended Fixes (Choose One)

### Option A: Use Kernel Cursor Only (Recommended)
**Pros:** Hardware-like cursor isolation, no userspace overhead
**Cons:** Requires syscall for every mouse move

**Changes needed:**
1. Add `SYS_CURSOR_SET` to `desktop/include/nxsyscall.h`
2. Call `nx_cursor_set(x, y, 1)` in desktop's mouse handler
3. Remove `desktop_draw_cursor()` calls from desktop code

```c
// In nxsyscall.h
#define SYS_CURSOR_SET 125

static inline int nx_cursor_set(int x, int y, int visible) {
    return (int)syscall3(SYS_CURSOR_SET, x, y, visible);
}

// In desktop mouse handler
nx_cursor_set(mouse_x, mouse_y, 1);
// Remove: desktop_draw_cursor(mouse_x, mouse_y);
```

### Option B: Disable Kernel Cursor
**Pros:** Simpler, desktop has full control
**Cons:** Cursor can be overwritten by desktop rendering bugs

**Changes needed:**
1. Set `g_cursor_visible = 0` in kernel init
2. Or comment out `kernel_draw_cursor_on_fb()` call in `sys_fb_flip_impl`

```c
// In syscall.c sys_fb_flip_impl
/* ========== KERNEL CURSOR COMPOSITOR ========== */
// kernel_draw_cursor_on_fb();  // DISABLED - desktop handles cursor
/* ============================================== */
```

### Option C: Hybrid (Current State)
**Pros:** Works now
**Cons:** Fragile, can break with any changes

**Keep as-is but:**
- Document the fragility
- Be careful with any cursor-related changes
- Test cursor after every code change

## Testing Cursor Changes

After any modification:

```bash
./build_all.sh
./boot_test.sh
# Move mouse and verify:
# - Cursor is visible
# - Cursor moves smoothly
# - No duplicate cursors
# - No cursor trails/artifacts
```

## Current Status

✅ Cursor is working
⚠️ System is fragile - changes can break it
📝 Documented for future reference

## Recommendation

Implement **Option A** (kernel cursor only) for stability:
1. It's the cleanest architecture
2. Matches how real OSes work (hardware cursor)
3. Eliminates race conditions
4. More robust to code changes

But for now, since it's working, we can leave it as-is and fix it properly later when we have more time to test.
