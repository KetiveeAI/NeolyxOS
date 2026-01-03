# ReoX Programming Language Specification

## Overview
ReoX is a custom programming language designed specifically for Neolyx OS development. It combines the power of C, Rust, and modern language features with direct OS integration.

## File Extension
- **Primary**: `.reox` (ReoX source files)
- **Header**: `.reoxh` (ReoX header files)
- **Library**: `.reoxlib` (ReoX library files)

## Language Features

### 1. Basic Syntax
```reox
// Single line comment
/* Multi-line comment */

// Variable declaration
let x: int = 42;
let name: string = "Neolyx";
let flag: bool = true;

// Function definition
fn main() -> int {
    print("Hello, Neolyx OS!");
    return 0;
}

// OS-specific functions
fn kernel_call(api: string, params: map) -> any {
    return os::kernel::call(api, params);
}
```

### 2. Data Types
```reox
// Primitive types
int x = 42;           // 64-bit integer
float y = 3.14;       // 64-bit float
bool flag = true;     // Boolean
string text = "Hello"; // String
char c = 'A';         // Character

// Complex types
array<int> numbers = [1, 2, 3, 4, 5];
map<string, any> config = {"name": "reolab", "version": 1.0};
struct Point {
    x: float,
    y: float
};

// OS-specific types
kernel::process_t process;
kernel::memory_t memory;
kernel::file_t file;
```

### 3. Control Structures
```reox
// Conditional statements
if (x > 0) {
    print("Positive");
} else if (x < 0) {
    print("Negative");
} else {
    print("Zero");
}

// Loops
for (let i = 0; i < 10; i++) {
    print(i);
}

while (condition) {
    // Loop body
}

// Pattern matching
match value {
    1 => print("One"),
    2 => print("Two"),
    _ => print("Other")
}
```

### 4. OS Integration
```reox
// Direct kernel calls
let process_id = kernel::create_process("my_app");
let memory = kernel::allocate_memory(1024);
let file = kernel::open_file("/home/user/file.txt");

// System calls
let result = syscall::read(file, buffer, size);
let status = syscall::write(file, data, length);

// Hardware access
let cpu_info = hardware::cpu::get_info();
let memory_info = hardware::memory::get_info();
```

### 5. Error Handling
```reox
// Result type (similar to Rust)
fn read_file(path: string) -> Result<string, Error> {
    let file = kernel::open_file(path);
    if (file.is_error()) {
        return Err(file.error());
    }
    return Ok(file.read_all());
}

// Exception handling
try {
    let data = read_file("/nonexistent.txt");
    print(data);
} catch (error) {
    print("Error: " + error.message);
}
```

### 6. Modules and Imports
```reox
// Import standard library
import std::io;
import std::string;

// Import OS modules
import kernel::process;
import kernel::memory;
import hardware::cpu;

// Custom modules
import my_module;
```

### 7. Concurrency
```reox
// Thread creation
let thread = spawn {
    print("Running in thread");
    return 42;
};

// Async/await
async fn fetch_data() -> string {
    let response = await http::get("https://api.example.com");
    return response.body;
}

// Channels
let (sender, receiver) = channel<string>();
sender.send("Hello from thread");
let message = receiver.recv();
```

## Compilation Process

### 1. Lexical Analysis
- Tokenizes source code
- Handles comments and whitespace
- Identifies keywords and operators

### 2. Syntax Analysis
- Parses tokens into AST (Abstract Syntax Tree)
- Validates syntax rules
- Reports syntax errors

### 3. Semantic Analysis
- Type checking
- Symbol resolution
- Scope validation

### 4. Code Generation
- Generates intermediate representation
- Optimizes code
- Produces executable binary

## Standard Library

### Core Modules
- `std::io` - Input/Output operations
- `std::string` - String manipulation
- `std::array` - Array operations
- `std::map` - Map/dictionary operations
- `std::time` - Time and date functions

### OS Modules
- `kernel::process` - Process management
- `kernel::memory` - Memory management
- `kernel::file` - File system operations
- `kernel::network` - Network operations
- `hardware::cpu` - CPU information
- `hardware::memory` - Memory information

## Example Programs

### Hello World
```reox
fn main() -> int {
    print("Hello, Neolyx OS!");
    return 0;
}
```

### File Operations
```reox
import kernel::file;
import std::io;

fn main() -> int {
    let file = kernel::file::open("/home/user/test.txt", "w");
    file.write("Hello from ReoX!");
    file.close();
    
    let content = kernel::file::read_all("/home/user/test.txt");
    print(content);
    return 0;
}
```

### Process Management
```reox
import kernel::process;
import std::time;

fn main() -> int {
    let process = kernel::process::create("my_app");
    process.start();
    
    while (process.is_running()) {
        print("Process is running...");
        std::time::sleep(1000); // Sleep 1 second
    }
    
    print("Process finished with code: " + process.exit_code());
    return 0;
}
```

## Integration with ReoLab IDE

The ReoLab IDE provides:
- Syntax highlighting for .reox files
- Code completion and IntelliSense
- Integrated debugging
- AI-powered code suggestions
- Direct compilation and execution
- Integration with Neolyx OS kernel APIs

## Future Extensions

- WebAssembly compilation target
- Cross-platform compatibility
- Advanced optimization passes
- Package management system
- Standard library expansion 