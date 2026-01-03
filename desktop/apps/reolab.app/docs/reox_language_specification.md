# ReoX Language Specification
## Hybrid Language for Neolyx OS App Development

**ReoX** is a hybrid programming language designed to bridge multiple programming paradigms, making it easy for developers to migrate existing applications to Neolyx OS. It combines the best features of C, C++, Objective-C, and Rust.

---

## 🎯 **Language Philosophy**

ReoX is designed as a **migration-friendly language** that allows developers to:
- **Port C/C++ applications** with minimal code changes
- **Migrate Rust projects** while maintaining safety guarantees
- **Adapt Objective-C code** with familiar syntax
- **Build new Neolyx OS applications** with modern features

---

## 📝 **Syntax Overview**

### **File Extension**: `.reox`

### **Basic Structure**
```reox
// ReoX combines multiple language styles
import std.io;
import kernel.api;

// C-style includes (for compatibility)
#include <stdio.h>
#include <stdlib.h>

// Rust-style modules
mod utils {
    pub fn helper() -> i32 { 42 }
}

// C++-style classes
class AppManager {
private:
    string name;
    int version;
    
public:
    // Constructor (C++ style)
    AppManager(string n, int v) {
        this.name = n;
        this.version = v;
    }
    
    // Method (Objective-C style)
    -(void)displayInfo {
        printf("App: %s, Version: %d\n", name.c_str(), version);
    }
    
    // Rust-style method
    pub fn get_version(&self) -> i32 {
        self.version
    }
}

// C-style function
int calculate(int a, int b) {
    return a + b;
}

// Rust-style function with ownership
fn process_data(mut data: Vec<i32>) -> Result<i32, Error> {
    if data.is_empty() {
        return Err(Error::new("Empty data"));
    }
    Ok(data.iter().sum())
}

// Objective-C style method
-(NSString*)formatMessage:(NSString*)msg {
    return [NSString stringWithFormat:@"Message: %@", msg];
}

// Main function (multiple styles supported)
fn main() -> i32 {
    // C-style
    printf("Hello from ReoX!\n");
    
    // C++-style
    auto app = AppManager("MyApp", 1);
    app.displayInfo();
    
    // Rust-style
    let result = process_data(vec![1, 2, 3, 4, 5]);
    match result {
        Ok(sum) => println!("Sum: {}", sum),
        Err(e) => println!("Error: {}", e),
    }
    
    // Neolyx OS specific
    kernel::init();
    syscall::register_handler("app_handler", app_handler);
    
    return 0;
}
```

---

## 🔧 **Language Features**

### **1. Multi-Paradigm Support**

#### **C Compatibility**
```reox
// Direct C code works
#include <stdio.h>
#include <stdlib.h>

int main() {
    int* ptr = malloc(sizeof(int));
    *ptr = 42;
    printf("Value: %d\n", *ptr);
    free(ptr);
    return 0;
}
```

#### **C++ Compatibility**
```reox
#include <iostream>
#include <vector>
#include <string>

class MyClass {
private:
    std::string name;
    std::vector<int> data;
    
public:
    MyClass(const std::string& n) : name(n) {}
    
    void addData(int value) {
        data.push_back(value);
    }
    
    void display() {
        std::cout << "Name: " << name << std::endl;
        for (int val : data) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
};
```

#### **Rust Compatibility**
```reox
use std::collections::HashMap;

struct Config {
    name: String,
    settings: HashMap<String, String>,
}

impl Config {
    fn new(name: &str) -> Self {
        Self {
            name: name.to_string(),
            settings: HashMap::new(),
        }
    }
    
    fn set(&mut self, key: &str, value: &str) {
        self.settings.insert(key.to_string(), value.to_string());
    }
    
    fn get(&self, key: &str) -> Option<&String> {
        self.settings.get(key)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut config = Config::new("MyApp");
    config.set("theme", "dark");
    config.set("language", "en");
    
    if let Some(theme) = config.get("theme") {
        println!("Theme: {}", theme);
    }
    
    Ok(())
}
```

#### **Objective-C Compatibility**
```reox
#import <Foundation/Foundation.h>

@interface Person : NSObject {
    NSString* name;
    int age;
}

@property (nonatomic, strong) NSString* name;
@property (nonatomic, assign) int age;

-(instancetype)initWithName:(NSString*)n age:(int)a;
-(void)displayInfo;

@end

@implementation Person

@synthesize name, age;

-(instancetype)initWithName:(NSString*)n age:(int)a {
    self = [super init];
    if (self) {
        self.name = n;
        self.age = a;
    }
    return self;
}

-(void)displayInfo {
    NSLog(@"Name: %@, Age: %d", self.name, self.age);
}

@end
```

### **2. Neolyx OS Integration**

#### **Kernel API Access**
```reox
import kernel.api;
import syscall.handlers;

// Direct kernel access
fn kernel_operations() {
    // Memory management
    let ptr = kernel::alloc(1024);
    kernel::write(ptr, b"Hello Kernel!");
    kernel::free(ptr);
    
    // Process management
    let pid = kernel::spawn_process("my_app");
    kernel::set_priority(pid, Priority::High);
    
    // File system
    let file = kernel::open_file("/etc/config", OpenMode::Read);
    let data = kernel::read_file(file, 1024);
    kernel::close_file(file);
    
    // Hardware access
    let gpu = hardware::gpu::init();
    hardware::gpu::set_resolution(gpu, 1920, 1080);
    
    // Network
    let socket = network::create_socket(SocketType::TCP);
    network::connect(socket, "192.168.1.1", 8080);
}
```

#### **System Calls**
```reox
// ReoX system calls (similar to C syscalls)
fn system_operations() {
    // File operations
    let fd = syscall::open("/file.txt", O_RDWR | O_CREAT, 0644);
    syscall::write(fd, b"Hello World", 11);
    syscall::close(fd);
    
    // Process operations
    let pid = syscall::fork();
    if pid == 0 {
        // Child process
        syscall::exec("/bin/app", &["app", "arg1"]);
    } else {
        // Parent process
        syscall::waitpid(pid, null, 0);
    }
    
    // Memory operations
    let addr = syscall::mmap(null, 4096, PROT_READ | PROT_WRITE, 
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    syscall::munmap(addr, 4096);
}
```

### **3. Migration Features**

#### **C to ReoX Migration**
```reox
// Original C code
/*
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char* name;
    int age;
} Person;

Person* create_person(char* name, int age) {
    Person* p = malloc(sizeof(Person));
    p->name = strdup(name);
    p->age = age;
    return p;
}

void free_person(Person* p) {
    free(p->name);
    free(p);
}
*/

// Migrated to ReoX (C-style preserved)
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    char* name;
    int age;
} Person;

Person* create_person(char* name, int age) {
    Person* p = malloc(sizeof(Person));
    p->name = strdup(name);
    p->age = age;
    return p;
}

void free_person(Person* p) {
    free(p->name);
    free(p);
}

// Enhanced with ReoX features
fn main() {
    let person = create_person("John", 30);
    printf("Name: %s, Age: %d\n", person.name, person.age);
    free_person(person);
    
    // Now can use Rust-style safety
    let safe_person = Person::new("Jane", 25);
    println!("Safe person: {}", safe_person);
}
```

#### **C++ to ReoX Migration**
```reox
// Original C++ code
/*
#include <iostream>
#include <vector>
#include <string>

class Calculator {
private:
    std::vector<double> history;
    
public:
    double add(double a, double b) {
        double result = a + b;
        history.push_back(result);
        return result;
    }
    
    void showHistory() {
        for (double val : history) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
};
*/

// Migrated to ReoX (C++-style preserved)
#include <iostream>
#include <vector>
#include <string>

class Calculator {
private:
    std::vector<double> history;
    
public:
    double add(double a, double b) {
        double result = a + b;
        history.push_back(result);
        return result;
    }
    
    void showHistory() {
        for (double val : history) {
            std::cout << val << " ";
        }
        std::cout << std::endl;
    }
    
    // Enhanced with ReoX features
    fn calculate_safe(&mut self, a: f64, b: f64) -> Result<f64, String> {
        if b == 0.0 {
            return Err("Division by zero".to_string());
        }
        Ok(a / b)
    }
};
```

#### **Rust to ReoX Migration**
```reox
// Original Rust code
/*
use std::collections::HashMap;

struct Config {
    settings: HashMap<String, String>,
}

impl Config {
    fn new() -> Self {
        Self {
            settings: HashMap::new(),
        }
    }
    
    fn set(&mut self, key: &str, value: &str) {
        self.settings.insert(key.to_string(), value.to_string());
    }
}
*/

// Migrated to ReoX (Rust-style preserved)
use std::collections::HashMap;

struct Config {
    settings: HashMap<String, String>,
}

impl Config {
    fn new() -> Self {
        Self {
            settings: HashMap::new(),
        }
    }
    
    fn set(&mut self, key: &str, value: &str) {
        self.settings.insert(key.to_string(), value.to_string());
    }
    
    // Enhanced with C++ features
    fn get_cpp_style(&self, key: &str) -> Option<&String> {
        self.settings.get(key)
    }
    
    // Enhanced with Neolyx OS features
    fn save_to_kernel(&self) -> Result<(), kernel::Error> {
        kernel::save_config("app_config", &self.settings)
    }
}
```

---

## 🏗️ **Compiler Architecture**

### **Multi-Pass Compiler**
```reox
// ReoX compiler handles multiple language styles
reoxc --lang=c      // C compatibility mode
reoxc --lang=cpp    // C++ compatibility mode
reoxc --lang=rust   // Rust compatibility mode
reoxc --lang=objc   // Objective-C compatibility mode
reoxc --lang=hybrid // Mixed mode (default)
```

### **Output Formats**
```reox
// Multiple output formats for different targets
reoxc --output=reoxexe  // Native ReoX executable
reoxc --output=elf      // ELF binary for Linux compatibility
reoxc --output=pe       // PE binary for Windows compatibility
reoxc --output=wasm     // WebAssembly for web apps
```

---

## 📦 **Package Management**

### **ReoX Package Manager (rxpm)**
```bash
# Install packages
rxpm install std.io
rxpm install kernel.api
rxpm install gui.framework

# Create new project
rxpm new my-app --template=c
rxpm new my-app --template=cpp
rxpm new my-app --template=rust
rxpm new my-app --template=hybrid

# Build and run
rxpm build
rxpm run
rxpm test
```

---

## 🔄 **Migration Tools**

### **Auto-Migration Scripts**
```bash
# Migrate C project
reox-migrate --from=c --to=reox ./my-c-project

# Migrate C++ project
reox-migrate --from=cpp --to=reox ./my-cpp-project

# Migrate Rust project
reox-migrate --from=rust --to=reox ./my-rust-project

# Analyze compatibility
reox-analyze ./existing-project
```

---

## 🎯 **Use Cases**

### **1. C/C++ Application Migration**
```reox
// Developers can gradually migrate C/C++ apps
// while maintaining compatibility

// Phase 1: Direct compilation (no changes needed)
reoxc --lang=c main.c -o app.reoxexe

// Phase 2: Add ReoX features incrementally
// Original C code works, new ReoX features available

// Phase 3: Full ReoX optimization
// Leverage all ReoX features for Neolyx OS
```

### **2. Rust Application Migration**
```reox
// Rust developers can migrate with minimal changes
// while gaining Neolyx OS integration

// Original Rust code continues to work
// New Neolyx OS APIs available
```

### **3. New Neolyx OS Applications**
```reox
// Build new applications using the best of all languages
// with native Neolyx OS integration
```

---

## 🚀 **Getting Started**

### **Hello World Example**
```reox
// Simple ReoX program
import std.io;

fn main() {
    println!("Hello from ReoX!");
    
    // C-style
    printf("Also works with C!\n");
    
    // C++-style
    std::cout << "And C++!" << std::endl;
    
    // Neolyx OS integration
    kernel::log("Hello from kernel!");
}
```

### **Build and Run**
```bash
# Compile
reoxc hello.reox -o hello.reoxexe

# Run
./hello.reoxexe

# Or use package manager
rxpm run hello.reox
```

---

**ReoX** - The bridge language for seamless app migration to Neolyx OS! 🚀 