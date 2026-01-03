// NeolyxOS Documentation - Navbar Component

(function () {
    // Calculate relative path to docs root based on current page depth
    function getBasePath() {
        const path = window.location.pathname;
        const depth = (path.match(/\//g) || []).length - (path.split('/docs/')[1] || '').split('/').length + 1;
        if (path.includes('/docs/reox/') || path.includes('/docs/architecture/') ||
            path.includes('/docs/kernel/') || path.includes('/docs/drivers/') ||
            path.includes('/docs/security/') || path.includes('/docs/filesystem/') ||
            path.includes('/docs/stability/') || path.includes('/docs/apps/') ||
            path.includes('/docs/developers/')) {
            return '../';
        }
        return '';
    }

    const base = getBasePath();

    // Documentation search index
    // Documentation search index
    const docs = [
        { title: "Project Status", desc: "What's implemented, partial, stub, or missing", url: base + "project_status.html" },
        { title: "Boot Roadmap", desc: "8-phase plan to boot NeolyxOS with GUI", url: base + "boot_roadmap.html" },
        { title: "NXGame Bridge", desc: "Direct hardware access for games/creative apps", url: base + "stability/nxgame_bridge.html" },
        { title: "System Integrity Protection", desc: "Protect system files from modification", url: base + "security/sip.html" },
        { title: "Syscalls Reference", desc: "Complete syscall list with NXGame Bridge", url: base + "kernel/syscalls.html" },
        { title: "System Overview", desc: "Full system stack, directory structure", url: base + "architecture/overview.html" },
        { title: "Filesystem Hierarchy", desc: "/System, /Applications, /Users, /Volumes", url: base + "architecture/filesystem.html" },
        { title: "Userspace Memory", desc: "ELF loading, BSS, Heap, Stack layout (Ring 3)", url: base + "kernel/userspace_ram.html" },
        { title: "User Directories", desc: "Desktop, Documents, Public folder", url: base + "architecture/user_dirs.html" },
        { title: "REOX Language", desc: "NeolyxOS native programming language", url: base + "reox/index.html" },
        { title: "REOX Syntax", desc: "Variables, functions, kinds, pattern matching", url: base + "reox/syntax.html" },
        { title: "REOX UI System", desc: "Layers, Panels, @Bind, widgets, layouts", url: base + "reox/ui.html" },
        { title: "REOX Examples", desc: "Calculator, Todo, System Monitor, File Browser", url: base + "reox/examples.html" },
        { title: "REOX API", desc: "Standard library, system APIs, C FFI", url: base + "reox/api.html" },
        { title: "GPU Driver", desc: "Mode-setting, buffer, accelerated 2D", url: base + "drivers/gpu.html" },
        { title: "GPU 3D Rendering", desc: "OpenGL-like pipeline, shaders, vertex", url: base + "drivers/gpu_3d.html" },
        { title: "Audio Driver", desc: "PCM ring buffer, 44.1kHz, mixing", url: base + "drivers/audio.html" },
        { title: "KDRV Framework", desc: "Kernel driver framework", url: base + "drivers/kdrv.html" },
        { title: "Virtual File System", desc: "VFS nodes, mount, path resolution", url: base + "filesystem/vfs.html" },
        { title: "NXFS Native FS", desc: "Extents, encryption, checksums", url: base + "filesystem/nxfs.html" },
        { title: "Security Hardening", desc: "Capability-based access control", url: base + "security/security_hardening.html" },
        { title: "NXRender GUI", desc: "Pure C GUI framework, widgets, layout, compositor", url: base + "gui/nxrender.html" },
        { title: "Animation System", desc: "Springs, bezier curves, keyframes, timelines", url: base + "gui/animation.html" },
        { title: "3D Lighting", desc: "Blinn-Phong, Fresnel, materials, reflections", url: base + "gui/lighting.html" },
        { title: "GPU Driver Interface", desc: "Command buffers, VSync, textures", url: base + "gui/gpu_driver.html" },
        { title: "NXTouch API", desc: "80+ touch gestures, stylus, palm rejection, multi-touch", url: base + "drivers/nxtouch_api.html" },
        { title: "NXBacklight API", desc: "Laptop brightness, ambient sensor, keyboard backlight", url: base + "drivers/nxbacklight_api.html" },
        { title: "Display Advanced API", desc: "DDC/CI, Adaptive Refresh, Gaming Mode, Night Light", url: base + "drivers/nxdisplay_advanced_api.html" },
        { title: "Settings Drivers API", desc: "Unified Settings app driver interface", url: base + "drivers/settings_drivers_api.html" },
        { title: "Network Overview", desc: "Network subsystem overview", url: base + "network/index.html" },
        { title: "TCP/IP Stack", desc: "Socket API, protocols, firewall, DHCP", url: base + "network/tcpip_stack.html" },
        { title: "Network Drivers", desc: "Ethernet and WiFi driver support", url: base + "network/drivers.html" },
    ];

    // Create navbar HTML
    function createNavbar() {
        const nav = document.createElement('nav');
        nav.className = 'nx-navbar';
        nav.innerHTML = `
            <a href="${base}index.html" class="nx-navbar-brand">🚀 NeolyxOS Docs</a>
            <ul class="nx-navbar-links">
                <li><a href="${base}architecture/overview.html">Architecture</a></li>
                <li><a href="${base}kernel/syscalls.html">Kernel</a></li>
                <li><a href="${base}drivers/kdrv.html">Drivers</a></li>
                <li><a href="${base}reox/index.html">REOX</a></li>
                <li><a href="${base}architecture/applications.html">Apps</a></li>
            </ul>
            <div class="nx-search-container">
                <svg class="nx-search-icon" width="14" height="14" viewBox="0 0 52 52" fill="none">
                    <path fill-rule="evenodd" clip-rule="evenodd" d="M35.4381 30.948L40.271 35.7811C40.271 35.7811 38.4405 36.6873 37.5639 37.5641C36.6871 38.4407 35.7811 40.271 35.7811 40.271L30.948 35.4381C27.7236 37.7583 23.8363 39.1747 19.5873 39.1747C8.76906 39.1747 0 30.4056 0 19.5873C0 8.76909 8.76908 0 19.5873 0C30.4056 0 39.1747 8.76907 39.1747 19.5873C39.1747 23.8363 37.7583 27.7236 35.4381 30.948ZM19.5873 6.0269C12.0839 6.02686 6.02687 12.0839 6.02691 19.5873C6.02687 27.0907 12.0839 33.1478 19.5873 33.1478C27.0907 33.1478 33.1478 27.0907 33.1478 19.5873C33.1478 12.0839 27.0907 6.02692 19.5873 6.0269Z" fill="#6e7681"/>
                    <path d="M38.8249 46.6193L41.7941 49.5886C44.049 51.8435 47.7048 51.8435 49.9597 49.5886C52.2146 47.3337 52.029 43.8635 49.7741 41.6086L46.8048 38.6394C44.5501 36.3845 40.8942 36.3845 38.6393 38.6394C36.3844 40.8941 36.57 44.3646 38.8249 46.6193Z" fill="#6e7681"/>
                </svg>
                <input type="text" class="nx-search-input" id="nxSearchInput" placeholder="Search... (Ctrl+K)" autocomplete="off">
                <div class="nx-search-results" id="nxSearchResults"></div>
            </div>
        `;
        return nav;
    }

    // Initialize search functionality
    function initSearch() {
        const input = document.getElementById('nxSearchInput');
        const results = document.getElementById('nxSearchResults');
        if (!input || !results) return;

        input.addEventListener('input', function () {
            const query = this.value.toLowerCase().trim();
            if (query.length < 2) {
                results.classList.remove('active');
                return;
            }

            const matches = docs.filter(doc =>
                doc.title.toLowerCase().includes(query) ||
                doc.desc.toLowerCase().includes(query)
            );

            if (matches.length === 0) {
                results.innerHTML = '<div class="nx-no-results">No results found</div>';
            } else {
                results.innerHTML = matches.slice(0, 8).map(doc => `
                    <div class="nx-search-result-item" onclick="window.location.href='${doc.url}'">
                        <div class="nx-search-result-title">${doc.title}</div>
                        <div class="nx-search-result-desc">${doc.desc}</div>
                    </div>
                `).join('');
            }
            results.classList.add('active');
        });

        input.addEventListener('blur', () => setTimeout(() => results.classList.remove('active'), 200));
        input.addEventListener('focus', function () { if (this.value.length >= 2) results.classList.add('active'); });

        document.addEventListener('keydown', (e) => {
            if ((e.ctrlKey && e.key === 'k') || (e.key === '/' && document.activeElement !== input)) {
                e.preventDefault();
                input.focus();
            }
            if (e.key === 'Escape') input.blur();
        });
    }

    // Auto-inject navbar when script loads
    document.addEventListener('DOMContentLoaded', function () {
        // Add CSS if not already added
        if (!document.querySelector('link[href*="navbar.css"]')) {
            const link = document.createElement('link');
            link.rel = 'stylesheet';
            link.href = base + 'assets/navbar.css';
            document.head.appendChild(link);
        }

        // Add navbar
        document.body.insertBefore(createNavbar(), document.body.firstChild);
        document.body.classList.add('nx-has-navbar');

        // Initialize search
        initSearch();
    });
})();
