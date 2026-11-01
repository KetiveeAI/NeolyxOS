/*
 * NeolyxOS - scheduler run queues (K0b)
 *
 * Two FIFO run queues: SYSTEM (PROC_FLAG_SYSTEM user processes such as
 * the desktop, and kernel threads) and NORMAL (every other process).
 * When both have work, the picker gives SYSTEM up to SQ_SYSTEM_TURNS
 * consecutive turns, then one NORMAL turn, so neither class can starve
 * the other. Within a class it is round robin.
 *
 * The queues link processes through process_t.next/prev. Callers hold
 * interrupts off (irq_save) around every call; nothing here allocates.
 *
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SCHED_QUEUE_H
#define NEOLYX_SCHED_QUEUE_H

#include "core/process.h"

typedef enum {
    SQ_CLASS_SYSTEM = 0,
    SQ_CLASS_NORMAL = 1,
    SQ_CLASS_COUNT
} sq_class_t;

/* SYSTEM turns in a row while NORMAL work waits (3 of every 4 turns) */
#define SQ_SYSTEM_TURNS     3

/* process_t.priority per class (0 = highest); informational */
#define SQ_PRIORITY_SYSTEM  64
#define SQ_PRIORITY_NORMAL  128

typedef struct {
    process_t *head[SQ_CLASS_COUNT];
    process_t *tail[SQ_CLASS_COUNT];
    uint32_t   system_streak;   /* SYSTEM picks in a row while NORMAL waited */
} sched_queues_t;

/* Class of a process from its flags (never changes while it is queued). */
sq_class_t sq_class_of(const process_t *p);

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
