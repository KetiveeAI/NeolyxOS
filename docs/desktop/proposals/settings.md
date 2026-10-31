# Proposal: kernel interfaces needed by System Settings

Area: Settings (`desktop/shell/views/settings_*.c`)
Status: proposal only. No syscall numbers, kernel ABI, boot flow or memory layout were changed.

System Settings shows live values wherever the kernel already provides them:

| Value | Source today |
|-------|--------------|
| Display resolution and color depth | `SYS_FB_INFO` (121) |
| Uptime | `SYS_TIME` (3) |
| Date and time | `SYS_RTC_GET` (130) |
| Total and used memory | `SYS_INFO` (50), `sysinfo_t.total_memory` / `free_memory` |
| Processor and hypervisor | `CPUID` (unprivileged instruction, read in userspace) |

The panes below show an "unavailable" state on real hardware because no interface exists. Each client already returns `SETTINGS_ERR_UNSUPPORTED` (-38). When an interface lands, only `settings_sysinfo.c` (or the named adapter) changes.

## 1. Volume statistics (Storage pane)

Client: `settings_sys_volume()` in `settings_sysinfo.c`.

Proposed call: `int sys_statvol(const char *mount_path, nx_volinfo_t *out)`.

```c
typedef struct {
    char     label[32];       /* volume label, NUL-terminated */
    char     fs_type[16];     /* "NXFS", ... */
    uint64_t total_bytes;
    uint64_t free_bytes;
    uint32_t block_size;
    uint32_t flags;           /* read-only, removable, encrypted */
} nx_volinfo_t;
```

- `PRIV_USER`. The kernel validates the path and the output pointer, as `sys_getcwd_impl` does.
- Errors: `-EFAULT` (bad pointer), `-ENOENT` (no mount at that path).

## 2. Audio output level (Sound pane)

Today the Sound pane changes the shell's volume level, which Control Center also uses. No device receives it.

Proposed calls: `sys_audio_info(nx_audio_info_t *out)`, which reports the device name, whether a device is present, and the min/max level, and `sys_audio_set_level(uint32_t percent, uint32_t muted)`.

`SYS_NXGAME_AUDIO` (117) is scoped to game sessions and is not a system mixer, so Settings does not use it.

## 3. Display backlight (Display pane)

`settings_drivers.h` from the older Settings.app declares `laptop_set_brightness()`, but no syscall backs it. Proposed: `sys_backlight_get(int display)` and `sys_backlight_set(int display, uint32_t percent)`. These return `-ENODEV` for external displays that have no DDC/CI support.

## 4. Input configuration (Keyboard, Mouse panes)

Key repeat rate and delay, pointer tracking speed, double-click interval and primary button are saved in the user `.nlc` config. The input driver currently accepts no configuration. Proposed: `sys_input_configure(uint32_t key, int32_t value)` with an enumerated key set. Each value is range-checked in the kernel and audited as a configuration change.

## Not kernel work, but needed by Settings

These belong to other desktop areas or to libraries. They are listed so the lead can route them.

- `lib/nxconfig.c`: `nxc_config_load()` returns built-in defaults, and `nxc_config_save()` does not write to disk (VFS TODO). Settings writes every preference through it, so preferences persist once the VFS write lands. No change to Settings is needed. The handle pool has 4 slots, and `anveshan_load_settings()` / `anveshan_save_settings()` take a new slot on every call, which can exhaust the pool. Settings loads its handle once and keeps it.
- NXUI has no accent-color setter. The Appearance pane saves `appearance.accent` and explains that the system accent stays Saffron.
- Dock: `dock_tick()` does not implement auto-hide, and `dock_render()` lays out only along the bottom edge. Settings stores both preferences through `settings_dock_adapter.c`.
- `control_center.c` opens Settings with `desktop_launch_app("Settings")`. `app_view_find()` does not match that name, so the launch does nothing when Settings is closed. The fix is to launch `"System Settings"` or call `settings_view_show_pane()`. While Settings is open, it already polls `control_center_get_pending_panel()` and switches to the requested pane.
