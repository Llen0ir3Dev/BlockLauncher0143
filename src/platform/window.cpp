#include <nexus/platform/window.hpp>

#include <cstdlib>
#include <cstring>

#include <SDL.h>

#include <nexus/core/log.hpp>

namespace nexus::platform {

namespace {

Key map_key(SDL_Keycode key) {
    switch (key) {
        case SDLK_w: return Key::W;
        case SDLK_a: return Key::A;
        case SDLK_s: return Key::S;
        case SDLK_d: return Key::D;
        case SDLK_e: return Key::E;
        case SDLK_q: return Key::Q;
        case SDLK_r: return Key::R;
        case SDLK_f: return Key::F;
        case SDLK_SPACE: return Key::Space;
        case SDLK_LSHIFT: return Key::LeftShift;
        case SDLK_LCTRL: return Key::LeftControl;
        case SDLK_ESCAPE: return Key::Escape;
        case SDLK_RETURN: return Key::Enter;
        case SDLK_TAB: return Key::Tab;
        case SDLK_F1: return Key::F1;
        case SDLK_F2: return Key::F2;
        case SDLK_F3: return Key::F3;
        case SDLK_F11: return Key::F11;
        case SDLK_F12: return Key::F12;
        case SDLK_UP: return Key::Up;
        case SDLK_DOWN: return Key::Down;
        case SDLK_LEFT: return Key::Left;
        case SDLK_RIGHT: return Key::Right;
        default: return Key::Unknown;
    }
}

} // namespace

Window::~Window() {
    destroy();
}

bool Window::create(const WindowConfig& config, std::string& error) {
    destroy();
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        error = std::string("SDL video init failed: ") + SDL_GetError();
        return false;
    }
    sdl_initialized_ = true;

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    Uint32 flags = SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN;
    if (config.resizable) flags |= SDL_WINDOW_RESIZABLE;
    if (config.hidden) {
        flags &= ~SDL_WINDOW_SHOWN;
        flags |= SDL_WINDOW_HIDDEN;
    }
    if (config.fullscreen) {
        flags &= ~SDL_WINDOW_RESIZABLE;
        flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
    }

    if (config.prefer_gles) {
        SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        window_ = SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
        if (window_ != nullptr) {
            gl_context_ = SDL_GL_CreateContext(static_cast<SDL_Window*>(window_));
            gles_ = gl_context_ != nullptr;
        }
    }
    if (window_ == nullptr || gl_context_ == nullptr) {
        if (window_ != nullptr) {
            SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
            window_ = nullptr;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
        window_ = SDL_CreateWindow(config.title.c_str(), SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, config.width, config.height, flags);
        if (window_ == nullptr) {
            error = std::string("SDL window creation failed: ") + SDL_GetError();
            destroy();
            return false;
        }
        gl_context_ = SDL_GL_CreateContext(static_cast<SDL_Window*>(window_));
        gles_ = false;
        if (gl_context_ == nullptr) {
            error = std::string("OpenGL context creation failed: ") + SDL_GetError();
            destroy();
            return false;
        }
    }
    SDL_GL_MakeCurrent(static_cast<SDL_Window*>(window_), static_cast<SDL_GLContext>(gl_context_));
    SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
    SDL_GetWindowSize(static_cast<SDL_Window*>(window_), &width_, &height_);
    NEXUS_LOG_INFO("window created: " + std::to_string(width_) + "x" +
                   std::to_string(height_) + " (" +
                   (gles_ ? "GLES 2.0" : "desktop GL compatibility") + ")");
    return true;
}

void Window::destroy() {
    if (gl_context_ != nullptr) {
        SDL_GL_DeleteContext(static_cast<SDL_GLContext>(gl_context_));
        gl_context_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(static_cast<SDL_Window*>(window_));
        window_ = nullptr;
    }
    if (sdl_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
        sdl_initialized_ = false;
    }
    relative_mouse_ = false;
}

std::string Window::context_description() const {
    if (gl_context_ == nullptr) return "no context";
    return gles_ ? "OpenGL ES 2.0" : "OpenGL 2.1 compatibility";
}

bool Window::pump_events(InputSnapshot& input) {
    input = InputSnapshot{};
    input.relative_mouse = relative_mouse_;
    if (window_ == nullptr) {
        input.quit = true;
        return false;
    }
    SDL_Event event;
    while (SDL_PollEvent(&event) != 0) {
        switch (event.type) {
            case SDL_QUIT:
                input.quit = true;
                break;
            case SDL_WINDOWEVENT:
                if (event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED ||
                    event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    width_ = event.window.data1;
                    height_ = event.window.data2;
                } else if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    input.focus_lost = true;
                }
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                if (event.key.repeat) break;
                const Key key = map_key(event.key.keysym.sym);
                if (key == Key::Unknown) break;
                const int index = static_cast<int>(key);
                if (event.type == SDL_KEYDOWN) {
                    input.key_down[index] = true;
                    input.key_pressed[index] = true;
                } else {
                    input.key_released[index] = true;
                }
                break;
            }
            case SDL_MOUSEMOTION:
                input.mouse_x = event.motion.x;
                input.mouse_y = event.motion.y;
                input.mouse_dx += event.motion.xrel;
                input.mouse_dy += event.motion.yrel;
                break;
            case SDL_MOUSEBUTTONDOWN:
            case SDL_MOUSEBUTTONUP:
                if (event.button.button >= 1 && event.button.button <= 3) {
                    input.mouse_buttons[event.button.button - 1] =
                        event.type == SDL_MOUSEBUTTONDOWN;
                }
                break;
            case SDL_MOUSEWHEEL:
                input.wheel += event.wheel.y;
                break;
            default:
                break;
        }
    }
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    struct Mapping { SDL_Scancode scan; Key key; };
    static const Mapping mappings[] = {
        {SDL_SCANCODE_W, Key::W}, {SDL_SCANCODE_A, Key::A}, {SDL_SCANCODE_S, Key::S},
        {SDL_SCANCODE_D, Key::D}, {SDL_SCANCODE_E, Key::E}, {SDL_SCANCODE_Q, Key::Q},
        {SDL_SCANCODE_R, Key::R}, {SDL_SCANCODE_F, Key::F}, {SDL_SCANCODE_SPACE, Key::Space},
        {SDL_SCANCODE_LSHIFT, Key::LeftShift}, {SDL_SCANCODE_LCTRL, Key::LeftControl},
        {SDL_SCANCODE_ESCAPE, Key::Escape}, {SDL_SCANCODE_RETURN, Key::Enter},
        {SDL_SCANCODE_TAB, Key::Tab}, {SDL_SCANCODE_F1, Key::F1}, {SDL_SCANCODE_F2, Key::F2},
        {SDL_SCANCODE_F3, Key::F3}, {SDL_SCANCODE_F11, Key::F11}, {SDL_SCANCODE_F12, Key::F12},
        {SDL_SCANCODE_UP, Key::Up}, {SDL_SCANCODE_DOWN, Key::Down},
        {SDL_SCANCODE_LEFT, Key::Left}, {SDL_SCANCODE_RIGHT, Key::Right}
    };
    for (const auto& mapping : mappings) {
        if (keys[mapping.scan]) input.key_down[static_cast<int>(mapping.key)] = true;
    }
    return !input.quit;
}

void Window::swap_buffers() {
    if (window_ != nullptr) SDL_GL_SwapWindow(static_cast<SDL_Window*>(window_));
}

void Window::set_title(const std::string& title) {
    if (window_ != nullptr) SDL_SetWindowTitle(static_cast<SDL_Window*>(window_), title.c_str());
}

void Window::set_relative_mouse(bool enabled) {
    relative_mouse_ = enabled;
    SDL_SetRelativeMouseMode(enabled ? SDL_TRUE : SDL_FALSE);
}

void* Window::get_proc_address(const char* name) const {
    return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name));
}

bool display_available() {
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* display = std::getenv("DISPLAY");
    return (wayland != nullptr && *wayland != '\0') || (display != nullptr && *display != '\0');
}

std::string sdl_version_string() {
    SDL_version version;
    SDL_GetVersion(&version);
    return std::to_string(version.major) + "." + std::to_string(version.minor) + "." +
           std::to_string(version.patch);
}

std::string sdl_error() {
    return SDL_GetError();
}

} // namespace nexus::platform
