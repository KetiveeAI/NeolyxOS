/*
 * Host unit test for the K0b scheduler run queues
 * (kernel/src/core/sched_queue.c).
 *
 * Checks:
 *   - class of a process from its flags (SYSTEM, KERNEL -> system class);
 *   - add/remove/contains: FIFO order per class, double add refused,
 *     removing the head, middle, tail and an unqueued process;
 *   - the picker gives SYSTEM exactly 3 of every 4 turns while both
 *     classes have work, and NORMAL every turn when SYSTEM is empty;
 *   - no starvation in either direction over a long simulated run that
 *     re-queues each picked process like schedule() does;
 *   - the turn counter resets when no NORMAL work waits, so a desktop
 *     that ran alone does not owe NORMAL turns later;
 *   - K1: FOREGROUND/BACKGROUND app classes (process_t.sched_class):
 *     9:2:1 SYSTEM:FOREGROUND:BACKGROUND under full load, 2:1 between apps
 *     with no SYSTEM work, BACKGROUND never starves, and a class change
 *     done as remove/change/add (process_set_class) keeps the queues sane.
 * ("NORMAL" below means any app: FOREGROUND by default.)
 *
 * Build and run from the repository root:
 *   gcc -std=c11 -O1 -Wall -Wextra -Ikernel/include \
 *       -o /tmp/sched_queue_test tests/host/sched_queue_test.c
 *   /tmp/sched_queue_test
 *
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdio.h>
#include <string.h>

#include "../../kernel/src/core/sched_queue.c"
#include "../../kernel/include/core/nx_mem.h"

static int g_fail, g_checks;

#define CHECK(cond, ...) do {                                   \
    g_checks++;                                                 \
    if (!(cond)) {                                              \
        g_fail++;                                               \
        printf("FAIL %s:%d: ", __FILE__, __LINE__);             \
        printf(__VA_ARGS__);                                    \
        printf("\n");                                           \
    }                                                           \
} while (0)

#define NPROC 8
static process_t procs[NPROC];

static void reset(sched_queues_t *q) {
    memset(q, 0, sizeof(*q));
    memset(procs, 0, sizeof(procs));
    for (int i = 0; i < NPROC; i++) procs[i].pid = (uint32_t)i;
}

/* Queue contents of one class as pids, for order checks */
static int queue_pids(const sched_queues_t *q, sq_class_t c, uint32_t *out, int max) {
    int n = 0;
    for (const process_t *p = q->head[c]; p && n < max; p = p->next) out[n++] = p->pid;
    return n;
}

static void test_class(void) {
    process_t p;
    memset(&p, 0, sizeof(p));
    p.flags = PROC_FLAG_USER;
    CHECK(sq_class_of(&p) == SQ_CLASS_FOREGROUND, "user process should default to FOREGROUND");
    p.flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    CHECK(sq_class_of(&p) == SQ_CLASS_SYSTEM, "PROC_FLAG_SYSTEM should be SYSTEM");
    p.flags = PROC_FLAG_KERNEL;
    CHECK(sq_class_of(&p) == SQ_CLASS_SYSTEM, "kernel thread should be SYSTEM");
    p.flags = PROC_FLAG_USER | PROC_FLAG_DETACHED;
    CHECK(sq_class_of(&p) == SQ_CLASS_FOREGROUND, "detached user process should be FOREGROUND");
    CHECK(sq_class_of(NULL) == SQ_CLASS_FOREGROUND, "NULL should not crash");
}

static void test_add_remove(void) {
    sched_queues_t q;
    reset(&q);
    uint32_t pids[NPROC];

    CHECK(!sq_any(&q), "empty queues report work");
    CHECK(sq_pick(&q) == NULL, "pick on empty queues");
    CHECK(sq_add(&q, NULL) == 0, "NULL add accepted");
    CHECK(sq_add(NULL, &procs[0]) == 0, "add to NULL queues accepted");
    sq_remove(&q, NULL);
    sq_remove(NULL, &procs[0]);

    for (int i = 0; i < 4; i++) procs[i].flags = PROC_FLAG_USER;
    for (int i = 0; i < 4; i++) CHECK(sq_add(&q, &procs[i]) == 1, "add %d", i);
    CHECK(sq_add(&q, &procs[0]) == 0, "double add of head accepted");
    CHECK(sq_add(&q, &procs[2]) == 0, "double add of middle accepted");
    CHECK(sq_add(&q, &procs[3]) == 0, "double add of tail accepted");
    CHECK(queue_pids(&q, SQ_CLASS_FOREGROUND, pids, NPROC) == 4, "4 queued");
    CHECK(pids[0] == 0 && pids[1] == 1 && pids[2] == 2 && pids[3] == 3, "FIFO order");
    CHECK(q.head[SQ_CLASS_SYSTEM] == NULL, "NORMAL process landed in SYSTEM");

    sq_remove(&q, &procs[1]);                      /* middle */
    CHECK(!sq_contains(&q, &procs[1]), "removed middle still queued");
    sq_remove(&q, &procs[1]);                      /* again: no-op */
    sq_remove(&q, &procs[0]);                      /* head */
    sq_remove(&q, &procs[3]);                      /* tail */
    int n = queue_pids(&q, SQ_CLASS_FOREGROUND, pids, NPROC);
    CHECK(n == 1 && pids[0] == 2, "after removals only pid 2 remains (n=%d)", n);
    CHECK(q.tail[SQ_CLASS_FOREGROUND] == &procs[2], "tail not fixed up");
    sq_remove(&q, &procs[2]);                      /* only element */
    CHECK(!sq_any(&q), "queues not empty after removing all");
    CHECK(q.head[SQ_CLASS_FOREGROUND] == NULL && q.tail[SQ_CLASS_FOREGROUND] == NULL,
          "head/tail not cleared");

    /* Re-add after removal works and links are clean */
    CHECK(sq_add(&q, &procs[1]) == 1, "re-add after removal refused");
    CHECK(procs[1].prev == NULL && procs[1].next == NULL, "stale links on single entry");

    /* A process never queued is not contained, even with a stale class head */
    procs[5].flags = PROC_FLAG_USER;
    CHECK(!sq_contains(&q, &procs[5]), "unqueued process reported queued");
}

/* Picks n turns, re-queueing the picked process at its tail like
 * schedule() does for a process that stays runnable. Counts per class. */
static void run_turns(sched_queues_t *q, int n, int *sys_turns, int *norm_turns,
                      int *per_pid) {
    for (int t = 0; t < n; t++) {
        process_t *p = sq_pick(q);
        if (!p) return;
        sq_remove(q, p);
        if (sq_class_of(p) == SQ_CLASS_SYSTEM) (*sys_turns)++; else (*norm_turns)++;
        if (per_pid) per_pid[p->pid]++;
        sq_add(q, p);
    }
}

static void test_weighting(void) {
    sched_queues_t q;
    reset(&q);

    /* One desktop, one busy app: 3 SYSTEM turns then 1 NORMAL, repeating */
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    procs[1].flags = PROC_FLAG_USER;
    sq_add(&q, &procs[0]);
    sq_add(&q, &procs[1]);
    const char *want = "SSSNSSSNSSSN";
    for (int t = 0; want[t]; t++) {
        process_t *p = sq_pick(&q);
        char got = (p == &procs[0]) ? 'S' : (p == &procs[1]) ? 'N' : '?';
        CHECK(got == want[t], "turn %d: got %c want %c", t, got, want[t]);
        sq_remove(&q, p);
        sq_add(&q, p);
    }

    /* Two SYSTEM, three NORMAL over 4000 turns: exactly 3000/1000, and
     * round robin inside each class (no process of a class starves) */
    reset(&q);
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    procs[1].flags = PROC_FLAG_KERNEL;
    for (int i = 2; i < 5; i++) procs[i].flags = PROC_FLAG_USER;
    for (int i = 0; i < 5; i++) sq_add(&q, &procs[i]);
    int s = 0, nrm = 0, per[NPROC] = {0};
    run_turns(&q, 4000, &s, &nrm, per);
    CHECK(s == 3000 && nrm == 1000, "weighting: system %d normal %d", s, nrm);
    CHECK(per[0] == 1500 && per[1] == 1500, "system round robin: %d %d", per[0], per[1]);
    for (int i = 2; i < 5; i++) {
        CHECK(per[i] >= 333 && per[i] <= 334, "normal pid %d got %d turns", i, per[i]);
    }

    /* Many NORMAL processes cannot starve the one SYSTEM process */
    reset(&q);
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    for (int i = 1; i < NPROC; i++) { procs[i].flags = PROC_FLAG_USER; }
    for (int i = 0; i < NPROC; i++) sq_add(&q, &procs[i]);
    s = nrm = 0;
    run_turns(&q, 400, &s, &nrm, NULL);
    CHECK(s == 300 && nrm == 100, "7 hogs vs desktop: system %d normal %d", s, nrm);

    /* Only NORMAL work: it gets every turn */
    reset(&q);
    procs[0].flags = PROC_FLAG_USER;
    procs[1].flags = PROC_FLAG_USER;
    sq_add(&q, &procs[0]);
    sq_add(&q, &procs[1]);
    s = nrm = 0;
    run_turns(&q, 100, &s, &nrm, NULL);
    CHECK(s == 0 && nrm == 100, "normal only: system %d normal %d", s, nrm);

    /* Only SYSTEM work: it gets every turn and builds no debt */
    reset(&q);
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    sq_add(&q, &procs[0]);
    s = nrm = 0;
    run_turns(&q, 100, &s, &nrm, NULL);
    CHECK(s == 100 && nrm == 0, "system only: system %d normal %d", s, nrm);
    CHECK(q.system_streak == 0, "streak grew with no NORMAL waiting (%u)", q.system_streak);

    /* NORMAL arrives after the desktop ran alone: desktop still gets its
     * 3 turns first, then NORMAL; no burst of NORMAL turns */
    procs[1].flags = PROC_FLAG_USER;
    sq_add(&q, &procs[1]);
    const char *want2 = "SSSNSSSN";
    for (int t = 0; want2[t]; t++) {
        process_t *p = sq_pick(&q);
        char got = (p == &procs[0]) ? 'S' : (p == &procs[1]) ? 'N' : '?';
        CHECK(got == want2[t], "late normal turn %d: got %c want %c", t, got, want2[t]);
        sq_remove(&q, p);
        sq_add(&q, p);
    }

    /* SYSTEM blocks (leaves the queue) mid-streak: NORMAL runs next, and
     * when SYSTEM returns it gets turns again within one NORMAL turn */
    reset(&q);
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    procs[1].flags = PROC_FLAG_USER;
    sq_add(&q, &procs[0]);
    sq_add(&q, &procs[1]);
    process_t *p = sq_pick(&q);
    CHECK(p == &procs[0], "first pick not SYSTEM");
    sq_remove(&q, p);                               /* desktop blocks */
    p = sq_pick(&q);
    CHECK(p == &procs[1], "NORMAL did not run while SYSTEM blocked");
    sq_remove(&q, p);
    sq_add(&q, p);
    sq_add(&q, &procs[0]);                          /* desktop wakes */
    p = sq_pick(&q);
    CHECK(p == &procs[0], "woken SYSTEM not picked next");
}

/* K1: three classes */
static void test_three_classes(void) {
    sched_queues_t q;
    reset(&q);
    process_t p;
    memset(&p, 0, sizeof(p));
    p.flags = PROC_FLAG_USER;
    p.sched_class = NX_CLASS_BACKGROUND;
    CHECK(sq_class_of(&p) == SQ_CLASS_BACKGROUND, "NX_CLASS_BACKGROUND app should be BACKGROUND");
    p.sched_class = NX_CLASS_FOREGROUND;
    CHECK(sq_class_of(&p) == SQ_CLASS_FOREGROUND, "NX_CLASS_FOREGROUND app should be FOREGROUND");
    p.flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    p.sched_class = NX_CLASS_BACKGROUND;
    CHECK(sq_class_of(&p) == SQ_CLASS_SYSTEM, "a system process ignores sched_class");
    CHECK(sq_priority_of(SQ_CLASS_SYSTEM) == 64 && sq_priority_of(SQ_CLASS_FOREGROUND) == 112 &&
          sq_priority_of(SQ_CLASS_BACKGROUND) == 144, "priority per class");

    /* desktop, one focused app, two background apps: 9:2:1 */
    procs[0].flags = PROC_FLAG_USER | PROC_FLAG_SYSTEM;
    procs[1].flags = PROC_FLAG_USER;                     /* FOREGROUND */
    procs[2].flags = PROC_FLAG_USER; procs[2].sched_class = NX_CLASS_BACKGROUND;
    procs[3].flags = PROC_FLAG_USER; procs[3].sched_class = NX_CLASS_BACKGROUND;
    for (int i = 0; i < 4; i++) sq_add(&q, &procs[i]);
    int per[NPROC] = {0};
    int s = 0, n = 0;
    run_turns(&q, 1200, &s, &n, per);
    CHECK(per[0] == 900, "system got %d of 1200 (want 900)", per[0]);
    CHECK(per[1] == 200, "foreground got %d of 1200 (want 200)", per[1]);
    CHECK(per[2] == 50 && per[3] == 50, "background got %d and %d (want 50 each)", per[2], per[3]);

    /* No SYSTEM work: foreground 2 of every 3 turns, background 1 */
    reset(&q);
    procs[1].flags = PROC_FLAG_USER;
    procs[2].flags = PROC_FLAG_USER; procs[2].sched_class = NX_CLASS_BACKGROUND;
    sq_add(&q, &procs[1]);
    sq_add(&q, &procs[2]);
    const char *want = "FFBFFBFFB";
    for (int t = 0; want[t]; t++) {
        process_t *pk = sq_pick(&q);
        char got = (pk == &procs[1]) ? 'F' : (pk == &procs[2]) ? 'B' : '?';
        CHECK(got == want[t], "app turn %d: got %c want %c", t, got, want[t]);
        sq_remove(&q, pk);
        sq_add(&q, pk);
    }

    /* Background only: every turn, no debt */
    reset(&q);
    procs[2].flags = PROC_FLAG_USER; procs[2].sched_class = NX_CLASS_BACKGROUND;
    sq_add(&q, &procs[2]);
    memset(per, 0, sizeof(per));
    s = n = 0;
    run_turns(&q, 50, &s, &n, per);
    CHECK(per[2] == 50 && q.fg_streak == 0, "background alone: %d turns, streak %u", per[2], q.fg_streak);

    /* Many foreground hogs cannot starve one background app */
    reset(&q);
    for (int i = 1; i < NPROC; i++) procs[i].flags = PROC_FLAG_USER;
    procs[7].sched_class = NX_CLASS_BACKGROUND;
    for (int i = 1; i < NPROC; i++) sq_add(&q, &procs[i]);
    memset(per, 0, sizeof(per));
    s = n = 0;
    run_turns(&q, 300, &s, &n, per);
    CHECK(per[7] == 100, "lone background app got %d of 300 (want 100)", per[7]);

    /* Class change while queued, done like process_set_class */
    reset(&q);
    procs[1].flags = PROC_FLAG_USER;
    procs[2].flags = PROC_FLAG_USER;
    sq_add(&q, &procs[1]);
    sq_add(&q, &procs[2]);
    int queued = sq_contains(&q, &procs[1]);
    if (queued) sq_remove(&q, &procs[1]);
    procs[1].sched_class = NX_CLASS_BACKGROUND;
    if (queued) sq_add(&q, &procs[1]);
    CHECK(queued, "process was queued");
    CHECK(q.head[SQ_CLASS_FOREGROUND] == &procs[2] && q.tail[SQ_CLASS_FOREGROUND] == &procs[2],
          "foreground queue holds only the other app");
    CHECK(q.head[SQ_CLASS_BACKGROUND] == &procs[1] && q.tail[SQ_CLASS_BACKGROUND] == &procs[1],
          "moved app is in the background queue");
    CHECK(sq_contains(&q, &procs[1]) && sq_contains(&q, &procs[2]), "both still queued");
}

int main(void) {
    test_class();
    test_add_remove();
    test_weighting();
    test_three_classes();
    printf("sched_queue_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
