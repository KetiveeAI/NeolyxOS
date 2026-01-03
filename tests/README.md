# NeolyxOS Test Suite

Comprehensive kernel and driver tests for QEMU VM verification.

## Test Files

| File | Description |
|------|-------------|
| `kernel_tests.c` | Kernel component tests (memory, AHCI, timer, VFS) |
| `run_tests.sh` | QEMU test runner script |

## Running Tests

### Quick Test (from terminal shell)
```
> test
```
This runs `kernel_run_tests()` and outputs results to serial.

### Full QEMU Test
```bash
cd tests
./run_tests.sh --build
```
This rebuilds kernel, boots in QEMU, and captures serial output.

## Test Categories

### Memory Tests
- PMM page allocation/free
- Heap kmalloc/kfree
- Memory stress (100 allocations)

### AHCI Disk Tests  
- Read sector 0 (MBR/GPT detection)
- Write/read verification

### Timer Tests
- Tick counter advancement

### VFS Tests
- Root filesystem access

## Expected Output
```
╔════════════════════════════════════════╗
║    NeolyxOS Kernel Test Suite v1.0     ║
╚════════════════════════════════════════╝

--- PMM Allocator Test ---
[PASS] PMM allocator working correctly

--- AHCI Read Test ---
  Found MBR signature (0x55AA)
[PASS] AHCI read working correctly

TEST SUMMARY:
  Passed:  7
  Failed:  0
  Skipped: 0
✓ ALL TESTS PASSED!
```