#!/bin/bash
# REOX Benchmark Suite
# Compares REOX vs C vs Rust performance

set -e

BENCH_DIR="$(dirname "$0")"
cd "$BENCH_DIR/.."

echo "========================================"
echo "       REOX Benchmark Suite"
echo "========================================"
echo ""

# Build runtime if needed
if [ ! -f runtime/libreox_runtime.a ]; then
    echo "Building runtime..."
    (cd runtime && make)
fi

# Compile REOX version
echo "[1/3] Compiling REOX..."
./target/release/reoxc benchmarks/fib.rx -o benchmarks/fib_reox.c
gcc -O3 -flto -march=native -o benchmarks/fib_reox benchmarks/fib_reox.c \
    -I runtime/ runtime/libreox_runtime.a -lm
strip benchmarks/fib_reox
REOX_SIZE=$(stat -c%s benchmarks/fib_reox)

# Compile C version
echo "[2/3] Compiling C..."
gcc -O3 -flto -march=native -o benchmarks/fib_c benchmarks/fib.c
strip benchmarks/fib_c
C_SIZE=$(stat -c%s benchmarks/fib_c)

# Compile Rust version
echo "[3/3] Compiling Rust..."
rustc -O -C lto=fat -C target-cpu=native -o benchmarks/fib_rust benchmarks/fib.rs 2>/dev/null || {
    echo "    (Rust not installed, skipping)"
    RUST_AVAILABLE=0
}
if [ -f benchmarks/fib_rust ]; then
    strip benchmarks/fib_rust
    RUST_SIZE=$(stat -c%s benchmarks/fib_rust)
    RUST_AVAILABLE=1
fi

echo ""
echo "========================================"
echo "       Binary Sizes"
echo "========================================"
printf "  %-10s %10s bytes\n" "REOX:" "$REOX_SIZE"
printf "  %-10s %10s bytes\n" "C:" "$C_SIZE"
if [ "$RUST_AVAILABLE" = "1" ]; then
    printf "  %-10s %10s bytes\n" "Rust:" "$RUST_SIZE"
fi

echo ""
echo "========================================"
echo "       Performance (fib(35))"
echo "========================================"

# Run benchmarks
echo ""
echo "--- REOX ---"
time ./benchmarks/fib_reox

echo ""
echo "--- C ---"
time ./benchmarks/fib_c

if [ "$RUST_AVAILABLE" = "1" ]; then
    echo ""
    echo "--- Rust ---"
    time ./benchmarks/fib_rust
fi

echo ""
echo "========================================"
echo "       Benchmark Complete"
echo "========================================"
