#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

#include <nlohmann/json.hpp>

#include <nexus/bridge/agent.hpp>
#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>
#include <nexus/graphics/gl.hpp>
#include <nexus/graphics/renderer.hpp>
#include <nexus/mcpe/mcpe.hpp>
#include <nexus/mod/hooks.hpp>
#include <nexus/mod/mod.hpp>
#include <nexus/mod/registry.hpp>
#include <nexus/platform/apk.hpp>
#include <nexus/platform/provisioning.hpp>
#include <nexus/platform/strategy.hpp>
#include <nexus/platform/system.hpp>
#include <nexus/platform/window.hpp>
#include <nexus/shader/analyzer.hpp>
#include <nexus/shader/compiler.hpp>
#include <nexus/shader/preprocessor.hpp>
#include <nexus/shader/translator.hpp>
#include <nexus/worldgen/world.hpp>

namespace {

std::atomic<bool> g_stop{false};

void handle_signal(int) {
    g_stop.store(true);
}

const char* kRuntimeUsage = R"(nexus-runtime - NEXUS bridge host and shader validation shell

This binary is NOT the Minecraft client. The original game remains the client.
The runtime hosts the NEXUS bridge agent, mod engine, shader translator and
worldgen tools, and optionally renders a GL validation scene.

Usage: nexus-runtime [options]
  --socket PATH       Bridge socket (default: $NEXUS_HOME/run/nexus-runtime.sock)
  --headless          Do not create a window (default when no display is available)
  --demo              Render a small GL scene using the platform GL context
  --seconds N         Stop after N seconds (0 = run until signalled)
  --screenshot PATH   Save one PNG frame in demo mode
  --assets DIR        Extracted assets directory for MCPE asset queries
  --mods DIR          Mod directory to discover and enable
  --allow-native      Allow native mods (opt-in)
  --seed N            World seed for worldgen bridge methods
  --shader-example    Run one shader translation at startup and log the result
  --help
)";

struct RuntimeOptions {
    std::string socket_path = nexus::bridge::default_socket_path();
    bool headless = false;
    bool demo = false;
    int seconds = 0;
    std::string screenshot;
    std::string assets;
    std::string mods;
    bool allow_native = false;
    uint64_t seed = 0;
    bool shader_example = false;
};

nexus::shader::TranslationOptions translation_options_from(const nlohmann::json& params) {
    nexus::shader::TranslationOptions options;
    const std::string target = params.value("target", "gles100");
    nexus::shader::TargetProfile profile = nexus::shader::TargetProfile::GLES100;
    if (nexus::shader::parse_target(target, profile)) options.target = profile;
    return options;
}

std::string constant_demo_vertex(bool gles) {
    if (gles) {
        return "#version 100\n"
               "attribute vec2 a_position;\n"
               "attribute vec2 a_texcoord;\n"
               "varying vec2 v_texcoord;\n"
               "void main() {\n"
               "  v_texcoord = a_texcoord;\n"
               "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
               "}\n";
    }
    return "#version 120\n"
           "attribute vec2 a_position;\n"
           "attribute vec2 a_texcoord;\n"
           "varying vec2 v_texcoord;\n"
           "void main() {\n"
           "  v_texcoord = a_texcoord;\n"
           "  gl_Position = vec4(a_position, 0.0, 1.0);\n"
           "}\n";
}

std::string constant_demo_fragment(bool gles) {
    if (gles) {
        return "#version 100\n"
               "precision mediump float;\n"
               "varying vec2 v_texcoord;\n"
               "uniform sampler2D TEXTURE_0;\n"
               "uniform float u_time;\n"
               "void main() {\n"
               "  vec4 tex = texture2D(TEXTURE_0, v_texcoord);\n"
               "  float pulse = 0.75 + 0.25 * sin(u_time * 2.0);\n"
               "  gl_FragColor = vec4(tex.rgb * pulse, 1.0);\n"
               "}\n";
    }
    return "#version 120\n"
           "varying vec2 v_texcoord;\n"
           "uniform sampler2D TEXTURE_0;\n"
           "uniform float u_time;\n"
           "void main() {\n"
           "  vec4 tex = texture2D(TEXTURE_0, v_texcoord);\n"
           "  float pulse = 0.75 + 0.25 * sin(u_time * 2.0);\n"
           "  gl_FragColor = vec4(tex.rgb * pulse, 1.0);\n"
           "}\n";
}

} // namespace

int main(int argc, char** argv) {
    RuntimeOptions options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << kRuntimeUsage;
            return 0;
        }
        if (arg == "--headless") options.headless = true;
        else if (arg == "--demo") options.demo = true;
        else if (arg == "--allow-native") options.allow_native = true;
        else if (arg == "--shader-example") options.shader_example = true;
        else if (arg == "--socket" && i + 1 < argc) options.socket_path = argv[++i];
        else if (arg == "--seconds" && i + 1 < argc) options.seconds = std::atoi(argv[++i]);
        else if (arg == "--screenshot" && i + 1 < argc) options.screenshot = argv[++i];
        else if (arg == "--assets" && i + 1 < argc) options.assets = argv[++i];
        else if (arg == "--mods" && i + 1 < argc) options.mods = argv[++i];
        else if (arg == "--seed" && i + 1 < argc) {
            options.seed = std::strtoull(argv[++i], nullptr, 0);
        } else {
            std::cerr << "nexus-runtime: unknown argument " << arg << "\n";
            return 2;
        }
    }
    if (!nexus::platform::display_available()) options.headless = true;
    if (options.headless) options.demo = false;

    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
    nexus::Logger::instance().set_log_file(
        nexus::fs::join(nexus::fs::logs_dir(), "runtime-" + nexus::fs::timestamp_string() + ".log"));

    NEXUS_LOG_INFO("NEXUS runtime starting (not the game client)");
    nexus::EventBus events;
    nexus::mod::ContentRegistry registry;
    nexus::mod::HookManager hooks;
    nexus::mod::ModHostOptions mod_options;
    mod_options.allow_native = options.allow_native;
    nexus::mod::ModHost mod_host(events, registry, hooks, mod_options);
    nexus::worldgen::WorldGenerator world(options.seed);
    mod_host.set_world(&world);

    if (!options.mods.empty()) {
        size_t discovered = 0;
        if (nexus::fs::is_regular_file(nexus::fs::join(options.mods, "mod.json"))) {
            std::string error;
            if (mod_host.add_manifest_path(options.mods, error)) discovered = 1;
            else NEXUS_LOG_WARN("mod manifest rejected: " + error);
        } else {
            discovered = mod_host.discover(options.mods);
        }
        NEXUS_LOG_INFO("discovered " + std::to_string(discovered) + " mods");
        for (const auto& record : mod_host.mods()) {
            std::string error;
            if (!mod_host.enable(record.manifest.id, error)) {
                NEXUS_LOG_WARN("mod " + record.manifest.id + " failed: " + error);
            }
        }
    }

    if (options.shader_example) {
        const std::string source =
            "precision mediump float;\n"
            "varying vec2 v_texcoord;\n"
            "uniform sampler2D TEXTURE_0;\n"
            "uniform vec4 FOG_COLOR;\n"
            "void main() { gl_FragColor = texture2D(TEXTURE_0, v_texcoord) * FOG_COLOR; }\n";
        const auto result = nexus::shader::translate_glsl(
            source, nexus::shader::ShaderStage::Fragment, translation_options_from({}));
        if (result.success) {
            NEXUS_LOG_INFO("shader example translated (" +
                           std::string(nexus::shader::compatibility_name(result.level)) + ")");
        } else {
            NEXUS_LOG_WARN("shader example failed: " + result.error);
        }
    }

    nexus::bridge::BridgeAgentOptions agent_options;
    agent_options.socket_path = options.socket_path;
    nexus::bridge::BridgeAgent agent(agent_options);
    agent.register_method(
        "runtime.info",
        [&](const std::string&, const nlohmann::json&, nlohmann::json& result,
            nexus::bridge::BridgeError&, const nexus::bridge::BridgeRequestContext&) {
            const auto system = nexus::platform::system_info();
            result = {{"name", "nexus-runtime"},
                      {"version", nexus::kVersion},
                      {"gameVersion", nexus::kGameVersion},
                      {"protocol", nexus::bridge::kProtocolName},
                      {"isGameClient", false},
                      {"mode", options.demo ? "demo" : "headless"},
                      {"system", system.to_json()},
                      {"mods", mod_host.to_json()},
                      {"worldSeed", world.seed()}};
            return true;
        });
    agent.register_method(
        "shader.translate",
        [](const std::string&, const nlohmann::json& params, nlohmann::json& result,
           nexus::bridge::BridgeError& error,
           const nexus::bridge::BridgeRequestContext&) {
            const std::string source = params.value("source", "");
            if (source.empty()) {
                error.code = nexus::bridge::error_code::kInvalidParams;
                error.message = "shader.translate requires a 'source' string";
                return false;
            }
            nexus::shader::ShaderStage stage = nexus::shader::ShaderStage::Fragment;
            nexus::shader::parse_stage(params.value("stage", "fragment"), stage);
            std::string prepared = source;
            if (nexus::mcpe::has_multiversion_marker(prepared)) {
                const int version = params.value("glslVersion", 100);
                prepared = nexus::mcpe::prepare_multiversion_source(prepared, version);
            }
            const auto translation =
                nexus::shader::translate_glsl(prepared, stage, translation_options_from(params));
            result = translation.report;
            result["success"] = translation.success;
            result["source"] = translation.source;
            if (!translation.success) {
                error.code = nexus::bridge::error_code::kInternal;
                error.message = translation.error;
            }
            return true;
        });
    agent.register_method(
        "shader.analyze",
        [](const std::string&, const nlohmann::json& params, nlohmann::json& result,
           nexus::bridge::BridgeError& error,
           const nexus::bridge::BridgeRequestContext&) {
            const std::string source = params.value("source", "");
            if (source.empty()) {
                error.code = nexus::bridge::error_code::kInvalidParams;
                error.message = "shader.analyze requires a 'source' string";
                return false;
            }
            nexus::shader::ShaderStage stage = nexus::shader::ShaderStage::Fragment;
            nexus::shader::parse_stage(params.value("stage", "fragment"), stage);
            nexus::shader::PreprocessOptions preprocess_options;
            preprocess_options.stage = stage;
            preprocess_options.defines["__VERSION__"] = std::to_string(params.value("glslVersion", 100));
            const auto pre = nexus::shader::preprocess_glsl(source, preprocess_options);
            if (!pre.success) {
                error.code = nexus::bridge::error_code::kInternal;
                error.message = pre.error;
                return false;
            }
            nexus::shader::ParseOptions parse_options;
            parse_options.stage = stage;
            auto parsed = nexus::shader::parse_glsl(pre.source, parse_options);
            if (!parsed.success) {
                error.code = nexus::bridge::error_code::kInternal;
                error.message = parsed.error;
                return false;
            }
            parsed.unit.version = pre.version;
            parsed.unit.es = pre.es;
            result = nexus::shader::analyze_shader(parsed.unit, stage).to_json();
            return true;
        });
    agent.register_method(
        "mcpe.assets",
        [&](const std::string&, const nlohmann::json& params, nlohmann::json& result,
            nexus::bridge::BridgeError& error,
            const nexus::bridge::BridgeRequestContext&) {
            const std::string assets = params.value("assets", options.assets);
            if (assets.empty()) {
                error.code = nexus::bridge::error_code::kInvalidParams;
                error.message = "mcpe.assets requires an 'assets' path or --assets at startup";
                return false;
            }
            result = nexus::mcpe::inspect_assets(assets).to_json();
            return true;
        });
    agent.register_method(
        "worldgen.sample",
        [&](const std::string&, const nlohmann::json& params, nlohmann::json& result,
            nexus::bridge::BridgeError&, const nexus::bridge::BridgeRequestContext&) {
            const int x = params.value("x", 0);
            const int z = params.value("z", 0);
            result = {{"x", x}, {"z", z}, {"height", world.height_at(x, z)},
                      {"biome", nexus::worldgen::biome_name(world.biome_at(x, z))},
                      {"seed", world.seed()}};
            return true;
        });
    agent.register_method(
        "worldgen.chunk",
        [&](const std::string&, const nlohmann::json& params, nlohmann::json& result,
            nexus::bridge::BridgeError&, const nexus::bridge::BridgeRequestContext&) {
            const int cx = params.value("x", 0);
            const int cz = params.value("z", 0);
            const auto summary = world.summarize(world.generate_chunk(cx, cz));
            result = {{"x", cx}, {"z", cz}, {"minHeight", summary.minimum_height},
                      {"maxHeight", summary.maximum_height}, {"solid", summary.solid_blocks},
                      {"water", summary.water_blocks}, {"lava", summary.lava_blocks},
                      {"ores", summary.ore_blocks}, {"caves", summary.cave_blocks},
                      {"dominantBiome", nexus::worldgen::biome_name(
                           static_cast<nexus::worldgen::BiomeType>(summary.dominant_biome))}};
            return true;
        });
    agent.register_method(
        "mods.list",
        [&](const std::string&, const nlohmann::json&, nlohmann::json& result,
            nexus::bridge::BridgeError&, const nexus::bridge::BridgeRequestContext&) {
            result = mod_host.to_json();
            return true;
        });
    agent.register_method(
        "events.emit",
        [&](const std::string&, const nlohmann::json& params, nlohmann::json& result,
            nexus::bridge::BridgeError& error,
            const nexus::bridge::BridgeRequestContext&) {
            const std::string name = params.value("event", "");
            if (name.empty()) {
                error.code = nexus::bridge::error_code::kInvalidParams;
                error.message = "events.emit requires an 'event' string";
                return false;
            }
            const auto data = params.value("data", nlohmann::json::object());
            result = {{"event", name}, {"delivered", mod_host.emit(name, data)}};
            return true;
        });
    agent.register_method(
        "shutdown",
        [&](const std::string&, const nlohmann::json&, nlohmann::json& result,
            nexus::bridge::BridgeError&, const nexus::bridge::BridgeRequestContext&) {
            result = {{"stopping", true}};
            g_stop.store(true);
            return true;
        });
    {
        std::string error;
        if (!agent.start(error)) {
            std::cerr << "nexus-runtime: " << error << "\n";
            return 1;
        }
    }

    nexus::platform::Window window;
    nexus::graphics::GlApi gl;
    nexus::graphics::Renderer renderer(gl);
    bool rendering = false;
    if (options.demo) {
        nexus::platform::WindowConfig config;
        config.title = "NEXUS-MCPE runtime (validation scene, not the game)";
        config.width = 960;
        config.height = 540;
        config.vsync = true;
        std::string error;
        if (!window.create(config, error)) {
            NEXUS_LOG_WARN("demo window unavailable: " + error);
            options.demo = false;
        } else {
            const std::function<void*(const char*)> loader = [&window](const char* name) {
                return window.get_proc_address(name);
            };
            if (!gl.load(loader, error)) {
                NEXUS_LOG_WARN("OpenGL not usable: " + error);
                options.demo = false;
            } else {
                NEXUS_LOG_INFO("GL context: " + gl.info());
                const std::string vertex = constant_demo_vertex(window.is_gles());
                const std::string fragment = constant_demo_fragment(window.is_gles());
                if (!renderer.initialize(vertex, fragment, error)) {
                    NEXUS_LOG_WARN("demo shader failed: " + error);
                    options.demo = false;
                } else {
                    std::string texture_error;
                    if (!renderer.create_checker_texture(64, texture_error)) {
                        NEXUS_LOG_WARN("demo texture failed: " + texture_error);
                        options.demo = false;
                    } else {
                        rendering = true;
                    }
                }
            }
        }
    }

    if (rendering) agent.publish("runtime.demo.ready", {{"renderer", gl.renderer()}});
    agent.publish("runtime.ready", {{"socket", options.socket_path},
                                    {"isGameClient", false},
                                    {"mode", rendering ? "demo" : "headless"}});

    const auto started = std::chrono::steady_clock::now();
    bool screenshot_saved = false;
    while (!g_stop.load()) {
        if (options.seconds > 0) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - started).count();
            if (elapsed >= static_cast<long long>(options.seconds) * 1000) break;
        }
        if (rendering) {
            nexus::platform::InputSnapshot input;
            if (!window.pump_events(input)) break;
            if (input.key_pressed[static_cast<int>(nexus::platform::Key::Escape)]) break;
            const double seconds = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - started).count();
            renderer.render(seconds, window.width(), window.height());
            if (!options.screenshot.empty() && !screenshot_saved && seconds > 0.15) {
                std::string error;
                if (renderer.capture_png(options.screenshot, window.width(), window.height(),
                                         error)) {
                    NEXUS_LOG_INFO("screenshot written to " + options.screenshot);
                    screenshot_saved = true;
                } else {
                    NEXUS_LOG_WARN("screenshot failed: " + error);
                    screenshot_saved = true;
                }
            }
            window.swap_buffers();
            std::this_thread::sleep_for(std::chrono::milliseconds(8));
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    agent.publish("runtime.stopping", {{"reason", "signal-or-timer"}});
    renderer.shutdown();
    window.destroy();
    agent.stop();
    mod_host.unload_all();
    nexus::Logger::instance().close_log_file();
    std::cout << "nexus-runtime stopped\n";
    return 0;
}
