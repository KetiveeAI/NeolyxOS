#!/usr/bin/env python3
"""
NeolyxOS Cursor Generator - Creates NXIB cursor images

Usage: python3 generate_cursors.py [output_dir]
"""

import struct
import sys
import os
import math

NXIB_MAGIC = b'NXIB'
NXIB_VERSION = 1

def write_nxib(filepath, size, pixels):
    """Write NXIB file"""
    with open(filepath, 'wb') as f:
        f.write(NXIB_MAGIC)
        f.write(struct.pack('<B', NXIB_VERSION))
        f.write(struct.pack('<H', size))
        f.write(struct.pack('<H', size))
        f.write(struct.pack('<B', 0))  # flags: RGBA
        f.write(bytes(pixels))

def make_canvas(size):
    return [0] * (size * size * 4)

def set_pixel(pixels, size, x, y, r, g, b, a=255):
    if 0 <= x < size and 0 <= y < size:
        idx = (y * size + x) * 4
        pixels[idx] = r
        pixels[idx + 1] = g
        pixels[idx + 2] = b
        pixels[idx + 3] = a

def draw_pointer(size):
    """Classic arrow pointer cursor"""
    pixels = make_canvas(size)
    # Arrow shape
    points = [
        (0, 0), (0, 1), (0, 2), (0, 3), (0, 4), (0, 5), (0, 6), (0, 7), (0, 8),
        (1, 1), (1, 2), (1, 3), (1, 4), (1, 5), (1, 6), (1, 7),
        (2, 2), (2, 3), (2, 4), (2, 5), (2, 6),
        (3, 3), (3, 4), (3, 5), (3, 6), (3, 7),
        (4, 4), (4, 5), (4, 6), (4, 7), (4, 8),
        (5, 5), (5, 9), (5, 10),
        (6, 6), (6, 10), (6, 11),
        (7, 7), (7, 11), (7, 12),
    ]
    # Scale for size
    scale = size / 16
    for px, py in points:
        x, y = int(px * scale), int(py * scale)
        set_pixel(pixels, size, x, y, 255, 255, 255, 255)
        # Black outline
        for dx, dy in [(-1, 0), (1, 0), (0, -1), (0, 1)]:
            ox, oy = x + dx, y + dy
            if 0 <= ox < size and 0 <= oy < size:
                idx = (oy * size + ox) * 4
                if pixels[idx + 3] == 0:  # Not already white
                    set_pixel(pixels, size, ox, oy, 0, 0, 0, 255)
    return pixels

def draw_text_cursor(size):
    """I-beam text cursor"""
    pixels = make_canvas(size)
    mid = size // 2
    # Vertical line
    for y in range(2, size - 2):
        set_pixel(pixels, size, mid, y, 0, 0, 0, 255)
        set_pixel(pixels, size, mid - 1, y, 0, 0, 0, 128)
        set_pixel(pixels, size, mid + 1, y, 0, 0, 0, 128)
    # Top serifs
    for x in range(mid - 3, mid + 4):
        set_pixel(pixels, size, x, 2, 0, 0, 0, 255)
        set_pixel(pixels, size, x, size - 3, 0, 0, 0, 255)
    return pixels

def draw_hand(size):
    """Pointing hand cursor"""
    pixels = make_canvas(size)
    # Simple hand shape
    # Palm
    for y in range(size // 3, size - 2):
        for x in range(2, size - 2):
            if y > size // 2 or x < size * 2 // 3:
                set_pixel(pixels, size, x, y, 255, 255, 255, 255)
    # Finger
    for y in range(2, size // 2):
        for x in range(size // 4, size // 2):
            set_pixel(pixels, size, x, y, 255, 255, 255, 255)
    return pixels

def draw_crosshair(size):
    """Crosshair cursor"""
    pixels = make_canvas(size)
    mid = size // 2
    # Horizontal line
    for x in range(size):
        if abs(x - mid) > 2:
            set_pixel(pixels, size, x, mid, 0, 0, 0, 255)
    # Vertical line
    for y in range(size):
        if abs(y - mid) > 2:
            set_pixel(pixels, size, mid, y, 0, 0, 0, 255)
    return pixels

def draw_move(size):
    """Four-way move cursor"""
    pixels = make_canvas(size)
    mid = size // 2
    # Four arrows
    for i in range(size // 4):
        # Up arrow
        set_pixel(pixels, size, mid, i, 0, 0, 0, 255)
        set_pixel(pixels, size, mid - i // 2, i, 0, 0, 0, 255)
        set_pixel(pixels, size, mid + i // 2, i, 0, 0, 0, 255)
        # Down arrow
        set_pixel(pixels, size, mid, size - 1 - i, 0, 0, 0, 255)
        set_pixel(pixels, size, mid - i // 2, size - 1 - i, 0, 0, 0, 255)
        set_pixel(pixels, size, mid + i // 2, size - 1 - i, 0, 0, 0, 255)
        # Left arrow
        set_pixel(pixels, size, i, mid, 0, 0, 0, 255)
        set_pixel(pixels, size, i, mid - i // 2, 0, 0, 0, 255)
        set_pixel(pixels, size, i, mid + i // 2, 0, 0, 0, 255)
        # Right arrow
        set_pixel(pixels, size, size - 1 - i, mid, 0, 0, 0, 255)
        set_pixel(pixels, size, size - 1 - i, mid - i // 2, 0, 0, 0, 255)
        set_pixel(pixels, size, size - 1 - i, mid + i // 2, 0, 0, 0, 255)
    # Center
    set_pixel(pixels, size, mid, mid, 0, 0, 0, 255)
    return pixels

def draw_wait(size):
    """Spinning wait cursor"""
    pixels = make_canvas(size)
    mid = size // 2
    r = size // 3
    # Circle with gap
    for angle in range(0, 300, 15):
        rad = angle * math.pi / 180
        x = int(mid + r * math.cos(rad))
        y = int(mid + r * math.sin(rad))
        gray = 200 - (angle * 150 // 300)
        set_pixel(pixels, size, x, y, gray, gray, gray, 255)
    return pixels

def draw_not_allowed(size):
    """Circle with slash - not allowed"""
    pixels = make_canvas(size)
    mid = size // 2
    r = size // 3
    # Circle
    for angle in range(360):
        rad = angle * math.pi / 180
        x = int(mid + r * math.cos(rad))
        y = int(mid + r * math.sin(rad))
        set_pixel(pixels, size, x, y, 255, 60, 60, 255)
    # Diagonal slash
    for i in range(-r, r):
        x = mid + i
        y = mid - i
        if 0 <= x < size and 0 <= y < size:
            set_pixel(pixels, size, x, y, 255, 60, 60, 255)
    return pixels

CURSORS = {
    'pointer': draw_pointer,
    'text': draw_text_cursor,
    'hand': draw_hand,
    'crosshair': draw_crosshair,
    'move': draw_move,
    'wait': draw_wait,
    'not_allowed': draw_not_allowed,
}

def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else '/home/swana/Documents/NEOLYXOS/neolyx-os/resources/cursors'
    os.makedirs(output_dir, exist_ok=True)
    
    sizes = [16, 24, 32]
    
    print(f"Generating cursors to {output_dir}...")
    total = 0
    
    for name, draw_func in CURSORS.items():
        for size in sizes:
            pixels = draw_func(size)
            filepath = os.path.join(output_dir, f"{name}_{size}.nxi")
            write_nxib(filepath, size, pixels)
            total += 1
        
        # Default size
        pixels = draw_func(16)
        filepath = os.path.join(output_dir, f"{name}.nxi")
        write_nxib(filepath, 16, pixels)
        total += 1
    
    print(f"Generated {total} cursor files.")

if __name__ == '__main__':
    main()
