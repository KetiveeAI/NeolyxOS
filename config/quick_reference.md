# Neolyx OS Quick Reference Card

App extention is end with 

## 🚀 Most Common Extensions

| Extension | Type | Description |
|-----------|------|-------------|
| `.sys` | Executable | Terminal/System executable |
| `.npa` | Package | Application package installer |
| `.nix` | Icon | Neolyx icon format |
| `.nlc` | Config | Configuration file |
| `.nmd` | Doc | Documentation (Markdown) |
| `.nrs` | Source | Rust source code |
| `.ncc` | Source | C/C++ source code |
| `.npy` | Source | Python script |


## 🛠️ System Commands

| Command | Purpose | Example |
|---------|---------|---------|
| `sys` | System administration | `sys system info` |
| `ip` | Network management | `ip list` |
| `nano` | Text editor | `nano file.txt` |
| `sys` | Terminal customization | `sys theme set dark` |
| `npa` | Package management | `npa install app` |

## 📁 Important Directories

| Directory | Purpose |
|-----------|---------|
| `/system/bin/` | System executables (.sys) |
| `/apps/packages/` | Application packages (.npa) |
| `/etc/config/` | System configuration |
| `/apps/icons/` | Application icons (.nix) |
| `/var/log/` | System logs |

## 🎨 Terminal Extensions (sys)

```bash
# Theme management
sys theme list
sys theme set dark
sys theme reset

# Font management
sys font list
sys font set Consolas 14
sys font size 16

# Transparency control
sys transparency on
sys transparency 80
sys transparency off

# Tab management
sys tabs new
sys tabs switch 1
sys tabs close 2

# History management
sys history show
sys history search "command"
sys history clear

# Alias management
sys alias list
sys alias add ll "ls -la"
sys alias remove ll

# Plugin management
sys plugin list
sys plugin load syntax-highlighting
sys plugin unload weather
```

## 🔧 System Administration (sys)

```bash
# User management
sys user add admin
sys user list
sys user remove user1

# Service management
sys service start neolyx-terminal
sys service stop old-service
sys service status all

# System operations
sys system info
sys system update
sys system backup
sys system restore

# Network management
sys network status
sys network config
sys network test

# Security operations
sys security scan
sys security update
sys security audit

# Log management
sys logs view system.log
sys logs clear error.log
sys logs export all
```

## 🌐 Network Management (ip)

```bash
# List all interfaces
ip list

# Show interface details
ip show eth0

# Add IP address
ip add eth0 192.168.1.100

# Remove IP address
ip remove eth0 192.168.1.100

# Ping host
ip ping google.com
```

## 📦 Package Management (npa)

```bash
# Install package
npa install neolyx-terminal

# Remove package
npa remove old-app

# List installed packages
npa list

# Search for packages
npa search text-editor

# Update package
npa update neolyx-terminal

# Show package info
npa info neolyx-terminal

# Verify package
npa verify neolyx-terminal
```

## 📝 Text Editor (nano)

```bash
# Edit file
nano file.txt

# Edit with options
nano -w script.py
nano -i document.md
nano -c config.nlc

# Supported file types
nano document.nmd    # Markdown
nano script.nrs      # Rust
nano config.nlc      # Configuration
nano webpage.nht     # HTML
nano style.ncs       # CSS
nano data.njs        # JSON
```

## 🔐 Security Extensions

| Extension | Purpose |
|-----------|---------|
| `.nenc` | Encrypted files |
| `.ncert` | Certificates |
| `.nkey` | Keys |
| `.nsig` | Digital signatures |
| `.nhash` | Hash files |

## 💾 Backup Extensions

| Extension | Purpose |
|-----------|---------|
| `.nbk` | Full system backup |
| `.nrs` | System restore point |
| `.nsn` | System snapshot |
| `.ncl` | System clone |

## 🎯 Development Workflow

1. **Create source files** with appropriate extensions (`.nrs`, `.ncc`, `.npy`)
2. **Build executables** that output `.sys` files
3. **Package applications** as `.npa` files
4. **Create icons** in `.nix` format
5. **Document** using `.nmd` files
6. **Configure** using `.nlc` files

## 📋 File Permissions

| Type | Permission | Octal |
|------|------------|-------|
| Executables | rwxr-xr-x | 755 |
| Configuration | rw-r--r-- | 644 |
| Sensitive config | rw------- | 600 |
| System files | rw-r--r-- | 644 |

## 🔄 Version Information

- **Neolyx OS**: 1.0.0
- **Kernel**: 1.0.0
- **API**: 1.0
- **Extension Format**: 1.0

## 📞 Emergency Commands

```bash
# System recovery
sys system restore

# Emergency terminal
sys emergency

# Safe mode
sys system safe-mode

# Reset configuration
sys config reset
```

---

*This quick reference is part of Neolyx OS and is licensed under the KetiveeAI License.* 