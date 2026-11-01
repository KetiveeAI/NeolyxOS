/*
 * NeolyxOS Shared Memory Objects
 *
 * Syscalls: shm.create (141), shm.map (142), shm.unmap (143).
 * Spec: docs/desktop/proposals/kernel-shm-ipc.md (approved 2026-11-01).
 *
 * An shm object is a fixed number of 4 KB frames, physically scattered and
 * all below 4 GB (the kernel reaches them through its identity map). Every
 * frame is zeroed here: pmm_alloc_page() does not clear pages, and a frame
 * may still hold another process's data. Its size never changes, so a
 * process that maps someone else's buffer never faults on it. Frames return
 * to the PMM when the last handle and the last mapping are gone.
 *
 * The frame list lives in whole PMM pages (512 entries each, at most 32
 * pages for 64 MiB), not in the kernel heap, so every byte an object used
 * goes back to the PMM when it is freed.
 *
 * Mappings go in the per-process shared region (AS_SHARED_BASE..END).
 * Each mapping reserves a 2 MiB-aligned span with at least one unmapped
 * page after it; spans are reused after shm.unmap.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_handle.h"
#include "core/process.h"
#include "core/syscall.h"
#include "mm/addrspace.h"
#include "mm/kheap.h"
#include "mm/pmm.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);

#define SHM_PAGE            4096ULL
#define SHM_SPAN_ALIGN      (2ULL << 20)            /* 2 MiB */
#define SHM_SYSTEM_FLOOR    (64ULL << 20)           /* keep this much RAM free */
#define SHM_IDENTITY_LIMIT  0x100000000ULL          /* kernel identity map: 0-4 GB */
#define SHM_PER_LIST_PAGE   512ULL                  /* frame entries per list page */
#define SHM_LIST_PAGES      ((NX_SHM_SIZE_MAX / SHM_PAGE) / SHM_PER_LIST_PAGE)  /* 32 */

typedef struct nx_shm {
    nx_object_t hdr;                /* must be first */
    uint64_t size;                  /* bytes, page multiple */
    uint64_t npages;
    uint64_t list[SHM_LIST_PAGES];  /* physical pages holding the frame list */
    struct process *charged;        /* creator while it is alive, else NULL */
    uint32_t creator_pid;           /* for logs only */
    struct nx_shm *next;            /* all live objects (creator bookkeeping) */
} nx_shm_t;

static nx_shm_t *g_shm_list = NULL;
static uint64_t g_shm_objects = 0;

/* ============ Logging ============ */

static void s_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) { serial_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    serial_puts(&buf[i]);
}

static void s_hex(uint64_t v) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) serial_putc(hex[(v >> i) & 0xF]);
}

static void s_log(struct process *proc, const char *msg) {
    serial_puts("[SHM] PID ");
    s_dec(proc ? proc->pid : 0);
    serial_puts(": ");
    serial_puts(msg);
    serial_puts("\n");
}

/* ============ Object lifetime ============ */

/* A frame the kernel can reach and that is not a user-window alias */
static int frame_usable(uint64_t f) {
    return f != 0 && f + SHM_PAGE <= SHM_IDENTITY_LIMIT &&
           !(f < USER_WINDOW_END && f + SHM_PAGE > USER_WINDOW_BASE);
}

static void zero_frame(uint64_t f) {
    volatile uint64_t *p = (volatile uint64_t *)f;   /* identity mapped */
    for (uint64_t i = 0; i < SHM_PAGE / 8; i++) p[i] = 0;
}

/* Frame list entries for frames [first, first + 512) start here */
static uint64_t *list_chunk(const nx_shm_t *shm, uint64_t first) {
    return (uint64_t *)shm->list[first / SHM_PER_LIST_PAGE];
}

static uint64_t shm_frame(const nx_shm_t *shm, uint64_t i) {
    return list_chunk(shm, i)[i % SHM_PER_LIST_PAGE];
}

/* Free the first `count` frames and every list page */
static void free_frames(nx_shm_t *shm, uint64_t count) {
    for (uint64_t i = 0; i < count; i++) {
        uint64_t f = shm_frame(shm, i);
        if (f) pmm_free_page(f);
    }
    for (uint64_t j = 0; j < SHM_LIST_PAGES; j++) {
        if (shm->list[j]) pmm_free_page(shm->list[j]);
        shm->list[j] = 0;
    }
}

/* Map the whole object at va, one 512-frame chunk (2 MiB) at a time */
static int map_object(addrspace_t *as, uint64_t va, const nx_shm_t *shm, uint32_t flags) {
    for (uint64_t first = 0; first < shm->npages; first += SHM_PER_LIST_PAGE) {
        uint64_t n = shm->npages - first;
        if (n > SHM_PER_LIST_PAGE) n = SHM_PER_LIST_PAGE;
        if (addrspace_map_shared(as, va + first * SHM_PAGE, list_chunk(shm, first), n, flags) != 0) {
            addrspace_unmap_shared(as, va, first);
            return -1;
        }
    }
    return 0;
}

static void shm_destroy(nx_object_t *obj) {
    nx_shm_t *shm = (nx_shm_t *)obj;

    /* Unlink from the live list */
    nx_shm_t **link = &g_shm_list;
    while (*link && *link != shm) link = &(*link)->next;
    if (*link) *link = shm->next;
    if (g_shm_objects) g_shm_objects--;

    if (shm->charged) {
        nx_proc_ipc_t *ipc = nx_ipc_state(shm->charged, 0);
        if (ipc && ipc->shm_bytes >= shm->size) ipc->shm_bytes -= shm->size;
    }

    free_frames(shm, shm->npages);

    serial_puts("[SHM] Freed object of ");
    s_dec(shm->size);
    serial_puts(" bytes (created by PID ");
    s_dec(shm->creator_pid);
    serial_puts("), ");
    s_dec(shm->npages);
    serial_puts(" pages returned\n");

    kfree(shm);
}

void nx_shm_forget_creator(struct process *proc) {
    for (nx_shm_t *s = g_shm_list; s; s = s->next) {
        if (s->charged == proc) s->charged = NULL;
    }
}

/* ============ Mappings ============ */

static int span_overlaps(const nx_proc_ipc_t *ipc, uint64_t start, uint64_t span) {
    for (uint32_t i = 0; i < ipc->map_count; i++) {
        const nx_shm_mapping_t *m = &ipc->maps[i];
        if (start < m->va + m->span && m->va < start + span) return 1;
    }
    return 0;
}

/* First free span in the shared region, or 0 when it is full */
static uint64_t find_span(const nx_proc_ipc_t *ipc, uint64_t span) {
    uint64_t start = AS_SHARED_BASE;
    while (start + span <= AS_SHARED_END && start + span > start) {
        int moved = 0;
        for (uint32_t i = 0; i < ipc->map_count; i++) {
            const nx_shm_mapping_t *m = &ipc->maps[i];
            if (start < m->va + m->span && m->va < start + span) {
                start = m->va + m->span;
                moved = 1;
            }
        }
        if (!moved && !span_overlaps(ipc, start, span)) return start;
    }
    return 0;
}

static void remove_mapping(struct process *proc, nx_proc_ipc_t *ipc, uint32_t idx) {
    nx_shm_mapping_t m = ipc->maps[idx];
    if (proc->as) addrspace_unmap_shared(proc->as, m.va, m.pages);

    ipc->maps[idx] = ipc->maps[ipc->map_count - 1];
    ipc->maps[ipc->map_count - 1].shm = NULL;
    ipc->map_count--;

    nx_object_unref(&m.shm->hdr);
}

void nx_shm_unmap_all(struct process *proc, nx_proc_ipc_t *ipc) {
    if (!proc || !ipc) return;
    while (ipc->map_count > 0) {
        remove_mapping(proc, ipc, ipc->map_count - 1);
    }
}

/* ============ Syscalls ============ */

/*
 * nx_handle_t sys_shm_create(uint64_t size, uint32_t flags)
 * Rights of the new handle: READ, WRITE, MAP, TRANSFER, DUPLICATE.
 */
int64_t sys_shm_create_impl(uint64_t size, uint64_t flags, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc || !proc->as) return -EPERM;

    if (flags != 0 || size == 0 || size > NX_SHM_SIZE_MAX) return -EINVAL;
    if (!addrspace_shared_ok()) {
        s_log(proc, "shm.create refused: shared region unavailable");
        return -ENOMEM;
    }

    uint64_t npages = (size + SHM_PAGE - 1) / SHM_PAGE;
    uint64_t bytes = npages * SHM_PAGE;

    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 1);
    if (!ipc) return -ENOMEM;
    if (ipc->shm_bytes + bytes > NX_SHM_PROC_QUOTA) {
        s_log(proc, "shm.create refused: per-process quota (256 MiB) reached");
        return -ENOMEM;
    }

    pmm_stats_t st;
    pmm_get_stats(&st);
    if (st.free_pages < npages || (st.free_pages - npages) * SHM_PAGE < SHM_SYSTEM_FLOOR) {
        s_log(proc, "shm.create refused: would leave less than 64 MiB free");
        return -ENOMEM;
    }

    nx_shm_t *shm = (nx_shm_t *)kzalloc(sizeof(nx_shm_t));
    if (!shm) {
        s_log(proc, "shm.create refused: out of kernel heap");
        return -ENOMEM;
    }

    uint64_t list_pages = (npages + SHM_PER_LIST_PAGE - 1) / SHM_PER_LIST_PAGE;
    for (uint64_t j = 0; j < list_pages; j++) {
        uint64_t f = pmm_alloc_page();
        if (!frame_usable(f)) {
            if (f) pmm_free_page(f);
            free_frames(shm, 0);
            kfree(shm);
            s_log(proc, "shm.create refused: out of physical memory (frame list)");
            return -ENOMEM;
        }
        zero_frame(f);
        shm->list[j] = f;
    }

    for (uint64_t i = 0; i < npages; i++) {
        uint64_t f = pmm_alloc_page();
        if (!frame_usable(f)) {
            if (f) pmm_free_page(f);
            free_frames(shm, i);
            kfree(shm);
            s_log(proc, "shm.create refused: out of physical memory");
            return -ENOMEM;
        }
        zero_frame(f);   /* never hand out a previous owner's data */
        list_chunk(shm, i)[i % SHM_PER_LIST_PAGE] = f;
    }

    shm->hdr.type = NX_OBJ_SHM;
    shm->hdr.refs = 0;
    shm->hdr.destroy = shm_destroy;
    shm->size = bytes;
    shm->npages = npages;
    shm->charged = proc;
    shm->creator_pid = proc->pid;
    shm->next = g_shm_list;
    g_shm_list = shm;
    g_shm_objects++;
    ipc->shm_bytes += bytes;

    int32_t h = nx_handle_alloc(proc, &shm->hdr, NX_RIGHTS_ALL);
    if (h < 0) {
        /* No reference was taken: destroy directly (uncharges and frees) */
        shm_destroy(&shm->hdr);
        return h;
    }

    serial_puts("[SHM] PID ");
    s_dec(proc->pid);
    serial_puts(" created ");
    s_dec(bytes);
    serial_puts(" bytes (");
    s_dec(npages);
    serial_puts(" pages), handle ");
    s_dec((uint64_t)h);
    serial_puts("\n");
    return h;
}

/*
 * void *sys_shm_map(nx_handle_t h, uint32_t prot, uint64_t *out_size)
 * Maps the whole object; the kernel picks the address.
 */
int64_t sys_shm_map_impl(uint64_t h, uint64_t prot, uint64_t out_size, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    struct process *proc = process_current();
    if (!proc || !proc->as) return -EPERM;
    if (h > (uint64_t)0x7FFFFFFF) return -EBADF;

    if (prot == 0 || (prot & ~(uint64_t)(NX_SHM_PROT_READ | NX_SHM_PROT_WRITE))) return -EINVAL;
    uint32_t need = NX_RIGHT_MAP | NX_RIGHT_READ;
    if (prot & NX_SHM_PROT_WRITE) need |= NX_RIGHT_WRITE;

    nx_object_t *obj = NULL;
    int rc = nx_handle_get(proc, (int32_t)h, NX_OBJ_SHM, need, &obj, NULL);
    if (rc != 0) return rc;
    nx_shm_t *shm = (nx_shm_t *)obj;

    /* Validate the output pointer before changing anything */
    if (out_size && syscall_validate_ptr((void *)out_size, sizeof(uint64_t), 1) != 0) {
        return -EFAULT;
    }

    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 1);
    if (!ipc) return -ENOMEM;
    if (ipc->map_count >= NX_SHM_MAP_MAX) {
        s_log(proc, "shm.map refused: 128 mappings already");
        return -ENOMEM;
    }

    uint64_t span = (shm->size + SHM_PAGE + SHM_SPAN_ALIGN - 1) & ~(SHM_SPAN_ALIGN - 1);
    uint64_t va = find_span(ipc, span);
    if (!va) {
        s_log(proc, "shm.map refused: shared region full");
        return -ENOMEM;
    }

    uint32_t flags = (prot & NX_SHM_PROT_WRITE) ? AS_MAP_WRITE : 0;
    if (map_object(proc->as, va, shm, flags) != 0) {
        s_log(proc, "shm.map failed: no page-table memory");
        return -ENOMEM;
    }

    nx_shm_mapping_t *m = &ipc->maps[ipc->map_count++];
    m->va = va;
    m->span = span;
    m->pages = shm->npages;
    m->shm = shm;
    nx_object_ref(&shm->hdr);

    if (out_size) {
        uint64_t sz = shm->size;
        if (syscall_copy_to_user((void *)out_size, &sz, sizeof(sz)) != 0) {
            remove_mapping(proc, ipc, ipc->map_count - 1);
            return -EFAULT;
        }
    }

#ifdef IPC_TRACE
    serial_puts("[SHM] PID ");
    s_dec(proc->pid);
    serial_puts(" mapped ");
    s_dec(shm->size);
    serial_puts(" bytes at ");
    s_hex(va);
    serial_puts("\n");
#else
    (void)s_hex;
#endif
    return (int64_t)va;
}

/* int sys_shm_unmap(void *addr) - addr must be a base returned by shm.map */
int64_t sys_shm_unmap_impl(uint64_t addr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    struct process *proc = process_current();
    nx_proc_ipc_t *ipc = nx_ipc_state(proc, 0);
    if (!ipc) return -EINVAL;

    for (uint32_t i = 0; i < ipc->map_count; i++) {
        if (ipc->maps[i].va == addr) {
            remove_mapping(proc, ipc, i);
            return 0;
        }
    }
    return -EINVAL;
}
