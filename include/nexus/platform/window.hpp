#pragma once

#include <cstdint>
#include <string>

namespace nexus::platform {

struct WindowConfig {
    std::string title = "NEXUS-MCPE";
    int width = 1280;
    int height = 720;
    bool fullscreen = false;
    bool resizable = true;
    bool vsync = true;
    // Prefer a GLES 2.0 context (the API used by Minecraft PE 0.14.3).  When
    // unavailable the window falls back to a desktop compatibility context.
    bool prefer_gles = true;
    bool hidden = false;
};

enum class Key {
    Unknown,
    W, A, S, D,
    Space,
    LeftShift,
    LeftControl,
    Escape,
    Enter,
    Tab,
    F1, F2, F3, F11, F12,
    Up, Down, Left, Right,
    E, Q, R, F,
    Count
};

struct InputSnapshot {
    bool quit = false;
    bool focus_lost = false;
    bool key_down[static_cast<int>(Key::Count)]{};
    bool key_pressed[static_cast<int>(Key::Count)]{};
    bool key_released[static_cast<int>(Key::Count)]{};
    bool mouse_buttons[3]{};
    int mouse_x = 0;
    int mouse_y = 0;
    int mouse_dx = 0;
    int mouse_dy = 0;
    int wheel = 0;
    bool relative_mouse = false;
};

class Window {
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool create(const WindowConfig& config, std::string& error);
    void destroy();

    bool valid() const { return window_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool is_gles() const { return gles_; }
    std::string context_description() const;

    // Polls SDL events into `input`.  Returns false when the window was closed.
    bool pump_events(InputSnapshot& input);
    void swap_buffers();
    void set_title(const std::string& title);
    void set_relative_mouse(bool enabled);

    void* gl_context() const { return gl_context_; }
    void* native_window() const { return window_; }
    void* get_proc_address(const char* name) const;

private:
    void* window_ = nullptr;
    void* gl_context_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool gles_ = false;
    bool sdl_initialized_ = false;
    bool relative_mouse_ = false;
};

bool display_available();
std::string sdl_version_string();
std::string sdl_error();

} // namespace nexus::platform
