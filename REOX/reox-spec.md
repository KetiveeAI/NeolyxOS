# Reox Language Specification v1.0

## Goals
- Simpler than C++, safer than C
- Built for NeolyxOS
- AI-native syntax
- Fast compilation
- Easy to learn

## Syntax Examples

### Hello World
```reox
fn main() {
    print("Hello from Reox!");
}
```

### Variables & Types
```reox
let x: int = 42;              // Immutable by default
let mut y: int = 10;          // Mutable with 'mut'
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
```

### Structures
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

### Pattern Matching
```reox
match command {
    "save" => save_file(),
    "load" => load_file(),
    "exit" => return,
    _ => print("Unknown command")
}
```

### UI Syntax (Swift-like)
```reox
window MainWindow {
    title: "Calculator"
    size: (400, 300)
    
    view {
        text_field input {
            placeholder: "Enter number"
        }
        
        button "Calculate" {
            on_click: {
                let result = calculate(input.text);
                output.text = result;
            }
        }
        
        label output {
            text: "Result: "
        }
    }
}
```

### AI Integration
```reox
ai assistant {
    model: "neolyx-gpt"
    
    fn suggest(context: string) -> string {
        return ai.prompt("Given context: {context}, suggest next action");
    }
}
```
```

#### **Week 3-4: Lexer (Tokenizer)**

**Opus Prompt for Lexer:**
```
I'm building Reox, a programming language for NeolyxOS (like Swift for macOS).

TASK: Create a complete lexer (tokenizer) for Reox language.

LANGUAGE FEATURES:
- Keywords: fn, let, mut, if, else, while, for, return, struct, match, ai, window
- Types: int, float, string, bool
- Operators: +, -, *, /, =, ==, !=, <, >, <=, >=, &&, ||
- Delimiters: {, }, (, ), [, ], ,, ;, :, ->
- Comments: // single line, /* multi line */
- Strings: "text" with escape sequences
- Numbers: 42, 3.14, 0xFF (hex)
- Identifiers: variable_name, functionName

IMPLEMENTATION:
Language: Rust (safe, fast, modern)
Output: Token stream for parser

FILE: reox/src/lexer/mod.rs

Provide:
1. Token enum with all token types
2. Lexer struct with state management
3. tokenize() function that returns Vec<Token>
4. Error handling for invalid tokens
5. Position tracking (line, column) for errors
6. Unit tests for each token type

Make it production-ready with excellent error messages.
```

#### **Week 5-6: Parser (AST Builder)**

**Opus Prompt for Parser:**
```
Create a parser for Reox language that builds an Abstract Syntax Tree.

TASK: Parse token stream into AST for compilation.

AST NODES NEEDED:
- Program (root)
- Function declarations
- Variable declarations
- Expressions (binary ops, function calls, literals)
- Statements (if, while, return, assignment)
- Type annotations
- Struct definitions

IMPLEMENTATION:
- Recursive descent parser
- Pratt parsing for expressions (operator precedence)
- Clear error messages with position info
- Recovery from syntax errors

FILE: reox/src/parser/mod.rs

Provide:
1. AST node enums/structs
2. Parser struct with methods for each grammar rule
3. parse() function: Vec<Token> -> AST
4. Error handling with helpful messages
5. Unit tests for each language construct

Focus on clarity and good error messages.
```

#### **Week 7-8: Type Checker**
```
Create type checker for Reox language.

TASK: Validate types and build symbol table.

FEATURES:
- Type inference where possible
- Type checking for operations
- Function signature validation
- Struct field validation
- Error for undefined variables
- Error for type mismatches

FILE: reox/src/typechecker/mod.rs

Provide complete type checking implementation.
```

#### **Week 9-12: Code Generation (LLVM)**
```
Create LLVM-based code generator for Reox.

TASK: Compile AST to native machine code via LLVM.

USE: llvm-sys Rust crate
TARGET: x86_64 NeolyxOS

GENERATE:
- Function definitions
- Variable allocations
- Arithmetic operations
- Control flow (if/while)
- Function calls
- Return statements

FILE: reox/src/codegen/mod.rs

Output: Executable ELF file for NeolyxOS.
```

---

### **Phase 2: Standard Library (Months 4-6)**

#### **Core Modules**
```
reox/stdlib/
├── core/
│   ├── print.reox       # Output functions
│   ├── string.reox      # String operations
│   ├── math.reox        # Math functions
│   └── mem.reox         # Memory operations
├── io/
│   ├── file.reox        # File I/O
│   ├── network.reox     # Network operations
│   └── serial.reox      # Serial I/O
├── ui/
│   ├── window.reox      # Window management
│   ├── controls.reox    # UI controls
│   └── graphics.reox    # Drawing primitives
└── ai/
    ├── assistant.reox   # AI integration
    └── ml.reox          # ML utilities
```

**Opus Prompt for Stdlib:**
```
Create standard library for Reox language.

TASK: Implement core functionality in Reox.

MODULES:
1. core/print.reox
   - print(text: string)
   - println(text: string)
   - printf(format: string, ...args)

2. core/string.reox
   - length(s: string) -> int
   - concat(a: string, b: string) -> string
   - substring(s: string, start: int, end: int) -> string
   - split(s: string, delim: string) -> [string]

3. core/math.reox
   - abs(x: float) -> float
   - sqrt(x: float) -> float
   - pow(base: float, exp: float) -> float
   - sin, cos, tan, etc.

Provide implementations that compile to LLVM IR.



Goal: Rewrite Reox compiler in Reox itself!
reox// reox/compiler/main.reox
// The Reox compiler, written in Reox!

fn main() {
    let args = os.args();
    
    if args.len() < 2 {
        print("Usage: reoxc <file.reox>");
        return;
    }
    
    let source_file = args[1];
    let source = file.read(source_file);
    
    // Compile pipeline
    let tokens = lexer.tokenize(source);
    let ast = parser.parse(tokens);
    let checked_ast = typechecker.check(ast);
    let ir = codegen.generate(checked_ast);
    let output = linker.link(ir);
    
    file.write("output.elf", output);
    print("Compilation successful!");
}
```

---

### **Phase 4: IDE & Tools (Months 10-12)**

#### **Reox Language Server (LSP)**
```
reox-lsp/
├── completion.rs       # Auto-completion
├── diagnostics.rs      # Error/warning display
├── hover.rs           # Hover tooltips
├── goto_definition.rs # Jump to definition
└── formatting.rs      # Code formatting
Works with: VS Code, Neovim, Emacs, etc.
Package Manager (like npm/cargo)
bash# reoxpm - Reox Package Manager

reoxpm new my-app        # Create new project
reoxpm add ui-toolkit    # Add dependency
reoxpm build            # Build project
reoxpm run              # Run application
reoxpm test             # Run tests
reoxpm publish          # Publish to registry

🔄 DAILY WORKFLOW (While Building Both)
Monday & Tuesday: Kernel Focus
bash# Morning: Kernel work
cd /neolyx-os/kernel
make clean && make
# Debug kernel boot issues
# Add new drivers
# Fix memory management

# Afternoon: Test in QEMU
./test_boot.sh
# Analyze crashes
# Fix bugs
Wednesday & Thursday: Reox Focus
bash# Morning: Language development
cd /neolyx-os/apps/reolab
cargo build
# Work on compiler
# Add language features
# Write tests

# Afternoon: Documentation
# Write Reox tutorials
# Create example programs
# Update language spec
Friday: Integration Day
bash# Morning: Connect Reox to OS
# Test compiling Reox programs on NeolyxOS
# Fix integration issues
# Update build system

# Afternoon: Planning
# Review week progress
# Plan next week
# Update roadmap
```

### **Weekend: Learning & Experiments**
- Study Swift compiler design
- Research LLVM optimization
- Read OS development papers
- Experiment with new features

---

## 📚 **RESOURCES TO STUDY**

### **For Reox Language:**
1. **"Crafting Interpreters"** by Bob Nystrom (FREE online!)
   - Best book for learning language design
   - Covers lexer, parser, interpreter

2. **LLVM Tutorial** (official docs)
   - How to use LLVM for code generation
   - Step-by-step kaleidoscope tutorial

3. **Swift Compiler** (open source!)
   - Study how Apple built Swift
   - https://github.com/apple/swift
   - Your inspiration!

4. **Rust Compiler** (rustc)
   - Another modern compiler to learn from
   - Excellent error messages

### **For Kernel Development:**
1. **OSDev Wiki** (osdev.org)
   - Everything about OS development
   - Tutorials, examples, forums

2. **"Operating Systems: Three Easy Pieces"** (FREE!)
   - Modern OS textbook
   - Covers scheduling, memory, files

3. **Linux Kernel** (source code)
   - Reference for how things work
   - Don't copy, just learn concepts

---

## 🎯 **REOX vs SWIFT COMPARISON**

Since you want Reox like Swift:
```
Feature              Swift    Reox (Target)
─────────────────────────────────────────────
Syntax               ✓        ✓ (similar)
Type Safety          ✓        ✓
Type Inference       ✓        ✓
Pattern Matching     ✓        ✓
Optionals (?/!)      ✓        ✓
Closures/Lambdas     ✓        ✓
Structs              ✓        ✓
Classes              ✓        Maybe (later)
Generics             ✓        Maybe (later)
UI Builder (SwiftUI) ✓        ✓ (ReoxUI)
AI Integration       ✗        ✓ (UNIQUE!)
OS Integration       macOS    NeolyxOS
Compile Target       Native   Native (LLVM)
ABI Stability        ✓        ✓
```

---

## 🚀 **FIRST OPUS SESSION FOR REOX**

### **Start with this prompt:**
```
I'm creating Reox, a programming language for NeolyxOS (my custom operating system).

INSPIRATION: Swift (for macOS), but for NeolyxOS
GOAL: Modern, safe, easy language optimized for my OS
IMPLEMENTATION: Rust (for compiler), LLVM (for code generation)
TARGET: x86_64 and ARM64 native code

REOX DESIGN GOALS:
1. Simpler syntax than C/C++
2. Memory safe like Rust
3. Fast compilation
4. Native OS integration
5. Built-in UI syntax
6. AI-first features
7. Easy to learn

EXAMPLE REOX CODE:
```reox
// Simple function
fn greet(name: string) {
    print("Hello, {name}!");
}

// Struct
struct User {
    id: int,
    name: string
}

// Pattern matching
match status {
    "ok" => continue(),
    "error" => handle_error(),
    _ => unknown()
}

// UI (Swift-like)
window Calculator {
    title: "Calculator"
    
    view {
        text_field input
        button "=" { calculate() }
        label result
    }
}
```

TASK 1: ARCHITECTURE DESIGN

Design the complete Reox compiler architecture:

1. **Project Structure**
```
   reolab/
   ├── src/
   │   ├── main.rs          # Compiler entry point
   │   ├── lexer/           # Tokenizer
   │   ├── parser/          # AST builder
   │   ├── typechecker/     # Type validation
   │   ├── codegen/         # LLVM code generation
   │   ├── stdlib/          # Standard library
   │   └── runtime/         # Runtime support
   ├── tests/               # Compiler tests
   └── examples/            # Example Reox programs
```

2. **Compilation Pipeline**
```
   .reox source file
        ↓
   Lexer (tokenize)
        ↓
   Parser (build AST)
        ↓
   Type Checker (validate)
        ↓
   LLVM IR Generator
        ↓
   LLVM Optimizer
        ↓
   Machine Code (.o)
        ↓
   Linker
        ↓
   Executable (ELF)
```

3. **Key Design Decisions**
   - Memory model: Ownership like Rust? GC? Manual?
   - Type system: Static? Inferred? Gradual?
   - Syntax style: C-like? Python-like? Swift-like?
   - Error handling: Exceptions? Result types?
   - Concurrency: async/await? Threads? Actors?

TASK 2: MINIMAL VIABLE COMPILER

Create a compiler that can compile this program:
```reox
fn main() {
    print("Hello from Reox!");
}
```

Provide:
1. Complete Cargo.toml
2. Lexer implementation
3. Parser implementation
4. Code generator (LLVM)
5. Main compiler driver
6. Test suite

Goal: Working compiler that outputs executable.

TASK 3: ROADMAP

Create detailed 12-month roadmap for Reox development:
- Month 1-3: Core language
- Month 4-6: Standard library
- Month 7-9: Advanced features
- Month 10-12: Tooling & ecosystem

DELIVERABLES:
1. ✓ Complete architecture document
2. ✓ Working minimal compiler
3. ✓ 12-month development roadmap
4. ✓ Setup instructions
5. ✓ First example programs

Let's build Reox! Start with architecture design.

✅ ACTION PLAN (Next 2 Weeks)
Week 1: Setup & Planning

 Run Opus session for Reox architecture
 Set up Reox project structure
 Continue kernel work (2-3 hours/day)
 Design Reox syntax (1-2 hours/day)

Week 2: First Code

 Implement Reox lexer
 Start parser
 Continue kernel drivers
 Test both in parallel