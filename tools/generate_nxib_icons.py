#!/usr/bin/env python3
"""
NeolyxOS Icon Generator - Creates NXIB bitmap icons for desktop
Generates procedural icons as pre-rendered RGBA bitmaps.

Usage: python3 generate_nxib_icons.py [output_dir]
"""

import struct
import sys
import os
import math

# NXIB Header format: magic(4) + version(1) + width(2) + height(2) + flags(1) = 10 bytes
NXIB_MAGIC = b'NXIB'
NXIB_VERSION = 1

# Icon IDs matching nxi_render.h
ICONS = {
    'wifi': 100,
    'bluetooth': 101,
    'share': 102,  # AirDrop
    'ethernet': 103,
    'brightness': 104,
    'volume': 105,
    'focus': 106,
    'mirror': 107,
    'darkmode': 108,
    'keyboard': 109,
    'play': 110,
    'pause': 111,
    'prev': 112,
    'next': 113,
    'bell': 114,
    'grid': 115,
    'settings': 10,
    'terminal': 9,
    'folder': 2,
    'search': 16,
    'close': 20,
    'app': 1,
    'file': 4,
}

# Standard sizes
SIZES = [16, 24, 32, 48, 64]

class IconCanvas:
    """Simple RGBA canvas for drawing icons"""
    
    def __init__(self, size):
        self.size = size
        self.pixels = [0] * (size * size * 4)  # RGBA
    
    def set_pixel(self, x, y, r, g, b, a=255):
        if 0 <= x < self.size and 0 <= y < self.size:
            idx = (y * self.size + x) * 4
            self.pixels[idx] = r
            self.pixels[idx + 1] = g
            self.pixels[idx + 2] = b
            self.pixels[idx + 3] = a
    
    def fill_rect(self, x, y, w, h, color):
        r, g, b, a = (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF
        if a == 0: a = 255
        for dy in range(h):
            for dx in range(w):
                self.set_pixel(x + dx, y + dy, r, g, b, a)
    
    def fill_circle(self, cx, cy, radius, color):
        r, g, b, a = (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF
        if a == 0: a = 255
        for dy in range(-radius, radius + 1):
            for dx in range(-radius, radius + 1):
                if dx*dx + dy*dy <= radius*radius:
                    self.set_pixel(cx + dx, cy + dy, r, g, b, a)
    
    def fill_rounded_rect(self, x, y, w, h, rad, color):
        # Simple approximation - fill rect then corners
        self.fill_rect(x + rad, y, w - 2*rad, h, color)
        self.fill_rect(x, y + rad, w, h - 2*rad, color)
        self.fill_circle(x + rad, y + rad, rad, color)
        self.fill_circle(x + w - rad - 1, y + rad, rad, color)
        self.fill_circle(x + rad, y + h - rad - 1, rad, color)
        self.fill_circle(x + w - rad - 1, y + h - rad - 1, rad, color)
    
    def draw_line(self, x1, y1, x2, y2, color):
        r, g, b, a = (color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF, (color >> 24) & 0xFF
        if a == 0: a = 255
        dx = abs(x2 - x1)
        dy = abs(y2 - y1)
        sx = 1 if x1 < x2 else -1
        sy = 1 if y1 < y2 else -1
        err = dx - dy
        x, y = x1, y1
        while True:
            self.set_pixel(x, y, r, g, b, a)
            if x == x2 and y == y2:
                break
            e2 = 2 * err
            if e2 > -dy:
                err -= dy
                x += sx
            if e2 < dx:
                err += dx
                y += sy
    
    def to_bytes(self):
        return bytes(self.pixels)

def draw_wifi(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    # Draw arcs (simplified as circles)
    for arc in range(3):
        ar = sz // 6 + arc * sz // 6
        for a in range(-45, 46, 8):
            px = cx + (ar * a) // 90
            py = cy - ar + (ar * (45 - abs(a))) // 60
            canvas.fill_circle(px, py, max(1, sz // 16), color)
    canvas.fill_circle(cx, cy + sz // 4, sz // 10, color)

def draw_bluetooth(canvas, color):
    sz = canvas.size
    cx = sz // 2
    lw = max(1, sz // 12)
    canvas.fill_rect(cx - lw, sz // 6, lw * 2, sz * 2 // 3, color)
    for i in range(sz // 4):
        canvas.fill_rect(cx + lw + i, sz // 4 + i * 2 // 3, lw, 1, color)
        canvas.fill_rect(cx + lw + i, sz * 3 // 4 - i * 2 // 3, lw, 1, color)

def draw_share(canvas, color):
    sz = canvas.size
    cx = sz // 2
    canvas.fill_circle(cx, sz // 5, sz // 8, color)
    canvas.fill_circle(sz // 5, sz * 3 // 4, sz // 8, color)
    canvas.fill_circle(sz * 4 // 5, sz * 3 // 4, sz // 8, color)
    canvas.draw_line(cx, sz // 5, sz // 5, sz * 3 // 4, color)
    canvas.draw_line(cx, sz // 5, sz * 4 // 5, sz * 3 // 4, color)

def draw_ethernet(canvas, color):
    sz = canvas.size
    canvas.fill_rounded_rect(sz // 4, sz // 4, sz // 2, sz // 2, sz // 10, color)
    for i in range(3):
        canvas.fill_rect(sz // 3 + i * sz // 6 - 1, sz * 3 // 4, 2, sz // 6, color)

def draw_brightness(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    r = sz // 3
    canvas.fill_circle(cx, cy, r // 2, color)
    for i in range(8):
        angle = i * 45 * math.pi / 180
        dx = int(r * 3 // 4 * math.cos(angle))
        dy = int(r * 3 // 4 * math.sin(angle))
        canvas.fill_circle(cx + dx, cy + dy, sz // 12, color)

def draw_volume(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    canvas.fill_rect(sz // 4, cy - sz // 8, sz // 6, sz // 4, color)
    for i in range(sz // 4):
        h = sz // 8 + i * 2
        canvas.fill_rect(sz // 4 + sz // 6 + i, cy - h // 2, 2, h, color)

def draw_focus(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    r = sz // 3
    canvas.fill_circle(cx, cy, r, color)
    canvas.fill_circle(cx + r // 2, cy - r // 4, r * 2 // 3, 0xFF1C1C1E)

def draw_darkmode(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    r = sz // 3
    canvas.fill_circle(cx, cy, r, color)
    canvas.fill_rect(cx, 0, sz // 2, sz, 0xFF1C1C1E)

def draw_mirror(canvas, color):
    sz = canvas.size
    canvas.fill_rounded_rect(sz // 8, sz // 4, sz // 3, sz // 2, sz // 16, color)
    canvas.fill_rounded_rect(sz // 2, sz // 5, sz // 3, sz // 2, sz // 16, color)

def draw_keyboard(canvas, color):
    sz = canvas.size
    canvas.fill_rounded_rect(sz // 8, sz // 3, sz * 3 // 4, sz // 3, sz // 12, color)
    for k in range(3):
        canvas.fill_rect(sz // 4 + k * sz // 5, sz // 3 + sz // 12, sz // 8, sz // 10, 0xFF1C1C1E)

def draw_play(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    for i in range(sz // 2):
        canvas.fill_rect(sz // 4 + i // 2, cy - i // 2, 1, i + 1, color)

def draw_pause(canvas, color):
    sz = canvas.size
    canvas.fill_rect(sz // 3 - sz // 10, sz // 4, sz // 6, sz // 2, color)
    canvas.fill_rect(sz * 2 // 3 - sz // 10, sz // 4, sz // 6, sz // 2, color)

def draw_prev(canvas, color):
    sz = canvas.size
    cy = sz // 2
    canvas.fill_rect(sz // 6, sz // 4, sz // 12, sz // 2, color)
    for i in range(sz // 4):
        canvas.fill_rect(sz // 2 - i // 2, cy - i // 2, 1, i + 1, color)

def draw_next(canvas, color):
    sz = canvas.size
    cy = sz // 2
    for i in range(sz // 4):
        canvas.fill_rect(sz // 3 + i // 2, cy - i // 2, 1, i + 1, color)
    canvas.fill_rect(sz * 5 // 6 - sz // 12, sz // 4, sz // 12, sz // 2, color)

def draw_bell(canvas, color):
    sz = canvas.size
    cx = sz // 2
    canvas.fill_circle(cx, sz // 3, sz // 4, color)
    canvas.fill_rect(sz // 3, sz // 3, sz // 3, sz // 3, color)
    canvas.fill_circle(cx, sz * 2 // 3 + 2, sz // 3, color)
    canvas.fill_circle(cx, sz - sz // 8, sz // 10, color)

def draw_grid(canvas, color):
    sz = canvas.size
    for row in range(3):
        for col in range(3):
            gx = sz // 5 + col * sz // 3
            gy = sz // 5 + row * sz // 3
            canvas.fill_circle(gx, gy, sz // 10, color)

def draw_settings(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    r = sz // 3
    canvas.fill_circle(cx, cy, r, color)
    canvas.fill_circle(cx, cy, r // 2, 0xFF1C1C1E)
    for i in range(6):
        angle = i * 60 * math.pi / 180
        tx = cx + int((r + sz // 12) * math.cos(angle))
        ty = cy + int((r + sz // 12) * math.sin(angle))
        canvas.fill_rect(tx - sz // 16, ty - sz // 16, sz // 8, sz // 8, color)

def draw_terminal(canvas, color):
    sz = canvas.size
    cy = sz // 2
    canvas.fill_rounded_rect(sz // 8, sz // 8, sz * 3 // 4, sz * 3 // 4, sz // 10, 0xFF2D2D2D)
    for i in range(sz // 4):
        canvas.fill_rect(sz // 4 + i, cy - i // 2, 1, i + 1, color)
    canvas.fill_rect(sz // 2, cy + 2, sz // 4, sz // 12, color)

def draw_folder(canvas, color):
    sz = canvas.size
    canvas.fill_rounded_rect(sz // 8, sz // 3, sz * 3 // 4, sz // 2, sz // 12, color)
    canvas.fill_rounded_rect(sz // 8, sz // 4, sz // 3, sz // 6, sz // 16, color)

def draw_search(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    canvas.fill_circle(cx - sz // 8, cy - sz // 8, sz // 4, color)
    canvas.fill_circle(cx - sz // 8, cy - sz // 8, sz // 6, 0xFF1C1C1E)
    for i in range(sz // 4):
        canvas.fill_rect(cx + i, cy + i, sz // 8, sz // 8, color)

def draw_close(canvas, color):
    sz = canvas.size
    for i in range(sz // 2):
        canvas.fill_rect(sz // 4 + i, sz // 4 + i, sz // 8, sz // 8, color)
        canvas.fill_rect(sz * 3 // 4 - i - sz // 8, sz // 4 + i, sz // 8, sz // 8, color)

def draw_app(canvas, color):
    sz = canvas.size
    cx, cy = sz // 2, sz // 2
    canvas.fill_rounded_rect(sz // 6, sz // 6, sz * 2 // 3, sz * 2 // 3, sz // 6, color)

def draw_file(canvas, color):
    sz = canvas.size
    canvas.fill_rect(sz // 4, sz // 6, sz // 2, sz * 2 // 3, color)
    canvas.fill_rect(sz // 4 + sz // 2 - sz // 8, sz // 6, sz // 8, sz // 6, 0xFF1C1C1E)

DRAW_FUNCS = {
    'wifi': draw_wifi,
    'bluetooth': draw_bluetooth,
    'share': draw_share,
    'ethernet': draw_ethernet,
    'brightness': draw_brightness,
    'volume': draw_volume,
    'focus': draw_focus,
    'darkmode': draw_darkmode,
    'mirror': draw_mirror,
    'keyboard': draw_keyboard,
    'play': draw_play,
    'pause': draw_pause,
    'prev': draw_prev,
    'next': draw_next,
    'bell': draw_bell,
    'grid': draw_grid,
    'settings': draw_settings,
    'terminal': draw_terminal,
    'folder': draw_folder,
    'search': draw_search,
    'close': draw_close,
    'app': draw_app,
    'file': draw_file,
}

def write_nxib(filepath, canvas):
    """Write NXIB file"""
    with open(filepath, 'wb') as f:
        # Header
        f.write(NXIB_MAGIC)
        f.write(struct.pack('<B', NXIB_VERSION))
        f.write(struct.pack('<H', canvas.size))
        f.write(struct.pack('<H', canvas.size))
        f.write(struct.pack('<B', 0))  # flags: 0=RGBA
        # Pixel data
        f.write(canvas.to_bytes())

def main():
    output_dir = sys.argv[1] if len(sys.argv) > 1 else '/home/swana/Documents/NEOLYXOS/neolyx-os/System/Icons'
    
    os.makedirs(output_dir, exist_ok=True)
    
    # Default foreground color (white-ish)
    color = 0xFFFFFFFF
    
    print(f"Generating NXIB icons to {output_dir}...")
    
    total = 0
    for name, draw_func in DRAW_FUNCS.items():
        for size in SIZES:
            canvas = IconCanvas(size)
            draw_func(canvas, color)
            
            # Write size-specific file
            filepath = os.path.join(output_dir, f"{name}_{size}.nxi")
            write_nxib(filepath, canvas)
            total += 1
        
        # Also write a default (32px)
        canvas = IconCanvas(32)
        draw_func(canvas, color)
        filepath = os.path.join(output_dir, f"{name}.nxi")
        write_nxib(filepath, canvas)
        total += 1
    
    print(f"Generated {total} icon files.")

if __name__ == '__main__':
    main()
