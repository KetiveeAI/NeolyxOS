# REOX Language Reference

**Version:** 1.0  
**Target:** NeolyxOS System UI & Low-Level Applications  
**Philosophy:** Simpler than C++, safer than C, designed for OS development

---

## Unique REOX Terminology

| REOX Term | Equivalent | Purpose |
|-----------|------------|---------|
| **kind** | enum | Tagged union with variants |
| **layer** | view/widget | UI component |
| **panel** | window | Top-level UI container |
| **action** | closure | Lambda function |
| **maybe** | optional | Nullable type (`?`) |
| **@Bind** | @State | Reactive state binding |
| **emit** | event | Event dispatch |
| **signal** | observable | Reactive binding |

---

## Syntax Reference

### Variables
```reox
let x: int = 42;           // Immutable
let mut y: int = 10;       // Mutable
let name: string = "NeolyxOS";
let active: bool = true;
```

### Functions
```reox
fn add(a: int, b: int) -> int {
    return a + b;
}

// Short form
fn multiply(a: int, b: int) -> int = a * b;

// Async function
async fn fetch_data(url: string) -> string {
    // ...
}
```

### Kinds (Enums)
```reox
kind Color {
    Red,
    Green,
    Blue,
    Rgb(int, int, int)
}

let c: Color = Color::Rgb(255, 128, 0);
```

### Pattern Matching
```reox
match color {
    when Color::Red => print("Red"),
    when Color::Rgb(r, g, b) => print("RGB: {r}, {g}, {b}"),
    _ => print("Other")
}
```

### Structs
```reox
struct User {
    id: int,
    name: string,
    email: string
}

let user = User {
    id: 1,
    name: "Swana",
    email: "swana@neolyx.os"
};
```

### Maybe (Optional)
```reox
let maybe_value: maybe int = find_user(42);

if let Some(user) = maybe_value {
    print("Found: " + user.name);
}
```

### Actions (Closures)
```reox
let greet = action(name: string) -> string {
    return "Hello, " + name + "!";
};

let result = greet("NeolyxOS");
```

---

## UI System (Layers & Panels)

### Basic Layer
```reox
layer Greeting {
    text("Hello, NeolyxOS!")
        .color(0x58A6FF)
        .size(24)
}
```

### Panel (Window)
```reox
panel Calculator {
    title: "Calculator"
    size: (400, 300)
    
    @Bind result: string = "0"
    
    vstack(gap: 12) {
        text(result)
            .font_size(32)
            .align(.right)
        
        hstack(gap: 8) {
            button("7").on_click(action { append("7") })
            button("8").on_click(action { append("8") })
            button("9").on_click(action { append("9") })
        }
        
        hstack(gap: 8) {
            button("=").on_click(action { calculate() })
            button("C").on_click(action { clear() })
        }
    }
}
```

### Built-in UI Components
| Component | Description |
|-----------|-------------|
| `text(string)` | Text display |
| `button(string)` | Clickable button |
| `input()` | Text input field |
| `checkbox(bool)` | Toggle checkbox |
| `slider(min, max)` | Value slider |
| `vstack()` | Vertical layout |
| `hstack()` | Horizontal layout |
| `zstack()` | Layered layout |
| `scroll()` | Scrollable container |
| `divider()` | Visual separator |

### Modifiers
```reox
text("Hello")
    .color(0xFFFFFF)
    .background(0x1E1E1E)
    .padding(16)
    .corner_radius(8)
    .shadow(4)
    .on_click(action { ... })
```

---

## Standard Library

### Core Functions
```reox
print(args...)           // Print to console
len(collection) -> int   // Get length
type(value) -> string    // Get type name
```

### Math
```reox
import std.math

abs(x)       // Absolute value
min(a, b)    // Minimum
max(a, b)    // Maximum
pow(x, n)    // Power
sqrt(x)      // Square root
```

### Strings
```reox
import std.string

split(s, delim) -> [string]
join(arr, sep) -> string
trim(s) -> string
contains(s, substr) -> bool
```

### Arrays
```reox
let arr: [int] = [1, 2, 3, 4, 5];
arr.push(6);
arr.pop();
arr.map(action(x) { x * 2 });
arr.filter(action(x) { x > 2 });
```

---

## NeolyxOS System Integration

### Extern Functions (C FFI)
```reox
extern "C" {
    fn nx_gpu_fill_rect(x: int, y: int, w: int, h: int, color: int);
    fn nx_audio_play(buffer: [int], len: int);
}
```

### System Signals
```reox
signal window_resize(width: int, height: int);

panel App {
    emit window_resize(800, 600);
}
```

---

## Build & Run

```bash
# Compile REOX to C
reoxc build app.rx -o app.c

# Compile with NXRender
reoxc build app.rx --ui -o app

# Run interpreter
reoxc run app.rx

# REPL mode
reoxc repl
```

---

**Copyright (c) 2025 KetiveeAI - Open Source**
