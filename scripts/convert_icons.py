#!/usr/bin/env python3
"""
NXI Icon Converter - Convert SVG icons to NeolyxOS .nxi vector format

NXI is a text-based vector format (like simplified SVG).

NXI Format:
  [nxi] - metadata (version, name, size)
  [canvas] - dimensions
  [colors] - color definitions
  [paths] - SVG path data
  [style] - fill/stroke assignments

Usage:
  python3 convert_icons_nxi.py /path/to/svg_dir /path/to/output_dir

Copyright (c) 2025 KetiveeAI
"""

import sys
import os
import re
from xml.etree import ElementTree as ET

def parse_svg(svg_path):
    """Parse SVG file and extract paths and colors"""
    tree = ET.parse(svg_path)
    root = tree.getroot()
    
    # Handle namespaces
    ns = {'svg': 'http://www.w3.org/2000/svg'}
    
    # Get viewbox
    viewbox = root.get('viewBox', '0 0 24 24')
    width = root.get('width', '24')
    height = root.get('height', '24')
    
    # Clean up dimensions
    width = re.sub(r'[^0-9]', '', str(width)) or '24'
    height = re.sub(r'[^0-9]', '', str(height)) or '24'
    
    paths = []
    colors = set()
    
    # Find all path elements
    for elem in root.iter():
        tag = elem.tag.split('}')[-1]  # Remove namespace
        
        if tag == 'path':
            d = elem.get('d', '')
            fill = elem.get('fill', 'currentColor')
            stroke = elem.get('stroke', 'none')
            
            if d:
                path_id = f"path_{len(paths)}"
                paths.append({
                    'id': path_id,
                    'd': d,
                    'fill': fill,
                    'stroke': stroke
                })
                if fill and fill != 'none':
                    colors.add(fill)
                if stroke and stroke != 'none':
                    colors.add(stroke)
        
        elif tag == 'circle':
            cx = elem.get('cx', '0')
            cy = elem.get('cy', '0')
            r = elem.get('r', '0')
            fill = elem.get('fill', 'currentColor')
            
            path_id = f"circle_{len(paths)}"
            paths.append({
                'id': path_id,
                'd': f"M{float(cx)-float(r)},{cy} a{r},{r} 0 1,0 {float(r)*2},0 a{r},{r} 0 1,0 -{float(r)*2},0",
                'fill': fill,
                'stroke': 'none'
            })
            if fill and fill != 'none':
                colors.add(fill)
        
        elif tag == 'rect':
            x = float(elem.get('x', '0'))
            y = float(elem.get('y', '0'))
            w = float(elem.get('width', '0'))
            h = float(elem.get('height', '0'))
            fill = elem.get('fill', 'currentColor')
            
            path_id = f"rect_{len(paths)}"
            paths.append({
                'id': path_id,
                'd': f"M{x},{y} L{x+w},{y} L{x+w},{y+h} L{x},{y+h} Z",
                'fill': fill,
                'stroke': 'none'
            })
            if fill and fill != 'none':
                colors.add(fill)
    
    return {
        'viewbox': viewbox,
        'width': width,
        'height': height,
        'paths': paths,
        'colors': colors
    }

def svg_to_nxi(svg_path, output_path, name=None):
    """Convert SVG to NXI format"""
    
    if name is None:
        name = os.path.splitext(os.path.basename(svg_path))[0]
        name = name.replace('neolyx-', '')  # Remove prefix
    
    try:
        data = parse_svg(svg_path)
    except Exception as e:
        print(f"  ✗ Parse error: {e}")
        return False
    
    lines = []
    lines.append("# NeolyxOS NXI Vector Icon Format v1.0")
    lines.append(f"# {name} icon")
    lines.append("")
    
    # [nxi] section
    lines.append("[nxi]")
    lines.append("version = 1.0")
    lines.append(f"name = {name}")
    lines.append(f"size = {data['width']}")
    lines.append("author = KetiveeAI")
    lines.append("category = system")
    lines.append(f"viewbox = {data['viewbox']}")
    lines.append("")
    
    # [canvas] section
    lines.append("[canvas]")
    lines.append(f"width = {data['width']}")
    lines.append(f"height = {data['height']}")
    lines.append("background = transparent")
    lines.append("")
    
    # [colors] section
    lines.append("[colors]")
    lines.append("primary = #FFFFFF")
    lines.append("accent = #00B4FF")
    
    color_map = {}
    for i, color in enumerate(data['colors']):
        if color and color != 'none' and color != 'currentColor':
            color_name = f"color_{i}"
            color_map[color] = color_name
            lines.append(f"{color_name} = {color}")
    lines.append("")
    
    # [paths] section
    lines.append("[paths]")
    for path in data['paths']:
        lines.append(f"{path['id']} = {path['d']}")
    lines.append("")
    
    # [style] section
    lines.append("[style]")
    for path in data['paths']:
        fill = path['fill']
        if fill == 'currentColor':
            fill = 'primary'
        elif fill in color_map:
            fill = color_map[fill]
        elif fill == 'none':
            fill = 'transparent'
        else:
            fill = 'primary'
        lines.append(f"{path['id']}.fill = {fill}")
    lines.append("")
    
    # Write output
    with open(output_path, 'w') as f:
        f.write('\n'.join(lines))
    
    return True

def batch_convert(input_dir, output_dir):
    """Convert all SVG files in directory"""
    
    os.makedirs(output_dir, exist_ok=True)
    
    svg_files = [f for f in os.listdir(input_dir) if f.endswith('.svg')]
    total = len(svg_files)
    converted = 0
    
    print(f"Converting {total} SVG files to NXI...")
    
    for svg_file in svg_files:
        svg_path = os.path.join(input_dir, svg_file)
        base_name = os.path.splitext(svg_file)[0].replace('neolyx-', '')
        nxi_path = os.path.join(output_dir, f"{base_name}.nxi")
        
        if svg_to_nxi(svg_path, nxi_path, base_name):
            print(f"  ✓ {svg_file} → {base_name}.nxi")
            converted += 1
        else:
            print(f"  ✗ {svg_file} failed")
    
    print(f"\nDone! Converted {converted}/{total} icons.")

def main():
    if len(sys.argv) < 3:
        print("Usage: python3 convert_icons_nxi.py <input_dir> <output_dir>")
        sys.exit(1)
    
    input_dir = sys.argv[1]
    output_dir = sys.argv[2]
    
    batch_convert(input_dir, output_dir)

if __name__ == '__main__':
    main()
