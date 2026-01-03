/**
 * NXRT Example App
 * 
 * Demonstrates how to build NeolyxOS apps using NXRT.
 */

#include <nxrt/nxrt_cpp.hpp>
#include <iostream>

// Native function that can be called from JavaScript
void greet(const char* args, char* result, size_t size) {
    snprintf(result, size, "Hello from NXRT! Args: %s", args);
}

int main() {
    using namespace nxrt;
    
    try {
        // Initialize runtime
        auto& runtime = Runtime::instance();
        std::cout << "NXRT Version: " << runtime.version() << std::endl;
        
        // Get system info
        auto sys = Runtime::systemInfo();
        std::cout << "OS: " << sys.osName << " " << sys.osVersion << std::endl;
        std::cout << "CPU Cores: " << sys.cpuCores << std::endl;
        std::cout << "Memory: " << (sys.memoryTotal / 1024 / 1024) << " MB" << std::endl;
        
        // Get display info
        auto display = Runtime::displayInfo();
        std::cout << "Display: " << display.width << "x" << display.height 
                  << " @" << display.refreshRate << "Hz" << std::endl;
        
        // Create app
        Manifest manifest;
        manifest.id = "com.example.hello";
        manifest.name = "Hello NXRT";
        manifest.version = "1.0.0";
        manifest.author = "NeolyxOS Team";
        manifest.description = "Example NXRT application";
        manifest.entry = "index.html";
        manifest.permissions = static_cast<uint32_t>(Permission::Network) | 
                               static_cast<uint32_t>(Permission::Storage);
        
        App app(manifest);
        
        // Set callbacks
        app.onState([](AppState state) {
            const char* names[] = {
                "Created", "Starting", "Running", 
                "Paused", "Stopping", "Stopped"
            };
            std::cout << "App state: " << names[static_cast<int>(state)] << std::endl;
        });
        
        app.onReady([]() {
            std::cout << "App is ready!" << std::endl;
        });
        
        // Create main window
        WindowConfig winConfig;
        winConfig.title = "Hello NXRT";
        winConfig.width = 1024;
        winConfig.height = 768;
        
        auto window = app.createWindow(winConfig);
        
        // Create WebView
        ViewConfig viewConfig;
        viewConfig.type = ViewType::WebView;
        viewConfig.url = "file:///app/index.html";
        viewConfig.allowScripts = true;
        
        auto view = window->createView(viewConfig);
        
        // Bind native function to JavaScript
        view->bind("nativeGreet", greet);
        
        // Load custom HTML
        view->loadHTML(R"(
            <!DOCTYPE html>
            <html>
            <head>
                <title>Hello NXRT</title>
                <style>
                    body {
                        font-family: system-ui, -apple-system, sans-serif;
                        background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
                        color: white;
                        margin: 0;
                        padding: 40px;
                        min-height: 100vh;
                    }
                    h1 { font-size: 3rem; margin-bottom: 1rem; }
                    .card {
                        background: rgba(255,255,255,0.1);
                        border-radius: 16px;
                        padding: 24px;
                        backdrop-filter: blur(10px);
                        margin-top: 20px;
                    }
                    button {
                        background: #fff;
                        color: #764ba2;
                        border: none;
                        padding: 12px 24px;
                        border-radius: 8px;
                        font-size: 16px;
                        cursor: pointer;
                        margin-top: 16px;
                    }
                    button:hover { transform: scale(1.05); }
                </style>
            </head>
            <body>
                <h1>[Run] Hello NXRT!</h1>
                <p>This app runs on the NeolyxOS Runtime.</p>
                
                <div class="card">
                    <h2>Native Integration</h2>
                    <p>Click the button to call a native C++ function:</p>
                    <button onclick="callNative()">Call Native</button>
                    <p id="result"></p>
                </div>
                
                <div class="card">
                    <h2>Cloud Services</h2>
                    <p>Access NeolyxOS cloud APIs directly.</p>
                    <button onclick="fetchData()">Fetch Data</button>
                </div>
                
                <script>
                    function callNative() {
                        // This calls the bound C++ function
                        const result = nativeGreet("World");
                        document.getElementById("result").textContent = result;
                    }
                    
                    function fetchData() {
                        // Cloud API call
                        console.log("Fetching from NeolyxOS Cloud...");
                    }
                </script>
            </body>
            </html>
        )", "file:///app/");
        
        // Start the app
        app.start();
        
        // Use services
        Storage storage;
        storage.set("lastRun", "2025-12-12");
        
        Cloud cloud;
        cloud.configure({
            .apiKey = "your-api-key",
            .endpoint = "https://api.neolyxos.com",
            .region = "us-west-1"
        });
        
        Auth auth;
        if (!auth.isLoggedIn()) {
            std::cout << "User not logged in" << std::endl;
        }
        
        // Run event loop
        runtime.run();
        
    } catch (const nxrt::RuntimeException& e) {
        std::cerr << "Runtime error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
