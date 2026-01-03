# NeolyxOS Calculator.app

System calculator application for NeolyxOS with basic, scientific, and programmer modes.

## Features

- **Basic Mode**: Standard arithmetic operations (+, -, ×, ÷)
- **Scientific Mode**: Trigonometry, logarithms, powers, roots, constants
- **Programmer Mode**: Hex/Dec/Oct/Bin conversion, bitwise operations
- **Memory Functions**: M+, M-, MR, MC, MS
- **History Tracking**: Recall previous calculations
- **Keyboard Support**: Full keyboard input for quick entry

## Directory Structure

```
Calculator.app/
├── main.c              # Application entry point
├── calc.h              # Common definitions
├── nxrender.h          # NXRender API wrapper
├── manifest.npa        # App manifest
├── Makefile
├── engine.c            # Core calculation logic
├── display.c           # Display rendering
├── buttons.c           # Button layouts and rendering
├── scientific.c        # Scientific functions
├── history.c           # Calculation history
├── resources/          # App resources
│   └── calculator.nxi  # App icon
├── bin/                # Compiled output
└── lib/                # Libraries
```

## Build

```bash
cd apps/Calculator.app
make clean
make
```

## Keyboard Shortcuts

| Key | Action |
|-----|--------|
| 0-9 | Enter digit |
| . | Decimal point |
| + | Add |
| - | Subtract |
| * | Multiply |
| / | Divide |
| Enter | Calculate result |
| Escape | Clear all |
| Backspace | Delete last digit |
| % | Percent |

## Scientific Functions

- **Trigonometry**: sin, cos, tan, asin, acos, atan
- **Hyperbolic**: sinh, cosh, tanh
- **Logarithms**: log, ln, log₂
- **Powers**: x², x³, xʸ, eˣ, 10ˣ
- **Roots**: √x, ∛x, ʸ√x
- **Constants**: π, e
- **Other**: n!, 1/x, |x|, floor, ceil, round, random

## Programmer Functions

- **Bitwise**: AND, OR, XOR, NOT, <<, >>
- **Bases**: Hex, Dec, Oct, Bin conversion
- **Modulo**: MOD operation

## License

Copyright (c) 2025 KetiveeAI. All Rights Reserved. Proprietary License.
