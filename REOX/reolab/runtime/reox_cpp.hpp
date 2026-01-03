/**
 * @file reox_cpp.hpp
 * @brief REOX C++ Declarative UI Builder
 * 
 * SwiftUI-like declarative syntax for C++ applications.
 * Cross-platform, no SDL, no Vulkan - pure NXRender backend.
 * 
 * Copyright (c) 2025 KetiveeAI - Open Source (MIT License)
 * 
 * Example:
 *   #include "reox_cpp.hpp"
 *   
 *   auto ui = RX::VStack(
 *       RX::Text("Hello, REOX!"),
 *       RX::Button("Click Me").onClick([]{ printf("Clicked!"); }),
 *       RX::HStack(
 *           RX::TextField().placeholder("Enter name..."),
 *           RX::Spacer()
 *       )
 *   );
 */

#ifndef REOX_CPP_HPP
#define REOX_CPP_HPP

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>

// Include C bridge
extern "C" {
#include "reox_nxrender_bridge.h"
}

namespace RX {

// ============================================================================
// Forward Declarations
// ============================================================================

struct Widget;
template<typename... Children> struct VStackT;
template<typename... Children> struct HStackT;

// ============================================================================
// Color
// ============================================================================

struct Color {
    uint8_t r, g, b, a;
    
    constexpr Color() : r(0), g(0), b(0), a(255) {}
    constexpr Color(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) : r(r), g(g), b(b), a(a) {}
    constexpr Color(uint32_t hex) 
        : r((hex >> 16) & 0xFF), g((hex >> 8) & 0xFF), b(hex & 0xFF), a(255) {}
    
    NxColor toNx() const { return {r, g, b, a}; }
    
    // NeolyxOS design system colors
    static constexpr Color primary() { return {88, 166, 255}; }
    static constexpr Color secondary() { return {99, 102, 241}; }
    static constexpr Color success() { return {52, 211, 153}; }
    static constexpr Color warning() { return {251, 191, 36}; }
    static constexpr Color error() { return {248, 113, 113}; }
    static constexpr Color surface() { return {30, 30, 30}; }
    static constexpr Color surfaceHover() { return {45, 45, 45}; }
    static constexpr Color background() { return {18, 18, 18}; }
    static constexpr Color text() { return {255, 255, 255}; }
    static constexpr Color textSecondary() { return {156, 163, 175}; }
    static constexpr Color clear() { return {0, 0, 0, 0}; }
};

// ============================================================================
// Widget Base
// ============================================================================

class Widget {
public:
    virtual ~Widget() = default;
    virtual RxNode* build() const = 0;
    
    // Modifiers (chainable)
    Widget& background(Color c) { bg_ = c; return *this; }
    Widget& foreground(Color c) { fg_ = c; return *this; }
    Widget& padding(float p) { padding_ = p; return *this; }
    Widget& cornerRadius(float r) { radius_ = r; return *this; }
    Widget& frame(float w, float h) { width_ = w; height_ = h; return *this; }
    
protected:
    Color bg_ = Color::clear();
    Color fg_ = Color::text();
    float padding_ = 0;
    float radius_ = 0;
    float width_ = -1;
    float height_ = -1;
    
    void applyStyle(RxNode* node) const {
        if (!node) return;
        node->background = bg_.toNx();
        node->foreground = fg_.toNx();
        node->padding = padding_;
        node->corner_radius = radius_;
        if (width_ > 0) node->width = width_;
        if (height_ > 0) node->height = height_;
    }
};

// ============================================================================
// Text Widget
// ============================================================================

class Text : public Widget {
public:
    Text(const std::string& text) : text_(text) {}
    
    Text& fontSize(float size) { fontSize_ = size; return *this; }
    Text& bold() { bold_ = true; return *this; }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_TEXT);
        node->text = strdup(text_.c_str());
        applyStyle(node);
        return node;
    }
    
private:
    std::string text_;
    float fontSize_ = 14;
    bool bold_ = false;
};

// ============================================================================
// Button Widget
// ============================================================================

class Button : public Widget {
public:
    Button(const std::string& label) : label_(label) {
        bg_ = Color::primary();
        radius_ = 8;
        padding_ = 12;
    }
    
    Button& onClick(std::function<void()> cb) { onClick_ = cb; return *this; }
    Button& destructive() { bg_ = Color::error(); return *this; }
    Button& secondary() { bg_ = Color::surface(); return *this; }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_BUTTON);
        node->text = strdup(label_.c_str());
        applyStyle(node);
        return node;
    }
    
private:
    std::string label_;
    std::function<void()> onClick_;
};

// ============================================================================
// TextField Widget
// ============================================================================

class TextField : public Widget {
public:
    TextField() {
        bg_ = Color::surface();
        radius_ = 8;
        padding_ = 12;
    }
    
    TextField& placeholder(const std::string& p) { placeholder_ = p; return *this; }
    TextField& text(const std::string& t) { text_ = t; return *this; }
    TextField& onChange(std::function<void(const std::string&)> cb) { onChange_ = cb; return *this; }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_TEXTFIELD);
        node->text = strdup(text_.empty() ? placeholder_.c_str() : text_.c_str());
        applyStyle(node);
        return node;
    }
    
private:
    std::string placeholder_;
    std::string text_;
    std::function<void(const std::string&)> onChange_;
};

// ============================================================================
// Spacer & Divider
// ============================================================================

class Spacer : public Widget {
public:
    RxNode* build() const override { return rx_node_create(RX_NODE_SPACER); }
};

class Divider : public Widget {
public:
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_DIVIDER);
        node->height = 1;
        return node;
    }
};

// ============================================================================
// Checkbox Widget
// ============================================================================

class Checkbox : public Widget {
public:
    Checkbox(const std::string& label = "") : label_(label) {}
    
    Checkbox& checked(bool c) { checked_ = c; return *this; }
    Checkbox& onToggle(std::function<void(bool)> cb) { onToggle_ = cb; return *this; }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_CHECKBOX);
        node->text = strdup(label_.c_str());
        node->value = checked_ ? 1.0f : 0.0f;
        applyStyle(node);
        return node;
    }
    
private:
    std::string label_;
    bool checked_ = false;
    std::function<void(bool)> onToggle_;
};

// ============================================================================
// Slider Widget
// ============================================================================

class Slider : public Widget {
public:
    Slider(float min = 0, float max = 1) : min_(min), max_(max) {}
    
    Slider& value(float v) { value_ = v; return *this; }
    Slider& onChange(std::function<void(float)> cb) { onChange_ = cb; return *this; }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_SLIDER);
        node->value = (value_ - min_) / (max_ - min_);
        applyStyle(node);
        return node;
    }
    
private:
    float min_, max_;
    float value_ = 0;
    std::function<void(float)> onChange_;
};

// ============================================================================
// Container Widgets (VStack, HStack)
// ============================================================================

class VStack : public Widget {
public:
    VStack(float gap = 8) : gap_(gap) {}
    
    template<typename T>
    VStack& add(T&& widget) {
        children_.push_back(std::make_unique<std::decay_t<T>>(std::forward<T>(widget)));
        return *this;
    }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_VSTACK);
        node->gap = gap_;
        applyStyle(node);
        
        for (const auto& child : children_) {
            rx_node_add_child(node, child->build());
        }
        return node;
    }
    
private:
    float gap_;
    std::vector<std::unique_ptr<Widget>> children_;
};

class HStack : public Widget {
public:
    HStack(float gap = 8) : gap_(gap) {}
    
    template<typename T>
    HStack& add(T&& widget) {
        children_.push_back(std::make_unique<std::decay_t<T>>(std::forward<T>(widget)));
        return *this;
    }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_HSTACK);
        node->gap = gap_;
        applyStyle(node);
        
        for (const auto& child : children_) {
            rx_node_add_child(node, child->build());
        }
        return node;
    }
    
private:
    float gap_;
    std::vector<std::unique_ptr<Widget>> children_;
};

class ZStack : public Widget {
public:
    template<typename T>
    ZStack& add(T&& widget) {
        children_.push_back(std::make_unique<std::decay_t<T>>(std::forward<T>(widget)));
        return *this;
    }
    
    RxNode* build() const override {
        RxNode* node = rx_node_create(RX_NODE_ZSTACK);
        applyStyle(node);
        
        for (const auto& child : children_) {
            rx_node_add_child(node, child->build());
        }
        return node;
    }
    
private:
    std::vector<std::unique_ptr<Widget>> children_;
};

// ============================================================================
// Application Window
// ============================================================================

class App {
public:
    App(uint32_t width, uint32_t height, const std::string& title = "REOX App")
        : width_(width), height_(height), title_(title) {}
    
    ~App() {
        if (initialized_) rx_bridge_destroy();
    }
    
    bool init() {
        rx_bridge_init(width_, height_);
        initialized_ = true;
        return true;
    }
    
    template<typename WidgetT>
    void setContent(WidgetT&& root) {
        if (!initialized_) return;
        
        if (rx_bridge->root) rx_node_destroy(rx_bridge->root);
        
        RxNode* rootNode = root.build();
        rootNode->x = 0;
        rootNode->y = 0;
        rootNode->width = static_cast<float>(width_);
        rootNode->height = static_cast<float>(height_);
        rootNode->state |= RX_STATE_DIRTY;
        
        rx_bridge->root = rootNode;
        rx_bridge->needs_redraw = true;
    }
    
    void mouseMove(float x, float y) { rx_handle_mouse_move(x, y); }
    void mouseDown(float x, float y) { rx_handle_mouse_down(x, y); }
    void mouseUp(float x, float y) { rx_handle_mouse_up(x, y); }
    
    void frame() { rx_frame(); }
    bool needsRedraw() const { return rx_bridge && rx_bridge->needs_redraw; }
    
private:
    uint32_t width_, height_;
    std::string title_;
    bool initialized_ = false;
};

} // namespace RX

#endif // REOX_CPP_HPP
