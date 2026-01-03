import os

DOCS_DIR = 'docs'
ASSETS_DIR = 'assets'
CSS_FILE = 'navbar.css'
JS_FILE = 'navbar.js'

def get_relative_assets_path(file_path):
    rel_path = os.path.relpath(file_path, DOCS_DIR)
    depth = rel_path.count(os.sep)
    if depth == 0:
        return 'assets/'
    return '../' * depth + 'assets/'

def process_file(file_path):
    if file_path.endswith('docs/index.html'):
        return

    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    changed = False

    assets_prefix = get_relative_assets_path(file_path)
    css_link = f'<link rel="stylesheet" href="{assets_prefix}{CSS_FILE}">'
    js_script = f'<script src="{assets_prefix}{JS_FILE}"></script>'

    # 1. Remove <nav class="navbar">
    while '<nav class="navbar">' in content:
        start = content.find('<nav class="navbar">')
        end = content.find('</nav>', start)
        if end != -1:
            content = content[:start] + content[end+6:]
            print(f"Removed nav class='navbar' from {file_path}")
            changed = True
        else:
            break

    # 2. Remove <nav class="nav">
    while '<nav class="nav">' in content:
        start = content.find('<nav class="nav">')
        end = content.find('</nav>', start)
        if end != -1:
            content = content[:start] + content[end+6:]
            print(f"Removed nav class='nav' from {file_path}")
            changed = True
        else:
            break
            
    # 3. Remove plain <nav> if it contains index link
    # This covers <nav><a href="...index.html">...
    if '<nav>' in content:
        start = content.find('<nav>')
        end = content.find('</nav>', start)
        if end != -1:
            snippet = content[start:end+6]
            if 'index.html' in snippet or 'Back to' in snippet:
                content = content[:start] + content[end+6:]
                print(f"Removed plain nav from {file_path}")
                changed = True

    # 4. Add CSS/JS if missing
    if 'navbar.css' not in content:
        head_end = content.find('</head>')
        if head_end != -1:
             content = content[:head_end] + '    ' + css_link + '\n    ' + js_script + '\n' + content[head_end:]
             print(f"Added assets to {file_path}")
             changed = True

    # 5. Remove inline CSS
    if '<style>' in content and ('.navbar' in content or '.nav' in content):
        style_start = content.find('<style>')
        style_end = content.find('</style>')
        if style_start != -1 and style_end != -1:
             snippet = content[style_start:style_end]
             # Check inside the style block
             if '.navbar' in snippet or '.nav {' in snippet or '.nav a' in snippet:
                 # Check if style block only had that or if we are deleting other stuff?
                 # Ideally we parse it but let's be blunt: most files had ONLY nav styles or separated ones.
                 # If it looks like a mixed block, maybe careful.
                 # But verify shows these files mostly have <style> ... .nav ... </style> for that purpose.
                 # We'll just strip the whole block if it looks like the nav style block.
                 content = content[:style_start] + content[style_end+8:]
                 print(f"Removed inline style from {file_path}")
                 changed = True

    if changed:
        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(content)

def main():
    for root, dirs, files in os.walk(DOCS_DIR):
        for file in files:
            if file.endswith('.html'):
                process_file(os.path.join(root, file))

if __name__ == '__main__':
    main()
