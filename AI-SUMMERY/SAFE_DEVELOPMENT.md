# Safe Development Guide for NeolyxOS

## ⚠️ Fragile Systems - Handle With Care

### 1. Cursor System (VERY FRAGILE)
**Status:** Working but dual implementation causes conflicts

**What NOT to change:**
- `kernel/src/core/syscall.c` - `kernel_draw_cursor_on_fb()`
- `desktop/shell/desktop_shell.c` - `desktop_draw_cursor()`
- Mouse event handling in desktop
- Framebuffer flip timing

**If you MUST change cursor code:**
1. Read `CURSOR_SYSTEM.md` first
2. Test immediately after change
3. Check for duplicate cursors or missing cursor
4. Verify smooth mouse movement

### 2. Desktop Heap (CRITICAL)
**Status:** Fixed and working

**What NOT to change:**
- `kernel/kernel_main.c` - Heap initialization (line ~700)
- `kernel/src/core/syscall.c` - `sys_brk_impl()`
- `desktop/lib/nx_malloc.c` - Heap allocator

**If heap breaks:**
- Desktop will crash with GPF immediately
- Check serial log for `[BRK]` messages
- Verify `syscall_set_desktop_heap()` is called

### 3. Config System (DISABLED)
**Status:** Causes crash, temporarily disabled

**What's disabled:**
- `nx_config_init()` call in `desktop_shell.c` (line ~1840)

**To re-enable:**
1. Uncomment `nx_config_init()`
2. Test immediately
3. If GPF at 0x01014564, disable again
4. Debug `config/nx_config.c` properly first

### 4. Build System
**Status:** Working but requires specific order

**ALWAYS use:**
```bash
./build_all.sh    # Full rebuild (desktop + kernel)
```

**DON'T use alone:**
```bash
./build.sh        # Only builds kernel, NOT desktop
make              # Old Makefile, incomplete
```

**After desktop changes:**
```bash
cd desktop && make clean && cd ..
./build_all.sh
```

## 🔧 Safe Development Workflow

### Making Changes

1. **Before changing code:**
   - Check if it's in a fragile system (above)
   - Read relevant documentation
   - Backup working image: `cp neolyx.img neolyx.img.working`

2. **After changing code:**
   ```bash
   ./build_all.sh
   ./boot_test.sh
   # Watch for crashes in QEMU
   tail -f serial_debug.log  # In another terminal
   ```

3. **If something breaks:**
   - Check serial log for crash address
   - Revert changes: `git diff` then `git checkout -- <file>`
   - Restore working image: `cp neolyx.img.working neolyx.img`

### Testing Checklist

After ANY code change, verify:
- [ ] OS boots to desktop
- [ ] Cursor is visible and moves
- [ ] No duplicate cursors
- [ ] No crashes in serial log
- [ ] Desktop renders correctly

## 📝 Adding New Features

### Safe Areas to Modify

✅ **Desktop UI elements** (windows, dock, menubar)
✅ **Color schemes and themes**
✅ **New applications** (in `desktop/apps/`)
✅ **Documentation files**

### Risky Areas (Test Thoroughly)

⚠️ **Syscall handlers** - Can crash kernel
⚠️ **Memory management** - Can corrupt heap
⚠️ **Interrupt handlers** - Can freeze system
⚠️ **Paging code** - Can cause triple fault

### Dangerous Areas (Expert Only)

🔴 **GDT/IDT setup** - System won't boot
🔴 **Ring transitions** - GPF or triple fault
🔴 **Bootloader** - System won't start
🔴 **ELF loader** - Desktop won't load

## 🐛 Common Issues and Fixes

### Issue: Cursor disappeared
**Cause:** Changed cursor drawing code
**Fix:** Revert cursor changes, see `CURSOR_SYSTEM.md`

### Issue: Desktop crashes with GPF
**Cause:** Heap corruption or uninitialized memory
**Fix:** Check serial log for crash address, verify heap init

### Issue: Black screen after boot
**Cause:** Desktop binary not embedded or corrupted
**Fix:** Run `./build_all.sh` to regenerate desktop ELF

### Issue: Build succeeds but old code runs
**Cause:** Used `build.sh` instead of `build_all.sh`
**Fix:** Run `./build_all.sh` to rebuild everything

### Issue: Config system crash
**Cause:** `nx_config_init()` is enabled
**Fix:** Keep it disabled until properly debugged

## 📚 Documentation Files

- `CURSOR_SYSTEM.md` - Cursor implementation details
- `BUG_ANALYSIS.md` - Boot crash investigation
- `FIX_SUMMARY.md` - What was fixed and how
- `Architecture.md` - System architecture overview
- `PROJECT_STRUCTURE.md` - Codebase organization

## 🎯 Current Status

✅ **Working:**
- Boot sequence
- Desktop rendering
- Mouse cursor
- Framebuffer syscalls
- Ring 3 userspace

⚠️ **Fragile:**
- Cursor system (dual implementation)
- Build process (must use build_all.sh)

🔴 **Broken:**
- Config system (crashes, disabled)

## 💡 Best Practices

1. **Always test after changes** - Don't accumulate untested changes
2. **Use build_all.sh** - Ensures everything is rebuilt
3. **Check serial log** - Catches issues early
4. **Keep backups** - Save working images
5. **Document changes** - Update relevant .md files
6. **Small commits** - Easier to revert if needed

## 🆘 Emergency Recovery

If system is completely broken:

```bash
# Restore from backup
cp neolyx.img.bak neolyx.img

# Or rebuild from scratch
git status  # Check what changed
git diff    # Review changes
git checkout -- .  # Revert all (CAREFUL!)
./build_all.sh
```

## 📞 Getting Help

1. Check serial log: `tail -100 serial_debug.log`
2. Look for crash address (RIP=0x...)
3. Check which system crashed (kernel vs userspace)
4. Review recent changes
5. Consult documentation files
