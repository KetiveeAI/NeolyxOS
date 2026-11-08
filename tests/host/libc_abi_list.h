/*
 * nxlibc ABI cross-check list: every <nx/sys.h> layout and constant next
 * to the kernel's own definition. Included by libc_abi_kernel.c (built
 * against kernel/include) and libc_abi_check.c (built against nxlibc's
 * headers); each defines the macros to take its own column.
 *
 *   S(nxlibc type, kernel type)                         sizeof
 *   O(nxlibc type, kernel type, nxlibc field, kernel field)   offsetof
 *   V(nxlibc constant, kernel constant)                 value
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

/* abi.query */
S(nxsys_abi_info_t, nx_abi_info_t)
O(nxsys_abi_info_t, nx_abi_info_t, flags, flags)
O(nxsys_abi_info_t, nx_abi_info_t, syscall_count, syscall_count)
V(NXSYS_ABI_FLAG_SHM, NX_ABI_FLAG_SHM)
V(NXSYS_ABI_FLAG_IPC, NX_ABI_FLAG_IPC)
V(NXSYS_ABI_FLAG_FB_RECTS, NX_ABI_FLAG_FB_RECTS)
V(NXSYS_ABI_FLAG_MEMCLASS, NX_ABI_FLAG_MEMCLASS)
V(NXSYS_ABI_FLAG_NET, NX_ABI_FLAG_NET)

/* processes */
V(NXSYS_SPAWN_WAITABLE, SPAWN_WAITABLE)
V(NXSYS_ARGV_MAX, PROCESS_ARGV_MAX)
V(NXSYS_ARG_LEN, PROCESS_ARG_LEN)

/* files */
V(NXSYS_O_RDONLY, VFS_O_RDONLY)
V(NXSYS_O_WRONLY, VFS_O_WRONLY)
V(NXSYS_O_RDWR, VFS_O_RDWR)
V(NXSYS_O_CREATE, VFS_O_CREATE)
V(NXSYS_O_TRUNC, VFS_O_TRUNC)
V(NXSYS_O_APPEND, VFS_O_APPEND)
V(NXSYS_SEEK_SET, VFS_SEEK_SET)
V(NXSYS_SEEK_CUR, VFS_SEEK_CUR)
V(NXSYS_SEEK_END, VFS_SEEK_END)
V(NXSYS_NAME_MAX, VFS_NAME_MAX)
V(NXSYS_DT_FILE, VFS_FILE)
V(NXSYS_DT_DIR, VFS_DIRECTORY)
V(NXSYS_DT_SYMLINK, VFS_SYMLINK)
V(NXSYS_DT_DEVICE, VFS_DEVICE)
V(NXSYS_DT_PIPE, VFS_PIPE)
V(NXSYS_DT_SOCKET, VFS_SOCKET)
S(nxsys_stat_t, stat_t)
O(nxsys_stat_t, stat_t, st_mode, st_mode)
O(nxsys_stat_t, stat_t, st_size, st_size)
O(nxsys_stat_t, stat_t, st_mtime, st_mtime)
O(nxsys_stat_t, stat_t, st_ctime, st_ctime)
S(nxsys_dirent_t, vfs_dirent_t)
O(nxsys_dirent_t, vfs_dirent_t, type, type)
O(nxsys_dirent_t, vfs_dirent_t, inode, inode)
O(nxsys_dirent_t, vfs_dirent_t, size, size)

/* sockets */
S(nxsys_sockaddr_in_t, sockaddr_in_t)
O(nxsys_sockaddr_in_t, sockaddr_in_t, port, port)
O(nxsys_sockaddr_in_t, sockaddr_in_t, addr, addr)
O(nxsys_sockaddr_in_t, sockaddr_in_t, zero, zero)
V(NXSYS_AF_INET, AF_INET)
V(NXSYS_SOCK_STREAM, SOCK_STREAM)
V(NXSYS_SOCK_DGRAM, SOCK_DGRAM)
V(NXSYS_SOCK_RAW, SOCK_RAW)

/* system information */
S(nxsys_sysinfo_t, sysinfo_t)
O(nxsys_sysinfo_t, sysinfo_t, uptime_ms, uptime_ms)
O(nxsys_sysinfo_t, sysinfo_t, num_processes, num_processes)

/* framebuffer and input */
S(nxsys_fb_info_t, fb_info_t)
O(nxsys_fb_info_t, fb_info_t, pitch, pitch)
O(nxsys_fb_info_t, fb_info_t, size, size)
S(nxsys_input_event_t, input_event_t)
O(nxsys_input_event_t, input_event_t, u.keyboard.ascii, keyboard.ascii)
O(nxsys_input_event_t, input_event_t, u.mouse.dy, mouse.dy)
O(nxsys_input_event_t, input_event_t, u.mouse.wheel, mouse.wheel)
V(NXSYS_INPUT_KEYBOARD, INPUT_TYPE_KEYBOARD)
V(NXSYS_INPUT_MOUSE_MOVE, INPUT_TYPE_MOUSE_MOVE)
V(NXSYS_INPUT_MOUSE_BUTTON, INPUT_TYPE_MOUSE_BUTTON)
S(nxsys_fb_rect_t, nx_fb_rect_t)
O(nxsys_fb_rect_t, nx_fb_rect_t, w, w)
V(NXSYS_FB_RECTS_MAX, NX_FB_RECTS_MAX)

/* real-time clock */
S(nxsys_rtc_time_t, rtc_time_t)
O(nxsys_rtc_time_t, rtc_time_t, month, month)
O(nxsys_rtc_time_t, rtc_time_t, weekday, weekday)

/* handles, shared memory, channels */
S(nxsys_handle_t, nx_handle_t)
S(nxsys_chan_msg_t, nx_chan_msg_t)
O(nxsys_chan_msg_t, nx_chan_msg_t, data_len, data_len)
O(nxsys_chan_msg_t, nx_chan_msg_t, sender_pid, sender_pid)
O(nxsys_chan_msg_t, nx_chan_msg_t, flags, flags)
S(nxsys_wait_item_t, nx_wait_item_t)
O(nxsys_wait_item_t, nx_wait_item_t, observed, observed)
V(NXSYS_RIGHT_READ, NX_RIGHT_READ)
V(NXSYS_RIGHT_WRITE, NX_RIGHT_WRITE)
V(NXSYS_RIGHT_MAP, NX_RIGHT_MAP)
V(NXSYS_RIGHT_TRANSFER, NX_RIGHT_TRANSFER)
V(NXSYS_RIGHT_DUPLICATE, NX_RIGHT_DUPLICATE)
V(NXSYS_RIGHTS_SAME, NX_RIGHTS_SAME)
V(NXSYS_SHM_PROT_READ, NX_SHM_PROT_READ)
V(NXSYS_SHM_PROT_WRITE, NX_SHM_PROT_WRITE)
V(NXSYS_SIGNAL_READABLE, NX_SIGNAL_READABLE)
V(NXSYS_SIGNAL_WRITABLE, NX_SIGNAL_WRITABLE)
V(NXSYS_SIGNAL_PEER_CLOSED, NX_SIGNAL_PEER_CLOSED)
V(NXSYS_WAIT_FOREVER, NX_WAIT_FOREVER)

/* memory classes */
V(NXSYS_CLASS_FOREGROUND, NX_CLASS_FOREGROUND)
V(NXSYS_CLASS_BACKGROUND, NX_CLASS_BACKGROUND)
V(NXSYS_MEM_NORMAL, NX_MEM_NORMAL)
V(NXSYS_MEM_WARN, NX_MEM_WARN)
V(NXSYS_MEM_CRITICAL, NX_MEM_CRITICAL)
V(NXSYS_EXIT_RECLAIMED, NX_EXIT_RECLAIMED)
S(nxsys_mem_stats_t, nx_mem_stats_t)
O(nxsys_mem_stats_t, nx_mem_stats_t, total_bytes, total_bytes)
O(nxsys_mem_stats_t, nx_mem_stats_t, self_bytes, self_bytes)

/* network status */
S(nxsys_net_if_info_t, nx_net_if_info_t)
O(nxsys_net_if_info_t, nx_net_if_info_t, mac, mac)
O(nxsys_net_if_info_t, nx_net_if_info_t, speed_mbps, speed_mbps)
O(nxsys_net_if_info_t, nx_net_if_info_t, dns, dns)
O(nxsys_net_if_info_t, nx_net_if_info_t, lease_seconds, lease_seconds)
O(nxsys_net_if_info_t, nx_net_if_info_t, tx_errors, tx_errors)
V(NXSYS_NET_IF_MAX, NX_NET_IF_MAX)
V(NXSYS_NET_DNS_MAX, NX_NET_DNS_MAX)

/* errno values the kernel returns (<errno.h> against core/syscall.h) */
V(EPERM, EPERM)
V(ENOENT, ENOENT)
V(ESRCH, ESRCH)
V(EINTR, EINTR)
V(EIO, EIO)
V(ENXIO, ENXIO)
V(E2BIG, E2BIG)
V(ENOEXEC, ENOEXEC)
V(EBADF, EBADF)
V(ECHILD, ECHILD)
V(EAGAIN, EAGAIN)
V(ENOMEM, ENOMEM)
V(EACCES, EACCES)
V(EFAULT, EFAULT)
V(EBUSY, EBUSY)
V(EEXIST, EEXIST)
V(EXDEV, EXDEV)
V(ENODEV, ENODEV)
V(ENOTDIR, ENOTDIR)
V(EISDIR, EISDIR)
V(EINVAL, EINVAL)
V(ENFILE, ENFILE)
V(EMFILE, EMFILE)
V(ENOTTY, ENOTTY)
V(EFBIG, EFBIG)
V(ENOSPC, ENOSPC)
V(ESPIPE, ESPIPE)
V(EROFS, EROFS)
V(EPIPE, EPIPE)
V(ENAMETOOLONG, ENAMETOOLONG)
V(ENOSYS, ENOSYS)
V(ENOTEMPTY, ENOTEMPTY)
V(ERANGE, ERANGE)
V(EMSGSIZE, EMSGSIZE)
V(ETIMEDOUT, ETIMEDOUT)
