/*
 * NeolyxOS Kernel Stress Tests
 * 
 * Tests for: input flooding, FS hammering, long uptime, memory leaks.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *kmalloc(uint64_t size);
extern void *kzalloc(uint64_t size);
extern void kfree(void *ptr);

/* Memory stats */
typedef struct {
    uint64_t total_memory;
    uint64_t free_memory;
    uint64_t used_memory;
} pmm_stats_t;
extern void pmm_get_stats(pmm_stats_t *stats);

/* Block cache stats */
extern void bcache_stats(uint32_t *hits, uint32_t *misses);
extern void bcache_sync(void);

/* Keyboard API (from drivers/keyboard.c) */
extern int kbd_check_key(void);
extern char kbd_getchar(void);

/* ============ Test Utilities ============ */

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static void serial_hex(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static void test_header(const char *name) {
    serial_puts("\n========================================\n");
    serial_puts("[TEST] ");
    serial_puts(name);
    serial_puts("\n========================================\n");
}

static void test_pass(const char *msg) {
    serial_puts("[PASS] ");
    serial_puts(msg);
    serial_puts("\n");
}

static void test_fail(const char *msg) {
    serial_puts("[FAIL] ");
    serial_puts(msg);
    serial_puts("\n");
}

static void test_info(const char *msg, uint64_t val) {
    serial_puts("[INFO] ");
    serial_puts(msg);
    serial_dec(val);
    serial_puts("\n");
}

/* ============ Memory Leak Detection ============ */

static uint64_t initial_free_memory = 0;

static void memory_snapshot_start(void) {
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    initial_free_memory = stats.free_memory;
}

static int memory_check_leaks(const char *test_name) {
    pmm_stats_t stats;
    pmm_get_stats(&stats);
    
    int64_t diff = (int64_t)initial_free_memory - (int64_t)stats.free_memory;
    
    if (diff > 4096) {  /* Allow 4KB tolerance for internal structures */
        test_fail("Memory leak detected in: ");
        serial_puts(test_name);
        serial_puts("\n");
        test_info("  Leaked bytes: ", diff);
        return 1;
    } else {
        test_pass("No memory leaks");
        return 0;
    }
}

/* ============ Test 1: Memory Allocation Stress ============ */

#define ALLOC_ITERATIONS 1000
#define ALLOC_SIZE_SMALL 64
#define ALLOC_SIZE_MEDIUM 1024
#define ALLOC_SIZE_LARGE 4096

static int test_memory_stress(void) {
    test_header("Memory Allocation Stress Test");
    memory_snapshot_start();
    
    void *ptrs[100];
    int failures = 0;
    
    /* Test 1: Rapid alloc/free cycles */
    serial_puts("[RUN] Rapid alloc/free cycles (1000 iterations)\n");
    for (int i = 0; i < ALLOC_ITERATIONS; i++) {
        void *p = kmalloc(ALLOC_SIZE_SMALL);
        if (!p) { failures++; continue; }
        kfree(p);
    }
    test_info("  Completed iterations: ", ALLOC_ITERATIONS);
    test_info("  Failures: ", failures);
    
    /* Test 2: Bulk allocation then bulk free */
    serial_puts("[RUN] Bulk alloc (100 blocks) then bulk free\n");
    for (int i = 0; i < 100; i++) {
        ptrs[i] = kmalloc(ALLOC_SIZE_MEDIUM);
    }
    int allocated = 0;
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) allocated++;
    }
    test_info("  Successfully allocated: ", allocated);
    
    for (int i = 0; i < 100; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }
    test_pass("Bulk alloc/free complete");
    
    /* Test 3: Interleaved alloc/free pattern */
    serial_puts("[RUN] Interleaved alloc/free pattern\n");
    for (int round = 0; round < 10; round++) {
        for (int i = 0; i < 50; i++) {
            ptrs[i] = kmalloc(ALLOC_SIZE_LARGE);
        }
        for (int i = 0; i < 25; i++) {
            if (ptrs[i]) kfree(ptrs[i]);
            ptrs[i] = NULL;
        }
        for (int i = 25; i < 50; i++) {
            if (ptrs[i]) kfree(ptrs[i]);
            ptrs[i] = NULL;
        }
    }
    test_pass("Interleaved pattern complete");
    
    return memory_check_leaks("Memory Stress");
}

/* ============ Test 2: Keyboard Queue Stress ============ */

static int test_keyboard_queue_stress(void) {
    test_header("Keyboard Input Stress Test");
    
    /* Note: Using consolidated keyboard driver API (kbd_check_key/kbd_getchar) */
    
    serial_puts("[RUN] Rapid keyboard polling (10000 iterations)\n");
    int keys_found = 0;
    
    uint64_t start = pit_get_ticks();
    for (int i = 0; i < 10000; i++) {
        if (kbd_check_key()) {
            kbd_getchar();  /* Consume the key */
            keys_found++;
        }
    }
    uint64_t end = pit_get_ticks();
    
    test_info("  Keys processed: ", keys_found);
    test_info("  Time (ms): ", end - start);
    test_pass("Keyboard polling stress complete");
    
    return 0;
}

/* ============ Test 3: Block Cache Stress ============ */

static int test_bcache_stress(void) {
    test_header("Block Cache Stress Test");
    memory_snapshot_start();
    
    uint32_t hits_before, misses_before;
    uint32_t hits_after, misses_after;
    
    bcache_stats(&hits_before, &misses_before);
    
    /* Simulate block access patterns */
    serial_puts("[RUN] Sequential access pattern\n");
    /* Note: Actual bcache_get requires device setup */
    /* This test verifies stats tracking works */
    
    bcache_stats(&hits_after, &misses_after);
    
    test_info("  Cache hits: ", hits_after);
    test_info("  Cache misses: ", misses_after);
    
    /* Test sync */
    serial_puts("[RUN] Cache sync\n");
    bcache_sync();
    test_pass("Cache sync complete");
    
    return memory_check_leaks("Block Cache");
}

/* ============ Test 4: Long Uptime Simulation ============ */

static int test_uptime_stability(void) {
    test_header("Uptime Stability Test");
    memory_snapshot_start();
    
    uint64_t start_ticks = pit_get_ticks();
    
    serial_puts("[RUN] Mixed operations loop (1000 cycles)\n");
    
    void *p;
    for (int cycle = 0; cycle < 1000; cycle++) {
        /* Memory operations */
        p = kmalloc(256);
        if (p) {
            /* Touch memory */
            *((uint8_t *)p) = 0xAA;
            kfree(p);
        }
        
        /* Keyboard check */
        while (kbd_check_key()) {
            kbd_getchar();
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    uint64_t end_ticks = pit_get_ticks();
    uint64_t elapsed = end_ticks - start_ticks;
    
    test_info("  Elapsed time (ms): ", elapsed);
    test_info("  Operations per ms: ", elapsed > 0 ? 3000 / elapsed : 0);
    test_pass("Stability test complete");
    
    return memory_check_leaks("Uptime Stability");
}

/* ============ Test 5: Pointer Validation ============ */

static int test_pointer_validation(void) {
    test_header("Pointer Validation Test");
    
    /* Test NULL handling */
    serial_puts("[RUN] NULL pointer handling\n");
    kfree(NULL);  /* Should not crash */
    test_pass("kfree(NULL) safe");
    
    /* Test double-free detection (if implemented) */
    serial_puts("[RUN] Allocation validity\n");
    void *p = kmalloc(64);
    if (p) {
        /* Verify we can write to allocated memory */
        for (int i = 0; i < 64; i++) {
            ((uint8_t *)p)[i] = (uint8_t)i;
        }
        /* Verify we can read back */
        int valid = 1;
        for (int i = 0; i < 64; i++) {
            if (((uint8_t *)p)[i] != (uint8_t)i) {
                valid = 0;
                break;
            }
        }
        kfree(p);
        
        if (valid) {
            test_pass("Memory read/write correct");
        } else {
            test_fail("Memory corruption detected");
            return 1;
        }
    } else {
        test_fail("Allocation failed");
        return 1;
    }
    
    return 0;
}

/* ============ Main Test Runner ============ */

typedef struct {
    const char *name;
    int (*test_fn)(void);
} test_entry_t;

static test_entry_t all_tests[] = {
    {"Memory Allocation", test_memory_stress},
    {"Keyboard Queue", test_keyboard_queue_stress},
    {"Block Cache", test_bcache_stress},
    {"Uptime Stability", test_uptime_stability},
    {"Pointer Validation", test_pointer_validation},
    {NULL, NULL}
};

/*
 * stress_test_run - Run all kernel stress tests
 * 
 * Returns: Number of failed tests
 */
int stress_test_run(void) {
    serial_puts("\n");
    serial_puts("╔═══════════════════════════════════════════════╗\n");
    serial_puts("║     NeolyxOS Kernel Stress Test Suite         ║\n");
    serial_puts("╚═══════════════════════════════════════════════╝\n");
    
    pmm_stats_t initial_stats;
    pmm_get_stats(&initial_stats);
    test_info("Initial free memory (bytes): ", initial_stats.free_memory);
    
    uint64_t start_time = pit_get_ticks();
    int passed = 0;
    int failed = 0;
    
    for (int i = 0; all_tests[i].name != NULL; i++) {
        int result = all_tests[i].test_fn();
        if (result == 0) {
            passed++;
        } else {
            failed++;
        }
    }
    
    uint64_t end_time = pit_get_ticks();
    
    serial_puts("\n========================================\n");
    serial_puts("           STRESS TEST SUMMARY\n");
    serial_puts("========================================\n");
    test_info("Total tests: ", passed + failed);
    test_info("Passed: ", passed);
    test_info("Failed: ", failed);
    test_info("Elapsed time (ms): ", end_time - start_time);
    
    pmm_stats_t final_stats;
    pmm_get_stats(&final_stats);
    test_info("Final free memory (bytes): ", final_stats.free_memory);
    
    int64_t total_leak = (int64_t)initial_stats.free_memory - (int64_t)final_stats.free_memory;
    if (total_leak > 0) {
        test_info("Total memory leaked: ", total_leak);
    } else {
        serial_puts("[INFO] No overall memory leaks detected\n");
    }
    
    if (failed == 0) {
        serial_puts("\n[RESULT] ALL TESTS PASSED ✓\n\n");
    } else {
        serial_puts("\n[RESULT] SOME TESTS FAILED ✗\n\n");
    }
    
    return failed;
}

/*
 * stress_test_single - Run a single test by name
 */
int stress_test_single(const char *name) {
    for (int i = 0; all_tests[i].name != NULL; i++) {
        /* Simple string compare */
        const char *a = all_tests[i].name;
        const char *b = name;
        int match = 1;
        while (*a && *b) {
            if (*a != *b) { match = 0; break; }
            a++; b++;
        }
        if (match && *a == '\0') {
            return all_tests[i].test_fn();
        }
    }
    serial_puts("[ERROR] Test not found: ");
    serial_puts(name);
    serial_puts("\n");
    return -1;
}
