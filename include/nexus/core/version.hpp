#pragma once

#include <string>

#ifndef NEXUS_VERSION
#define NEXUS_VERSION "0.1.0"
#endif

namespace nexus {
inline constexpr const char* kVersion = NEXUS_VERSION;
inline constexpr int kModAbiVersion = 1;
inline constexpr const char* kGameVersion = "0.14.3";

// Version codes published for Pocket Edition 0.14.3.  The code is not
// interchangeable between CPU architectures: only 780140300 and 780140301
// contain x86 native libraries, which is what an x86_64 Linux port can load.
enum class GameArchitecture {
    Unknown,
    ArmeabiV7aGb,
    ArmeabiV7aHc,
    ArmeabiV7aJb,
    X86
};

struct GameVersionCode {
    int code;
    GameArchitecture architecture;
    const char* android_runtime;
};

const GameVersionCode* game_version_codes(size_t& count);
const GameVersionCode* find_game_version_code(int code);
const char* game_architecture_name(GameArchitecture architecture);
bool is_x86_version_code(int code);
// Returns true only for a verified x86 0.14.3 version code.
bool is_supported_x86_version(int code);

std::string version_string();

} // namespace nexus
