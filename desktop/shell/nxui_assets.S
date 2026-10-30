/*
 * NeolyxOS - NXUI embedded assets
 *
 * System typefaces linked into the desktop image (read-only data).
 * Paths are relative to desktop/, where the desktop Makefile runs.
 * Fonts: Source Sans 3 (NX Sans: Regular, Medium, Semibold, Bold,
 * Italic) and Cascadia Mono (NX Sans Mono), both under the SIL Open Font
 * License 1.1. Keep in sync with resources/fonts/nxsans-manifest.json.
 *
 * Copyright (c) 2025 KetiveeAI
 */

    .section .rodata.nxui_assets, "a"

    .balign 16
    .global nxui_font_regular, nxui_font_regular_end
nxui_font_regular:
    .incbin "../resources/fonts/NXSans/NXSans-Text.ttf"
nxui_font_regular_end:

    .balign 16
    .global nxui_font_medium, nxui_font_medium_end
nxui_font_medium:
    .incbin "../resources/fonts/NXSans/NXSans-UI.ttf"
nxui_font_medium_end:

    .balign 16
    .global nxui_font_bold, nxui_font_bold_end
nxui_font_bold:
    .incbin "../resources/fonts/NXSans/NXSans-Bold.ttf"
nxui_font_bold_end:

    .balign 16
    .global nxui_font_display, nxui_font_display_end
nxui_font_display:
    .incbin "../resources/fonts/NXSans/NXSans-Display.ttf"
nxui_font_display_end:

    .balign 16
    .global nxui_font_mono, nxui_font_mono_end
nxui_font_mono:
    .incbin "../resources/fonts/NXSansMono/NXSans-Mono.ttf"
nxui_font_mono_end:

    .balign 16
    .global nxui_font_italic, nxui_font_italic_end
nxui_font_italic:
    .incbin "../resources/fonts/NXSans/NXSans-Italic.ttf"
nxui_font_italic_end:

    .section .note.GNU-stack, "", @progbits
