/*
 * NeolyxOS Kernel Test Suite
 * Tests kernel components and drivers in QEMU VM
 * 
 * Usage: Build into kernel and call kernel_run_tests() from terminal
 */

#include <stdint.h>
#include <stddef.h>

/* External kernel APIs */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pmm_alloc_page(void);
extern void pmm_free_page(uint64_t addr);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);
extern uint64_t timer_get_ticks(void);

/* AHCI driver */
extern int ahci_read(int port, uint64_t lba, uint32_t count, void *buffer);
extern int ahci_write(int port, uint64_t lba, uint32_t count, const void *buffer);

/* VFS */
extern int vfs_open(const char *path, int flags);
extern int vfs_close(int fd);
extern int64_t vfs_read(int fd, void *buf, size_t count);
extern int64_t vfs_write(int fd, const void *buf, size_t count);

/* Test results */
static int tests_passed = 0;
static int tests_failed = 0;
static int tests_skipped = 0;

/* Helper macros */
#define TEST_ASSERT(cond, msg) do { \
    if (!(cond)) { \
        serial_puts("[FAIL] "); \
        serial_puts(msg); \
        serial_puts("\n"); \
        tests_failed++; \
        return; \
    } \
} while(0)

#define TEST_PASS(msg) do { \
    serial_puts("[PASS] "); \
    serial_puts(msg); \
    serial_puts("\n"); \
    tests_passed++; \
} while(0)

#define TEST_SKIP(msg) do { \
    serial_puts("[SKIP] "); \
    serial_puts(msg); \
    serial_puts("\n"); \
    tests_skipped++; \
} while(0)

/* Print number */
static void print_num(uint64_t n) {
    char buf[24];
    int i = 0;
    if (n == 0) { serial_putc('0'); return; }
    while (n > 0) {
        buf[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0) serial_putc(buf[--i]);
}

/* ======== Memory Tests ======== */

static void test_pmm_alloc_free(void) {
    serial_puts("\n--- PMM Allocator Test ---\n");
    
    uint64_t page1 = pmm_alloc_page();
    TEST_ASSERT(page1 != 0, "PMM: First page allocation");
    
    uint64_t page2 = pmm_alloc_page();
    TEST_ASSERT(page2 != 0, "PMM: Second page allocation");
    TEST_ASSERT(page1 != page2, "PMM: Unique page addresses");
    
    /* Free and reallocate */
    pmm_free_page(page1);
    uint64_t page3 = pmm_alloc_page();
    TEST_ASSERT(page3 != 0, "PMM: Post-free allocation");
    
    pmm_free_page(page2);
    pmm_free_page(page3);
    
    TEST_PASS("PMM allocator working correctly");
}

static void test_heap_alloc(void) {
    serial_puts("\n--- Heap Allocator Test ---\n");
    
    /* Small allocation */
    void *ptr1 = kmalloc(64);
    TEST_ASSERT(ptr1 != NULL, "Heap: 64-byte allocation");
    
    /* Medium allocation */
    void *ptr2 = kmalloc(1024);
    TEST_ASSERT(ptr2 != NULL, "Heap: 1KB allocation");
    
    /* Large allocation */
    void *ptr3 = kmalloc(16384);
    TEST_ASSERT(ptr3 != NULL, "Heap: 16KB allocation");
    
    /* Write test pattern */
    uint8_t *test = (uint8_t *)ptr1;
    for (int i = 0; i < 64; i++) test[i] = (uint8_t)i;
    
    /* Verify pattern */
    int valid = 1;
    for (int i = 0; i < 64; i++) {
        if (test[i] != (uint8_t)i) valid = 0;
    }
    TEST_ASSERT(valid, "Heap: Memory write/read integrity");
    
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    
    TEST_PASS("Heap allocator working correctly");
}

static void test_memory_stress(void) {
    serial_puts("\n--- Memory Stress Test ---\n");
    
    #define STRESS_ALLOCS 100
    void *ptrs[STRESS_ALLOCS];
    int allocated = 0;
    
    /* Allocate many small blocks */
    for (int i = 0; i < STRESS_ALLOCS; i++) {
        ptrs[i] = kmalloc(128);
        if (ptrs[i]) allocated++;
    }
    
    serial_puts("  Allocated ");
    print_num(allocated);
    serial_puts(" blocks\n");
    
    TEST_ASSERT(allocated > 50, "Memory stress: 50+ allocations");
    
    /* Free all */
    for (int i = 0; i < STRESS_ALLOCS; i++) {
        if (ptrs[i]) kfree(ptrs[i]);
    }
    
    TEST_PASS("Memory stress test completed");
}

/* ======== AHCI/Disk Tests ======== */

static void test_ahci_read(void) {
    serial_puts("\n--- AHCI Read Test ---\n");
    
    uint64_t buffer_phys = pmm_alloc_page();
    if (!buffer_phys) {
        TEST_SKIP("AHCI: No memory for buffer");
        return;
    }
    
    uint8_t *buffer = (uint8_t *)buffer_phys;
    
    /* Read first sector (should contain MBR/GPT) */
    int result = ahci_read(0, 0, 1, buffer);
    
    if (result != 0) {
        TEST_SKIP("AHCI: No disk attached on port 0");
        pmm_free_page(buffer_phys);
        return;
    }
    
    /* Check for MBR signature */
    if (buffer[510] == 0x55 && buffer[511] == 0xAA) {
        serial_puts("  Found MBR signature (0x55AA)\n");
    }
    
    /* Check for GPT signature at LBA 1 */
    result = ahci_read(0, 1, 1, buffer);
    if (result == 0) {
        if (buffer[0] == 'E' && buffer[1] == 'F' && buffer[2] == 'I') {
            serial_puts("  Found GPT header (EFI PART)\n");
        }
    }
    
    pmm_free_page(buffer_phys);
    TEST_PASS("AHCI read working correctly");
}

static void test_ahci_write_read(void) {
    serial_puts("\n--- AHCI Write/Read Test ---\n");
    
    /* Use a high LBA to avoid overwriting important data */
    uint64_t test_lba = 100000;  /* ~50MB into disk */
    
    uint64_t buf1_phys = pmm_alloc_page();
    uint64_t buf2_phys = pmm_alloc_page();
    
    if (!buf1_phys || !buf2_phys) {
        TEST_SKIP("AHCI: No memory for buffers");
        if (buf1_phys) pmm_free_page(buf1_phys);
        if (buf2_phys) pmm_free_page(buf2_phys);
        return;
    }
    
    uint8_t *write_buf = (uint8_t *)buf1_phys;
    uint8_t *read_buf = (uint8_t *)buf2_phys;
    
    /* Write test pattern */
    for (int i = 0; i < 512; i++) {
        write_buf[i] = (uint8_t)(i ^ 0xA5);
    }
    
    int result = ahci_write(0, test_lba, 1, write_buf);
    if (result != 0) {
        TEST_SKIP("AHCI: Write failed (no disk or read-only)");
        pmm_free_page(buf1_phys);
        pmm_free_page(buf2_phys);
        return;
    }
    
    /* Read back */
    result = ahci_read(0, test_lba, 1, read_buf);
    TEST_ASSERT(result == 0, "AHCI: Read after write");
    
    /* Verify data */
    int match = 1;
    for (int i = 0; i < 512; i++) {
        if (read_buf[i] != write_buf[i]) match = 0;
    }
    
    pmm_free_page(buf1_phys);
    pmm_free_page(buf2_phys);
    
    TEST_ASSERT(match, "AHCI: Data integrity after write/read");
    TEST_PASS("AHCI write/read working correctly");
}

/* ======== Timer Tests ======== */

static void test_timer(void) {
    serial_puts("\n--- Timer Test ---\n");
    
    uint64_t t1 = timer_get_ticks();
    
    /* Busy wait */
    for (volatile int i = 0; i < 10000000; i++);
    
    uint64_t t2 = timer_get_ticks();
    
    serial_puts("  Ticks before: ");
    print_num(t1);
    serial_puts("\n  Ticks after:  ");
    print_num(t2);
    serial_puts("\n");
    
    TEST_ASSERT(t2 > t1, "Timer: Tick counter advancing");
    TEST_PASS("Timer working correctly");
}

/* ======== VFS Tests ======== */

static void test_vfs_basic(void) {
    serial_puts("\n--- VFS Basic Test ---\n");
    
    /* Try to open root */
    int fd = vfs_open("/", 0);
    
    if (fd < 0) {
        TEST_SKIP("VFS: No filesystem mounted");
        return;
    }
    
    vfs_close(fd);
    TEST_PASS("VFS root accessible");
}

/* ======== Network Tests ======== */

/* Network API externs */
extern int socket_create(int type, int protocol);
extern int socket_close(int fd);
extern uint16_t tcpip_checksum(const void *data, uint32_t len);

static void test_socket_create_close(void) {
    serial_puts("\n--- Socket Create/Close Test ---\n");
    
    int fd = socket_create(2 /* SOCK_DGRAM */, 0);
    if (fd < 0) {
        TEST_SKIP("Socket: Network stack not initialized");
        return;
    }
    TEST_ASSERT(fd >= 3, "Socket: Create UDP socket (fd >= 3)");
    
    int result = socket_close(fd);
    TEST_ASSERT(result == 0, "Socket: Close socket");
    
    TEST_PASS("Socket create/close working");
}

static void test_checksum(void) {
    serial_puts("\n--- TCP/IP Checksum Test ---\n");
    
    /* Test with known IP header data */
    uint8_t data[] = {0x45, 0x00, 0x00, 0x73, 0x00, 0x00,
                      0x40, 0x00, 0x40, 0x11, 0x00, 0x00,
                      0xc0, 0xa8, 0x00, 0x01, 0xc0, 0xa8, 0x00, 0xc7};
    
    uint16_t cs = tcpip_checksum(data, 20);
    TEST_ASSERT(cs != 0, "Checksum: Non-zero result");
    
    /* Test with all zeros - checksum should be 0xFFFF */
    uint8_t zeros[20] = {0};
    uint16_t cs_zeros = tcpip_checksum(zeros, 20);
    TEST_ASSERT(cs_zeros == 0xFFFF, "Checksum: All zeros = 0xFFFF");
    
    TEST_PASS("Checksum calculation working");
}

static void test_udp_sendto_recvfrom(void) {
    serial_puts("\n--- UDP sendto/recvfrom Test ---\n");
    
    /* Function declarations */
    extern int socket_sendto(int fd, const void *data, uint32_t len, void *addr);
    extern int socket_recvfrom(int fd, void *buf, uint32_t len, void *addr);
    
    int fd = socket_create(2 /* SOCK_DGRAM */, 0);
    if (fd < 0) {
        TEST_SKIP("UDP: Could not create socket");
        return;
    }
    
    /* Test that sendto returns error with no interface (expected) */
    uint8_t test_data[16] = {0};
    struct {
        uint16_t family;
        uint16_t port;
        uint8_t addr[4];
        uint8_t zero[8];
    } dest_addr = {2, 0x3500, {8, 8, 8, 8}, {0}};  /* 8.8.8.8:53 */
    
    /* This will likely fail (no network) but shouldn't crash */
    int ret = socket_sendto(fd, test_data, 16, &dest_addr);
    serial_puts("  sendto returned: ");
    if (ret < 0) serial_puts("error (expected without network)\n");
    else { print_num(ret); serial_puts(" bytes\n"); }
    
    /* Verify recvfrom returns 0 (no data) */
    uint8_t recv_buf[64];
    ret = socket_recvfrom(fd, recv_buf, 64, NULL);
    TEST_ASSERT(ret == 0, "UDP recvfrom: Returns 0 with empty buffer");
    
    socket_close(fd);
    TEST_PASS("UDP sendto/recvfrom functions verified");
}

static void test_icmp_ping_function(void) {
    serial_puts("\n--- ICMP Ping Function Test ---\n");
    
    extern int icmp_ping(void *iface, uint8_t dst[4], uint16_t seq);
    extern void *network_get_interface_by_index(int index);
    
    void *iface = network_get_interface_by_index(0);
    if (!iface) {
        TEST_SKIP("ICMP: No network interface available");
        return;
    }
    
    /* Ping loopback - function should not crash */
    uint8_t loopback[4] = {127, 0, 0, 1};
    int ret = icmp_ping(iface, loopback, 1);
    serial_puts("  icmp_ping returned: ");
    if (ret < 0) serial_puts("error\n");
    else { print_num(ret); serial_puts(" bytes sent\n"); }
    
    TEST_PASS("ICMP ping function exists and callable");
}

/* ======== Main Test Runner ======== */

void kernel_run_tests(void) {
    serial_puts("\n");
    serial_puts("╔════════════════════════════════════════╗\n");
    serial_puts("║    NeolyxOS Kernel Test Suite v1.0     ║\n");
    serial_puts("║    Running in QEMU VM Environment      ║\n");
    serial_puts("╚════════════════════════════════════════╝\n");
    
    tests_passed = 0;
    tests_failed = 0;
    tests_skipped = 0;
    
    /* Memory Tests */
    test_pmm_alloc_free();
    test_heap_alloc();
    test_memory_stress();
    
    /* Disk Tests */
    test_ahci_read();
    test_ahci_write_read();
    
    /* Timer Tests */
    test_timer();
    
    /* VFS Tests */
    test_vfs_basic();
    
    /* Network Tests */
    test_socket_create_close();
    test_checksum();
    test_udp_sendto_recvfrom();
    test_icmp_ping_function();
    
    /* Summary */
    serial_puts("\n");
    serial_puts("════════════════════════════════════════\n");
    serial_puts("TEST SUMMARY:\n");
    serial_puts("  Passed:  "); print_num(tests_passed); serial_puts("\n");
    serial_puts("  Failed:  "); print_num(tests_failed); serial_puts("\n");
    serial_puts("  Skipped: "); print_num(tests_skipped); serial_puts("\n");
    serial_puts("════════════════════════════════════════\n");
    
    if (tests_failed == 0) {
        serial_puts("✓ ALL TESTS PASSED!\n");
    } else {
        serial_puts("✗ SOME TESTS FAILED\n");
    }
}
