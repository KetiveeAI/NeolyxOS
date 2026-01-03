/**
 * @file nxrt_cpp.hpp
 * @brief C++ wrapper for NXRT runtime
 * 
 * Provides RAII-based C++ interface for building NeolyxOS apps.
 */

#pragma once

extern "C" {
#include "nxrt/nxrt.h"
}

#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <stdexcept>

namespace nxrt {

// =============================================================================
// Exceptions
// =============================================================================

class RuntimeException : public std::runtime_error {
public:
    RuntimeException(nxrt_error_t error)
        : std::runtime_error(nxrt_error_string(error))
        , error_(error) {}
    
    nxrt_error_t error() const { return error_; }
    
private:
    nxrt_error_t error_;
};

// =============================================================================
// Enums
// =============================================================================

enum class AppState {
    Created  = NXRT_STATE_CREATED,
    Starting = NXRT_STATE_STARTING,
    Running  = NXRT_STATE_RUNNING,
    Paused   = NXRT_STATE_PAUSED,
    Stopping = NXRT_STATE_STOPPING,
    Stopped  = NXRT_STATE_STOPPED,
};

enum class WindowType {
    Normal     = NXRT_WINDOW_NORMAL,
    Frameless  = NXRT_WINDOW_FRAMELESS,
    Fullscreen = NXRT_WINDOW_FULLSCREEN,
    Overlay    = NXRT_WINDOW_OVERLAY,
    Popup      = NXRT_WINDOW_POPUP,
};

enum class ViewType {
    WebView = NXRT_VIEW_WEBVIEW,
    Native  = NXRT_VIEW_NATIVE,
    Canvas  = NXRT_VIEW_CANVAS,
    Video   = NXRT_VIEW_VIDEO,
};

enum class Permission : uint32_t {
    None         = NXRT_PERM_NONE,
    Network      = NXRT_PERM_NETWORK,
    Storage      = NXRT_PERM_STORAGE,
    Camera       = NXRT_PERM_CAMERA,
    Microphone   = NXRT_PERM_MICROPHONE,
    Location     = NXRT_PERM_LOCATION,
    Notifications = NXRT_PERM_NOTIFICATIONS,
    Bluetooth    = NXRT_PERM_BLUETOOTH,
    USB          = NXRT_PERM_USB,
    Filesystem   = NXRT_PERM_FILESYSTEM,
    All          = NXRT_PERM_ALL,
};

// =============================================================================
// Structs
// =============================================================================

struct Manifest {
    std::string id;
    std::string name;
    std::string version;
    std::string author;
    std::string description;
    std::string icon;
    std::string entry;
    uint32_t permissions = 0;
    bool sandboxed = true;
    bool singleInstance = false;
    bool autoStart = false;
    bool background = false;
    
    nxrt_manifest_t toC() const {
        nxrt_manifest_t m = {};
        strncpy(m.id, id.c_str(), 63);
        strncpy(m.name, name.c_str(), 127);
        strncpy(m.version, version.c_str(), 31);
        strncpy(m.author, author.c_str(), 127);
        strncpy(m.description, description.c_str(), 511);
        strncpy(m.icon, icon.c_str(), 255);
        strncpy(m.entry, entry.c_str(), 255);
        m.permissions = permissions;
        m.sandboxed = sandboxed ? 1 : 0;
        m.single_instance = singleInstance ? 1 : 0;
        m.auto_start = autoStart ? 1 : 0;
        m.background = background ? 1 : 0;
        return m;
    }
};

struct WindowConfig {
    std::string title;
    int x = -1, y = -1;
    int width = 1280, height = 720;
    int minWidth = 0, minHeight = 0;
    int maxWidth = 0, maxHeight = 0;
    WindowType type = WindowType::Normal;
    bool resizable = true;
    bool visible = true;
    bool alwaysOnTop = false;
    bool transparent = false;
    
    nxrt_window_config_t toC() const {
        nxrt_window_config_t c = {};
        strncpy(c.title, title.c_str(), 255);
        c.x = x; c.y = y;
        c.width = width; c.height = height;
        c.min_width = minWidth; c.min_height = minHeight;
        c.max_width = maxWidth; c.max_height = maxHeight;
        c.type = static_cast<nxrt_window_type_t>(type);
        c.resizable = resizable ? 1 : 0;
        c.visible = visible ? 1 : 0;
        c.always_on_top = alwaysOnTop ? 1 : 0;
        c.transparent = transparent ? 1 : 0;
        return c;
    }
};

struct ViewConfig {
    ViewType type = ViewType::WebView;
    std::string url;
    bool devTools = false;
    bool contextMenu = true;
    bool allowScripts = true;
    
    nxrt_view_config_t toC() const {
        nxrt_view_config_t c = {};
        c.type = static_cast<nxrt_view_type_t>(type);
        strncpy(c.url, url.c_str(), 1023);
        c.dev_tools = devTools ? 1 : 0;
        c.context_menu = contextMenu ? 1 : 0;
        c.allow_scripts = allowScripts ? 1 : 0;
        return c;
    }
};

struct SystemInfo {
    std::string osName;
    std::string osVersion;
    std::string deviceName;
    std::string cpuModel;
    uint32_t cpuCores;
    uint64_t memoryTotal;
    uint64_t memoryAvailable;
    std::string gpuName;
};

struct DisplayInfo {
    int width;
    int height;
    float scale;
    float refreshRate;
};

// =============================================================================
// Runtime (RAII Singleton)
// =============================================================================

class Runtime {
public:
    static Runtime& instance() {
        static Runtime rt;
        return rt;
    }
    
    void run() {
        nxrt_error_t err = nxrt_run();
        if (err != NXRT_SUCCESS) {
            throw RuntimeException(err);
        }
    }
    
    void quit() {
        nxrt_quit();
    }
    
    static const char* version() {
        return nxrt_version();
    }
    
    static SystemInfo systemInfo() {
        nxrt_system_info_t info;
        nxrt_system_info(&info);
        
        SystemInfo si;
        si.osName = info.os_name;
        si.osVersion = info.os_version;
        si.deviceName = info.device_name;
        si.cpuModel = info.cpu_model;
        si.cpuCores = info.cpu_cores;
        si.memoryTotal = info.memory_total;
        si.memoryAvailable = info.memory_available;
        si.gpuName = info.gpu_name;
        return si;
    }
    
    static int displayCount() {
        return nxrt_display_count();
    }
    
    static DisplayInfo displayInfo(int index = 0) {
        nxrt_display_info_t info;
        nxrt_display_info(index, &info);
        return {info.width, info.height, info.scale, info.refresh_rate};
    }
    
private:
    Runtime() {
        nxrt_error_t err = nxrt_init();
        if (err != NXRT_SUCCESS) {
            throw RuntimeException(err);
        }
    }
    
    ~Runtime() {
        nxrt_shutdown();
    }
    
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
};

// =============================================================================
// View
// =============================================================================

class View {
public:
    View(nxrt_view_t handle) : handle_(handle) {}
    
    ~View() {
        if (handle_ != NXRT_INVALID_HANDLE) {
            nxrt_view_destroy(handle_);
        }
    }
    
    void navigate(const std::string& url) {
        nxrt_view_navigate(handle_, url.c_str());
    }
    
    void loadHTML(const std::string& html, const std::string& baseUrl = "") {
        nxrt_view_load_html(handle_, html.c_str(), baseUrl.c_str());
    }
    
    void eval(const std::string& script) {
        nxrt_view_eval(handle_, script.c_str());
    }
    
    void bind(const std::string& name, 
              void (*handler)(const char*, char*, size_t)) {
        nxrt_view_bind(handle_, name.c_str(), handler);
    }
    
    void reload() { nxrt_view_reload(handle_); }
    void back() { nxrt_view_back(handle_); }
    void forward() { nxrt_view_forward(handle_); }
    
    nxrt_view_t handle() const { return handle_; }
    
private:
    nxrt_view_t handle_;
};

// =============================================================================
// Window
// =============================================================================

class Window {
public:
    Window(nxrt_window_t handle) : handle_(handle) {}
    
    ~Window() {
        if (handle_ != NXRT_INVALID_HANDLE) {
            nxrt_window_destroy(handle_);
        }
    }
    
    std::shared_ptr<View> createView(const ViewConfig& config = {}) {
        auto c = config.toC();
        nxrt_view_t view = nxrt_view_create(handle_, &c);
        if (view == NXRT_INVALID_HANDLE) {
            throw RuntimeException(NXRT_ERROR_NO_MEMORY);
        }
        return std::make_shared<View>(view);
    }
    
    void show() { nxrt_window_show(handle_); }
    void hide() { nxrt_window_hide(handle_); }
    void close() { nxrt_window_close(handle_); }
    
    void setTitle(const std::string& title) {
        nxrt_window_set_title(handle_, title.c_str());
    }
    
    void setSize(int width, int height) {
        nxrt_window_set_size(handle_, width, height);
    }
    
    void setPosition(int x, int y) {
        nxrt_window_set_position(handle_, x, y);
    }
    
    void setFullscreen(bool fs) {
        nxrt_window_set_fullscreen(handle_, fs ? 1 : 0);
    }
    
    nxrt_window_t handle() const { return handle_; }
    
private:
    nxrt_window_t handle_;
};

// =============================================================================
// App
// =============================================================================

class App {
public:
    using StateCallback = std::function<void(AppState)>;
    using ErrorCallback = std::function<void(nxrt_error_t, const std::string&)>;
    using ReadyCallback = std::function<void()>;
    
    App(const std::string& manifestPath) {
        handle_ = nxrt_app_create(manifestPath.c_str());
        if (handle_ == NXRT_INVALID_HANDLE) {
            throw RuntimeException(NXRT_ERROR_NOT_FOUND);
        }
        setupCallbacks();
    }
    
    App(const Manifest& manifest) {
        auto m = manifest.toC();
        handle_ = nxrt_app_create_ex(&m);
        if (handle_ == NXRT_INVALID_HANDLE) {
            throw RuntimeException(NXRT_ERROR_NO_MEMORY);
        }
        setupCallbacks();
    }
    
    ~App() {
        if (handle_ != NXRT_INVALID_HANDLE) {
            nxrt_app_destroy(handle_);
        }
    }
    
    void start() {
        nxrt_error_t err = nxrt_app_start(handle_);
        if (err != NXRT_SUCCESS) {
            throw RuntimeException(err);
        }
    }
    
    void stop() { nxrt_app_stop(handle_); }
    void pause() { nxrt_app_pause(handle_); }
    void resume() { nxrt_app_resume(handle_); }
    
    AppState state() const {
        return static_cast<AppState>(nxrt_app_state(handle_));
    }
    
    std::shared_ptr<Window> createWindow(const WindowConfig& config = {}) {
        auto c = config.toC();
        nxrt_window_t win = nxrt_window_create(handle_, &c);
        if (win == NXRT_INVALID_HANDLE) {
            throw RuntimeException(NXRT_ERROR_NO_MEMORY);
        }
        return std::make_shared<Window>(win);
    }
    
    bool hasPermission(Permission perm) const {
        return nxrt_permission_check(handle_, static_cast<nxrt_permission_t>(perm));
    }
    
    void requestPermission(Permission perm) {
        nxrt_permission_request(handle_, static_cast<nxrt_permission_t>(perm));
    }
    
    void onState(StateCallback callback) { onState_ = std::move(callback); }
    void onError(ErrorCallback callback) { onError_ = std::move(callback); }
    void onReady(ReadyCallback callback) { onReady_ = std::move(callback); }
    
    nxrt_app_t handle() const { return handle_; }
    
private:
    nxrt_app_t handle_ = NXRT_INVALID_HANDLE;
    StateCallback onState_;
    ErrorCallback onError_;
    ReadyCallback onReady_;
    
    void setupCallbacks() {
        nxrt_app_on_state(handle_, [](void* ud, nxrt_app_state_t state) {
            auto* self = static_cast<App*>(ud);
            if (self->onState_) self->onState_(static_cast<AppState>(state));
        }, this);
        
        nxrt_app_on_ready(handle_, [](void* ud) {
            auto* self = static_cast<App*>(ud);
            if (self->onReady_) self->onReady_();
        }, this);
    }
};

// =============================================================================
// Storage Service
// =============================================================================

class Storage {
public:
    Storage() : svc_(nxrt_service_get(NXRT_SERVICE_STORAGE)) {}
    
    void set(const std::string& key, const std::vector<uint8_t>& data) {
        nxrt_storage_set(svc_, key.c_str(), data.data(), data.size());
    }
    
    void set(const std::string& key, const std::string& value) {
        nxrt_storage_set(svc_, key.c_str(), value.data(), value.size());
    }
    
    std::vector<uint8_t> get(const std::string& key) {
        std::vector<uint8_t> data(4096);
        size_t size = data.size();
        if (nxrt_storage_get(svc_, key.c_str(), data.data(), &size) == NXRT_SUCCESS) {
            data.resize(size);
            return data;
        }
        return {};
    }
    
    std::string getString(const std::string& key) {
        auto data = get(key);
        return std::string(data.begin(), data.end());
    }
    
    void remove(const std::string& key) {
        nxrt_storage_delete(svc_, key.c_str());
    }
    
private:
    nxrt_service_t svc_;
};

// =============================================================================
// Cloud Service
// =============================================================================

class Cloud {
public:
    struct Config {
        std::string apiKey;
        std::string endpoint;
        std::string region;
        bool useTLS = true;
        uint32_t timeoutMs = 30000;
    };
    
    Cloud() : svc_(nxrt_service_get(NXRT_SERVICE_CLOUD)) {}
    
    void configure(const Config& config) {
        nxrt_cloud_config_t c = {};
        strncpy(c.api_key, config.apiKey.c_str(), 127);
        strncpy(c.endpoint, config.endpoint.c_str(), 255);
        strncpy(c.region, config.region.c_str(), 31);
        c.use_tls = config.useTLS ? 1 : 0;
        c.timeout_ms = config.timeoutMs;
        nxrt_cloud_configure(svc_, &c);
    }
    
    std::string request(const std::string& method, 
                        const std::string& path,
                        const std::string& body = "") {
        std::vector<char> response(65536);
        size_t size = response.size();
        
        nxrt_cloud_request(svc_, method.c_str(), path.c_str(),
                           body.data(), body.size(),
                           response.data(), &size);
        
        return std::string(response.data(), size);
    }
    
    std::string get(const std::string& path) {
        return request("GET", path);
    }
    
    std::string post(const std::string& path, const std::string& body) {
        return request("POST", path, body);
    }
    
private:
    nxrt_service_t svc_;
};

// =============================================================================
// Auth Service
// =============================================================================

class Auth {
public:
    Auth() : svc_(nxrt_service_get(NXRT_SERVICE_AUTH)) {}
    
    void login(const std::string& provider = "neolyxos") {
        nxrt_auth_login(svc_, provider.c_str());
    }
    
    void logout() {
        nxrt_auth_logout(svc_);
    }
    
    bool isLoggedIn() const {
        return nxrt_auth_is_logged_in(svc_);
    }
    
    std::string user() const {
        char json[1024];
        nxrt_auth_get_user(svc_, json, sizeof(json));
        return json;
    }
    
private:
    nxrt_service_t svc_;
};

} // namespace nxrt
