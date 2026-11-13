#include <nexus/core/version.hpp>

namespace nexus {

namespace {
const GameVersionCode kVersionCodes[] = {
    {720140300, GameArchitecture::ArmeabiV7aGb, "Android 2.3+ (Gingerbread)"},
    {720140301, GameArchitecture::ArmeabiV7aGb, "Android 2.3+ (Gingerbread), reupload"},
    {740140300, GameArchitecture::ArmeabiV7aHc, "Android 3.2+ (Honeycomb)"},
    {740140301, GameArchitecture::ArmeabiV7aHc, "Android 3.2+ (Honeycomb), reupload"},
    {760140300, GameArchitecture::ArmeabiV7aJb, "Android 4.1+ (Jelly Bean)"},
    {760140301, GameArchitecture::ArmeabiV7aJb, "Android 4.1+ (Jelly Bean), reupload"},
    {780140300, GameArchitecture::X86, "x86"},
    {780140301, GameArchitecture::X86, "x86, reupload"},
};
} // namespace

const GameVersionCode* game_version_codes(size_t& count) {
    count = sizeof(kVersionCodes) / sizeof(kVersionCodes[0]);
    return kVersionCodes;
}

const GameVersionCode* find_game_version_code(int code) {
    for (const auto& entry : kVersionCodes) {
        if (entry.code == code) return &entry;
    }
    return nullptr;
}

const char* game_architecture_name(GameArchitecture architecture) {
    switch (architecture) {
        case GameArchitecture::ArmeabiV7aGb: return "armeabi-v7a (Gingerbread)";
        case GameArchitecture::ArmeabiV7aHc: return "armeabi-v7a (Honeycomb)";
        case GameArchitecture::ArmeabiV7aJb: return "armeabi-v7a (Jelly Bean)";
        case GameArchitecture::X86: return "x86";
        case GameArchitecture::Unknown: break;
    }
    return "unknown";
}

bool is_x86_version_code(int code) {
    const GameVersionCode* entry = find_game_version_code(code);
    return entry != nullptr && entry->architecture == GameArchitecture::X86;
}

bool is_supported_x86_version(int code) {
    return code == 780140300 || code == 780140301;
}

std::string version_string() {
    return std::string("NEXUS-MCPE ") + kVersion + " (Minecraft PE " + kGameVersion + ")";
}

} // namespace nexus
