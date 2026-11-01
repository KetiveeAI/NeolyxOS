/*
 * NeolyxOS - scheduler run queues (K0b)
 *
 * See include/core/sched_queue.h. Tested on the host by
 * tests/host/sched_queue_test.c.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include "core/sched_queue.h"

sq_class_t sq_class_of(const process_t *p) {
    if (p && (p->flags & (PROC_FLAG_SYSTEM | PROC_FLAG_KERNEL))) return SQ_CLASS_SYSTEM;
    return SQ_CLASS_NORMAL;
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
    return q && (q->head[SQ_CLASS_SYSTEM] != NULL || q->head[SQ_CLASS_NORMAL] != NULL);
}

process_t *sq_pick(sched_queues_t *q) {
    if (!q) return NULL;
    process_t *sys = q->head[SQ_CLASS_SYSTEM];
    process_t *norm = q->head[SQ_CLASS_NORMAL];

    if (!norm) {
        q->system_streak = 0;           /* nobody waiting: no debt */
        return sys;
    }
    if (sys && q->system_streak < SQ_SYSTEM_TURNS) {
        q->system_streak++;
        return sys;
    }
    q->system_streak = 0;
    return norm;
}
