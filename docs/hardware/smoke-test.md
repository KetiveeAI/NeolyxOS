# Hardware smoke test: booting NeolyxOS from a USB stick

This is the first test of NeolyxOS on a real PC. The goal is to learn what
the machine gives NeolyxOS and where boot stops, not to install anything.
Everything needed is a USB stick (any size from 256 MB, it will be erased),
the PC, and a phone camera.

## What the test image does

The image built by `scripts/make_usb_image.sh` contains
`\EFI\BOOT\diagnose.flg`. When that file is present, every boot from the
stick runs in **diagnostics mode**:

1. **The loader shows a diagnostics page for 20 seconds** before starting
   the kernel: firmware vendor and UEFI version, display mode, pixel format,
   framebuffer address, free memory below and above 4 GB, free space at
   1 MB (where the kernel is loaded), the ACPI table address, and where the
   kernel and desktop were loaded.
2. **The kernel shows its log on screen** while it boots. The log wraps to
   the top of the screen when it reaches the bottom; the newest line is the
   one just above the blank line.
3. **Before the desktop starts, the kernel shows a one-screen boot summary
   for 15 seconds**: what it found (PS/2, USB, disks, PCI) and every warning
   and error.
4. **If the kernel crashes**, it shows a red "KERNEL EXCEPTION" or "KERNEL
   PANIC" title and the last log lines, and stops.
5. **Disk writes are refused.** Every disk driver (AHCI, NVMe, ATA) refuses
   writes for the whole boot, and each refusal is logged. The PC's own
   disks, and other operating systems on them, cannot be changed by this
   boot. Nothing writes UEFI variables either.

Without `diagnose.flg` (the normal `neolyx.img`), none of this changes how
NeolyxOS boots, except that a kernel crash now shows the crash screen.

## Build the image

On the build machine, from the repository root:

```
./build_all.sh
./rebuild_image.sh
scripts/make_usb_image.sh
```

This writes `neolyx-usb.img` (128 MiB, GPT with one EFI System Partition)
and `neolyx-usb.img.sha256`. The script needs `sgdisk` (gdisk),
`mkfs.fat` (dosfstools) and mtools.

## Write it to a USB stick

Writing the image erases the stick. Check the device name twice: writing to
the wrong device erases that disk.

On Linux:

```
lsblk -o NAME,SIZE,MODEL,TRAN      # find the stick, e.g. sdb with TRAN=usb
sudo dd if=neolyx-usb.img of=/dev/sdX bs=4M conv=fsync status=progress
```

On Windows, use any raw image writer in "DD image" mode.

## Boot it

1. Plug the stick in and power on the PC.
2. Open the firmware boot menu (often F12, F11, F8 or Esc during power-on)
   and choose the USB stick's **UEFI** entry.
3. If the firmware refuses it with a security message, **Secure Boot** is
   on. NeolyxOS's loader is not signed yet. Turn Secure Boot off in the
   firmware setup for the test, and turn it back on afterwards.

## What to photograph

| When | What |
| --- | --- |
| Loader diagnostics page (20 s) | The whole page |
| Boot summary with a red title (15 s) | The whole screen |
| End state | The desktop or Setup screen, the crash screen, or whatever is on screen if nothing changes for a minute |

If the screen stays black or freezes, note how far it got: firmware logo,
loader page, kernel log, summary.

Also note the PC model, and whether the keyboard and mouse are USB or the
laptop's built-in ones.

## Expected limits on real hardware today

These are known and do not need a report, but a photo still helps:

- **No disk root.** The kernel looks for its boot volume on AHCI port 0
  only, and has no USB storage driver, so "ESP mount failed" is expected
  when booting from a USB stick. The desktop still starts, because the
  loader puts it in memory.
- **Input may not work.** NeolyxOS drives only PS/2 keyboards and mice.
  Laptop keyboards are usually PS/2 inside and work. USB keyboards work only
  if the firmware emulates PS/2 for them. On PCs with no PS/2 controller the
  summary says "No PS/2 controller".
- **The USB controller may be left alone.** If the firmware still owns the
  xHCI controller, if its registers are above 4 GB, or if it needs
  scratchpad buffers, the kernel does not touch it (the summary says why).
  This keeps firmware USB keyboard emulation working.
- **Colors may be swapped** if the loader page says the pixel format is RGB.
- **Shut down by holding the power button.** "Shut down" in NeolyxOS
  currently restarts the machine. With disk writes refused, nothing is lost.

## After the test

Remove the stick before the PC restarts, so it boots its normal operating
system. Send the photos with the PC model.
