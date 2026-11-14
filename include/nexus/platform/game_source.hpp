#pragma once

#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/platform/apk.hpp>

namespace nexus::platform {

// Where a candidate game package was discovered.  Kept explicit so the user
// (and the launcher) can see why a file was considered.
enum class GameSourceOrigin {
    Explicit,      // path supplied on the command line / launcher argument
    EnvGameDir,    // $NEXUS_GAME_DIR
    AppImage,      // alongside the running AppImage (game/ or its directory)
    NexusHome,     // $NEXUS_HOME/game
    WorkingDir,    // ./game or ./
    UserFolder     // ~/NEXUS-MCPE/game or ~/NEXUS-MCPE
};

const char* game_source_origin_name(GameSourceOrigin origin);

struct GameSourceCandidate {
    std::string path;         // APK path, or directory that was searched
    std::string origin;       // human-readable origin label
    bool present = false;     // the file/directory exists
    bool is_apk = false;      // candidate points at an .apk file
    bool valid = false;       // APK parsed and contains libminecraftpe.so
    bool has_minecraft_library = false;  // libminecraftpe.so found in the APK
    std::string library_path; // e.g. lib/armeabi-v7a/libminecraftpe.so
    bool arm32 = false;       // the game library is 32-bit ARM
    bool executable = false;  // safe to launch on this build (compat layer ready)
    std::string executable_reason;
    std::string reason;       // explanation when not valid
    ApkInfo apk;              // populated when is_apk and parsing succeeded

    nlohmann::json to_json() const;
};

struct GameSourceReport {
    std::vector<GameSourceCandidate> candidates;
    bool found = false;
    std::string chosen;       // path of the first valid APK, empty when none
    std::string error;        // populated when nothing usable was found

    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

// Searches the well-known locations for a user-supplied Minecraft PE APK.
// This function is read-only: it never copies, extracts or modifies anything.
// An optional explicit path (file or directory) is checked first.
GameSourceReport detect_game_sources(const std::string& explicit_path = "");

// Scans one directory (non-recursively) for .apk files and evaluates each.
std::vector<GameSourceCandidate> scan_directory_for_apks(const std::string& directory,
                                                         const std::string& origin);

} // namespace nexus::platform
