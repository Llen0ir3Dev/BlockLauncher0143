#include <nexus/platform/game_source.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/version.hpp>

namespace nexus::platform {

namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool has_apk_extension(const std::string& path) {
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) return false;
    return lower(path.substr(dot)) == ".apk";
}

std::string dirname_of(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    if (slash == 0) return "/";
    return path.substr(0, slash);
}

// Evaluate one APK path.  Never throws; all failures become a reason string.
GameSourceCandidate evaluate_apk(const std::string& path, const std::string& origin) {
    GameSourceCandidate candidate;
    candidate.path = path;
    candidate.origin = origin;
    candidate.present = fs::is_regular_file(path);
    candidate.is_apk = true;
    if (!candidate.present) {
        candidate.reason = "file not found";
        return candidate;
    }
    candidate.apk = probe_apk(path);
    if (!candidate.apk.valid_apk) {
        candidate.reason = "not a valid APK";
        if (!candidate.apk.issues.empty()) candidate.reason += ": " + candidate.apk.issues.front();
        return candidate;
    }

    // Locate libminecraftpe.so and confirm its ELF architecture.  An APK is only
    // "valid" for NEXUS when it actually carries the game library; a random
    // Android package must never be mistaken for the game.
    for (const auto& abi : candidate.apk.abis) {
        for (const auto& library : abi.libraries) {
            const std::string name = nexus::basename(library);
            if (name != "libminecraftpe.so") continue;
            candidate.has_minecraft_library = true;
            candidate.library_path = "lib/" + abi.abi + "/" + name;
            candidate.arm32 = abi.primary_elf.elf32 &&
                              abi.primary_elf.machine == 40 /* EM_ARM */;
            break;
        }
        if (candidate.has_minecraft_library) break;
    }
    if (!candidate.has_minecraft_library) {
        candidate.reason = "APK does not contain libminecraftpe.so";
        return candidate;
    }
    if (!candidate.apk.has_arm() && !candidate.apk.has_x86() &&
        !candidate.apk.has_x86_64()) {
        candidate.reason = "APK contains no supported native library";
        return candidate;
    }
    candidate.valid = true;

    // A verified APK is not yet a runnable game: the Linux x86_64 compatibility
    // layer (in-tree ARM32 loader + bionic shims + GPU/audio/input bridges) is
    // still under development.  Never report otherwise.
    candidate.executable = false;
    candidate.executable_reason =
        "verified package; ARM32 execution layer not complete yet";

    std::ostringstream note;
    note << "Minecraft PE " << (candidate.apk.manifest.version_name.empty()
                                    ? std::string("(unknown version)")
                                    : candidate.apk.manifest.version_name)
         << " (" << candidate.apk.manifest.version_code << ")"
         << (candidate.arm32 ? ", ARM32" : ", non-ARM32");
    candidate.reason = note.str();
    return candidate;
}

// Add a directory scan, but only when the directory exists.
void consider_directory(const std::string& directory, const std::string& origin,
                        std::vector<GameSourceCandidate>& out) {
    if (directory.empty() || !fs::is_directory(directory)) return;
    const auto found = scan_directory_for_apks(directory, origin);
    out.insert(out.end(), found.begin(), found.end());
}

} // namespace

const char* game_source_origin_name(GameSourceOrigin origin) {
    switch (origin) {
        case GameSourceOrigin::Explicit: return "explicit path";
        case GameSourceOrigin::EnvGameDir: return "$NEXUS_GAME_DIR";
        case GameSourceOrigin::AppImage: return "AppImage folder";
        case GameSourceOrigin::NexusHome: return "$NEXUS_HOME/game";
        case GameSourceOrigin::WorkingDir: return "working directory";
        case GameSourceOrigin::UserFolder: return "~/NEXUS-MCPE";
    }
    return "unknown";
}

nlohmann::json GameSourceCandidate::to_json() const {
    nlohmann::json json;
    json["path"] = path;
    json["origin"] = origin;
    json["present"] = present;
    json["isApk"] = is_apk;
    json["valid"] = valid;
    json["hasMinecraftLibrary"] = has_minecraft_library;
    json["libraryPath"] = library_path;
    json["arm32"] = arm32;
    json["executable"] = executable;
    json["executableReason"] = executable_reason;
    json["reason"] = reason;
    if (is_apk && !apk.path.empty()) {
        json["versionName"] = apk.manifest.version_name;
        json["versionCode"] = apk.manifest.version_code;
        json["abis"] = nlohmann::json::array();
        for (const auto& abi : apk.abis) json["abis"].push_back(abi.abi);
        json["hasArm"] = apk.has_arm();
        json["hasX86"] = apk.has_x86();
        json["sha256"] = apk.sha256;
        json["size"] = apk.size;
    }
    return json;
}

nlohmann::json GameSourceReport::to_json() const {
    nlohmann::json json;
    json["found"] = found;
    json["chosen"] = chosen;
    json["error"] = error;
    json["candidates"] = nlohmann::json::array();
    for (const auto& candidate : candidates) json["candidates"].push_back(candidate.to_json());
    return json;
}

std::string GameSourceReport::to_markdown() const {
    std::ostringstream out;
    out << "# Game package detection\n\n";
    out << "- Found: " << (found ? "yes" : "no") << "\n";
    if (!chosen.empty()) out << "- Chosen: `" << chosen << "`\n";
    if (!error.empty()) out << "- Error: " << error << "\n";
    out << "\n";
    if (candidates.empty()) {
        out << "No APK candidates were found in any searched location.\n";
        return out.str();
    }
    out << "| Path | Origin | Present | Valid | libminecraftpe.so | Executable | Detail |\n";
    out << "| --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& candidate : candidates) {
        out << "| `" << candidate.path << "` | " << candidate.origin << " | "
            << (candidate.present ? "yes" : "no") << " | "
            << (candidate.valid ? "yes" : "no") << " | "
            << (candidate.has_minecraft_library ? "yes" : "no") << " | "
            << (candidate.executable ? "yes" : "no") << " | " << candidate.reason
            << (candidate.executable_reason.empty() ? "" : " (" + candidate.executable_reason + ")")
            << " |\n";
    }
    return out.str();
}

std::vector<GameSourceCandidate> scan_directory_for_apks(const std::string& directory,
                                                         const std::string& origin) {
    std::vector<GameSourceCandidate> out;
    if (!fs::is_directory(directory)) return out;
    for (const auto& entry : fs::list_files(directory)) {
        if (!has_apk_extension(entry)) continue;
        out.push_back(evaluate_apk(entry, origin));
    }
    std::sort(out.begin(), out.end(),
              [](const GameSourceCandidate& a, const GameSourceCandidate& b) {
                  if (a.valid != b.valid) return a.valid > b.valid;
                  return a.path < b.path;
              });
    return out;
}

GameSourceReport detect_game_sources(const std::string& explicit_path) {
    GameSourceReport report;
    std::vector<GameSourceCandidate>& candidates = report.candidates;

    auto consider_path = [&](const std::string& path, const std::string& origin) {
        if (path.empty()) return;
        if (fs::is_regular_file(path)) {
            if (has_apk_extension(path)) {
                candidates.push_back(evaluate_apk(path, origin));
            }
        } else if (fs::is_directory(path)) {
            consider_directory(path, origin, candidates);
        }
    };

    // 1. Explicit path (file or directory).
    consider_path(explicit_path, game_source_origin_name(GameSourceOrigin::Explicit));

    // 2. $NEXUS_GAME_DIR
    if (const char* game_dir = std::getenv("NEXUS_GAME_DIR")) {
        consider_path(game_dir, game_source_origin_name(GameSourceOrigin::EnvGameDir));
    }

    // 3. Next to the running AppImage (the documented NEXUS-MCPE/game layout).
    if (const char* appimage = std::getenv("APPIMAGE")) {
        const std::string base = dirname_of(appimage);
        consider_directory(fs::join(base, "game"),
                           game_source_origin_name(GameSourceOrigin::AppImage), candidates);
        consider_directory(base, game_source_origin_name(GameSourceOrigin::AppImage), candidates);
    }

    // 4. $NEXUS_HOME/game
    consider_directory(fs::join(fs::nexus_home(), "game"),
                       game_source_origin_name(GameSourceOrigin::NexusHome), candidates);

    // 5. ./game then ./
    consider_directory("game", game_source_origin_name(GameSourceOrigin::WorkingDir), candidates);
    consider_directory(".", game_source_origin_name(GameSourceOrigin::WorkingDir), candidates);

    // 6. ~/NEXUS-MCPE/game and ~/NEXUS-MCPE
    const std::string user_root = fs::join(fs::home_dir(), "NEXUS-MCPE");
    consider_directory(fs::join(user_root, "game"),
                       game_source_origin_name(GameSourceOrigin::UserFolder), candidates);
    consider_directory(user_root, game_source_origin_name(GameSourceOrigin::UserFolder), candidates);

    // De-duplicate by absolute path while preserving order and the first valid.
    std::vector<GameSourceCandidate> unique;
    std::vector<std::string> seen;
    for (auto& candidate : candidates) {
        const std::string key = fs::absolute(candidate.path);
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        unique.push_back(std::move(candidate));
    }
    candidates = std::move(unique);

    for (const auto& candidate : candidates) {
        if (candidate.valid) {
            report.found = true;
            report.chosen = candidate.path;
            return report;
        }
    }

    if (candidates.empty()) {
        report.error = "no APK found in any searched location";
    } else {
        report.error = "APK candidates found but none is a usable Minecraft PE package";
    }
    return report;
}

} // namespace nexus::platform
