/*
 * NeolyxOS - scheduler run queues (K0b)
 *
 * See include/core/sched_queue.h. Tested on the host by
 * tests/host/sched_queue_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "core/sched_queue.h"
#include "core/nx_mem.h"

sq_class_t sq_class_of(const process_t *p) {
    if (!p) return SQ_CLASS_FOREGROUND;
    if (p->flags & (PROC_FLAG_SYSTEM | PROC_FLAG_KERNEL)) return SQ_CLASS_SYSTEM;
    if (p->sched_class == NX_CLASS_BACKGROUND) return SQ_CLASS_BACKGROUND;
    return SQ_CLASS_FOREGROUND;
}

uint32_t sq_priority_of(sq_class_t c) {
    switch (c) {
    case SQ_CLASS_SYSTEM:     return SQ_PRIORITY_SYSTEM;
    case SQ_CLASS_BACKGROUND: return SQ_PRIORITY_BACKGROUND;
    default:                  return SQ_PRIORITY_FOREGROUND;
    }
}

int sq_contains(const sched_queues_t *q, const process_t *p) {
    if (!q || !p) return 0;
    sq_class_t c = sq_class_of(p);
    return p->prev != NULL || q->head[c] == p;
}

int sq_add(sched_queues_t *q, process_t *p) {
    if (!q || !p) return 0;
    if (sq_contains(q, p)) return 0;
    sq_class_t c = sq_class_of(p);

    p->next = NULL;
    p->prev = q->tail[c];
    if (q->tail[c]) {
        q->tail[c]->next = p;
    } else {
        q->head[c] = p;
    }
    q->tail[c] = p;
    return 1;
}

void sq_remove(sched_queues_t *q, process_t *p) {
    if (!q || !p || !sq_contains(q, p)) return;
    sq_class_t c = sq_class_of(p);

    if (p->prev) {
        p->prev->next = p->next;
    } else {
        q->head[c] = p->next;
    }
    if (p->next) {
        p->next->prev = p->prev;
    } else {
        q->tail[c] = p->prev;
    }
    p->next = NULL;
    p->prev = NULL;
}

int sq_any(const sched_queues_t *q) {
    if (!q) return 0;
    for (int c = 0; c < SQ_CLASS_COUNT; c++) {
        if (q->head[c]) return 1;
    }
    return 0;
}

/* FOREGROUND vs BACKGROUND, the same streak rule one level down */
static process_t *sq_pick_app(sched_queues_t *q) {
    process_t *fg = q->head[SQ_CLASS_FOREGROUND];
    process_t *bg = q->head[SQ_CLASS_BACKGROUND];

    if (!bg) {
        q->fg_streak = 0;               /* nobody waiting: no debt */
        return fg;
    }
    if (fg && q->fg_streak < SQ_FOREGROUND_TURNS) {
        q->fg_streak++;
        return fg;
    }
    q->fg_streak = 0;
    return bg;
}

process_t *sq_pick(sched_queues_t *q) {
    if (!q) return NULL;
    process_t *sys = q->head[SQ_CLASS_SYSTEM];
    int apps = (q->head[SQ_CLASS_FOREGROUND] != NULL || q->head[SQ_CLASS_BACKGROUND] != NULL);

    if (!apps) {
        q->system_streak = 0;           /* nobody waiting: no debt */
        return sys;
    }
    if (sys && q->system_streak < SQ_SYSTEM_TURNS) {
        q->system_streak++;
        return sys;
    }
    q->system_streak = 0;
    return sq_pick_app(q);
}
