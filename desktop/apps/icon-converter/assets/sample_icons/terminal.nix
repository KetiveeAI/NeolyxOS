# This is a placeholder for a .nix icon file
# The actual .nix file would contain binary data with the NIXICON\x01 header
# 
# To create a real .nix icon, use the icon-converter:
# icon-converter.exe terminal.svg terminal.nix --size 64x64
#
# The .nix format includes:
# - Magic header: NIXICON\x01
# - Version: 1
# - Width: 64 (4 bytes)
# - Height: 64 (4 bytes) 
# - Color depth: 32 (1 byte)
# - Frame count: 1 (4 bytes)
# - Animation delay: 0 (4 bytes)
# - Compression: 0 (1 byte)
# - Image data: 64x64x4 bytes of RGBA data 