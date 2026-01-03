#!/bin/bash
#
# ZebraScript Engine Sync Script
# Syncs the shared engine from ZepraBrowser source
#
# Copyright (c) 2025 KetiveeAI. All Rights Reserved.
#

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BROWSER_DIR="$SCRIPT_DIR/../../apps/zeprabrowser/source/zepraScript"
ENGINE_DIR="$SCRIPT_DIR"

echo "=================================================="
echo "  ZebraScript Engine Sync"
echo "  Syncing from ZepraBrowser to shared lib"
echo "=================================================="

# Check if browser source exists
if [ ! -d "$BROWSER_DIR" ]; then
    echo "ERROR: ZepraBrowser source not found at $BROWSER_DIR"
    exit 1
fi

echo ""
echo "Source: $BROWSER_DIR"
echo "Target: $ENGINE_DIR"
echo ""

# Create backup of current engine
BACKUP_DIR="$ENGINE_DIR/.backup_$(date +%Y%m%d_%H%M%S)"
echo "[1/5] Creating backup..."
mkdir -p "$BACKUP_DIR"
cp -r "$ENGINE_DIR/src" "$BACKUP_DIR/" 2>/dev/null || true
echo "      Backup saved to $BACKUP_DIR"

# Sync runtime components
echo "[2/5] Syncing runtime..."
mkdir -p "$ENGINE_DIR/src/runtime"
cp "$BROWSER_DIR/src/runtime/"*.cpp "$ENGINE_DIR/src/runtime/" 2>/dev/null || true

# Sync builtins
echo "[3/5] Syncing builtins..."
mkdir -p "$ENGINE_DIR/src/builtins"
cp "$BROWSER_DIR/src/builtins/"*.cpp "$ENGINE_DIR/src/builtins/" 2>/dev/null || true

# Sync JIT compiler
echo "[4/5] Syncing JIT..."
mkdir -p "$ENGINE_DIR/src/jit"
cp "$BROWSER_DIR/src/jit/"*.cpp "$ENGINE_DIR/src/jit/" 2>/dev/null || true

# Sync garbage collector
echo "[5/5] Syncing GC..."
mkdir -p "$ENGINE_DIR/src/gc"
cp "$BROWSER_DIR/src/gc/"*.cpp "$ENGINE_DIR/src/gc/" 2>/dev/null || true

# Update version
echo ""
echo "Updating version info..."
BROWSER_VERSION=$(grep -oP 'ZS_VERSION_STRING\s+"\K[^"]+' "$BROWSER_DIR/include/zeprascript/config.hpp" 2>/dev/null || echo "1.0.0")
echo "  Browser version: $BROWSER_VERSION"

# Build the engine
echo ""
echo "Building shared library..."
cd "$ENGINE_DIR"
make clean >/dev/null 2>&1 || true
if make; then
    echo "  Build successful!"
else
    echo "  Build failed. Check errors above."
    exit 1
fi

# Run tests
echo ""
echo "Running tests..."
if make test; then
    echo "  All tests passed!"
else
    echo "  Some tests failed. Check output above."
fi

echo ""
echo "=================================================="
echo "  Sync complete!"
echo "  Library: $ENGINE_DIR/lib/libzeprascript.so"
echo "=================================================="
