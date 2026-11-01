/*
 * NeolyxOS - scheduler run queues (K0b)
 *
 * Three FIFO run queues:
 *   SYSTEM      PROC_FLAG_SYSTEM user processes (the desktop) and kernel
 *               threads (K0b);
 *   FOREGROUND  apps in NX_CLASS_FOREGROUND, the default (K1);
 *   BACKGROUND  apps nxserver (or the app itself) moved to
 *               NX_CLASS_BACKGROUND (K1).
 * When SYSTEM and app work are both waiting, SYSTEM gets up to
 * SQ_SYSTEM_TURNS turns in a row, then one app turn. App turns go to
 * FOREGROUND up to SQ_FOREGROUND_TURNS in a row while BACKGROUND waits,
 * then one BACKGROUND turn. Under full load that is 9:2:1, and no class
 * can starve. Within a class it is round robin.
 *
 * The queues link processes through process_t.next/prev. Callers hold
 * interrupts off (irq_save) around every call; nothing here allocates.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SCHED_QUEUE_H
#define NEOLYX_SCHED_QUEUE_H

#include "core/process.h"

/* Lower value = more urgent (used for wake-up preemption) */
typedef enum {
    SQ_CLASS_SYSTEM = 0,
    SQ_CLASS_FOREGROUND = 1,
    SQ_CLASS_BACKGROUND = 2,
    SQ_CLASS_COUNT
} sq_class_t;

/* SYSTEM turns in a row while app work waits (3 of every 4 turns) */
#define SQ_SYSTEM_TURNS      3
/* FOREGROUND turns in a row while BACKGROUND waits (2 of every 3 app turns) */
#define SQ_FOREGROUND_TURNS  2

/* process_t.priority per class (0 = highest); informational */
#define SQ_PRIORITY_SYSTEM      64
#define SQ_PRIORITY_FOREGROUND  112
#define SQ_PRIORITY_BACKGROUND  144

typedef struct {
    process_t *head[SQ_CLASS_COUNT];
    process_t *tail[SQ_CLASS_COUNT];
    uint32_t   system_streak;   /* SYSTEM picks in a row while app work waited */
    uint32_t   fg_streak;       /* FOREGROUND picks in a row while BACKGROUND waited */
} sched_queues_t;

/*
 * Class of a process: SYSTEM from its flags, otherwise from
 * process_t.sched_class (BACKGROUND when NX_CLASS_BACKGROUND, else
 * FOREGROUND). It must not change while the process is queued: remove it,
 * change sched_class, add it again (process_set_class does this).
 */
sq_class_t sq_class_of(const process_t *p);

/* process_t.priority value for a class */
uint32_t sq_priority_of(sq_class_t c);

/* Append p to its class queue. Returns 1 if added, 0 if already queued
 * or p is NULL. Does not change p->state. */
int sq_add(sched_queues_t *q, process_t *p);

/* Unlink p from its queue if queued (no-op otherwise). */
void sq_remove(sched_queues_t *q, process_t *p);

/* 1 if p is in a queue. */
int sq_contains(const sched_queues_t *q, const process_t *p);

/* 1 if any process is queued. */
int sq_any(const sched_queues_t *q);

/* The process that should run next (still queued; the caller removes
 * it), or NULL when both queues are empty. Updates the turn counter. */
process_t *sq_pick(sched_queues_t *q);

#endif /* NEOLYX_SCHED_QUEUE_H */
