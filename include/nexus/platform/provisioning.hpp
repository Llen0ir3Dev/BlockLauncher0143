#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <nexus/platform/apk.hpp>

namespace nexus::platform {

struct InstallationOptions {
    bool copy_apk = true;
    bool extract_libraries = true;
    bool extract_assets = false;
    bool overwrite = false;
    uint64_t max_entry_bytes = 256ull * 1024 * 1024;
};

struct GameInstallation {
    std::string id;
    std::string name;
    std::string root;
    std::string apk_path;
    std::string sha256;
    std::string version_name;
    int version_code = -1;
    std::vector<std::string> abis;
    bool x86_ready = false;
    bool libraries_extracted = false;
    bool assets_extracted = false;
    uint64_t apk_size = 0;
    std::string created_at;
    std::string source_path;
    std::vector<std::string> warnings;

    nlohmann::json to_json() const;
    static bool from_json(const nlohmann::json& json, GameInstallation& out, std::string& error);
};

// Creates a local installation directory from a user-supplied APK.  The source
// APK is inspected first and is never modified or deleted.  Extracted files are
// for local execution/analysis only and are excluded from distribution.
bool create_installation(const std::string& apk_path, const std::string& installs_root,
                         const InstallationOptions& options, GameInstallation& installation,
                         std::string& error);

bool discover_installations(const std::string& installs_root,
                            std::vector<GameInstallation>& installations);
bool verify_installation(const GameInstallation& installation, std::string& error);
bool remove_installation(const GameInstallation& installation, std::string& error,
                         const std::string& installs_root = "");

// Extracts one asset from an installed APK to a destination path.  Used by the
// shader/texture importers without ever touching the original APK.
bool extract_apk_entry(const std::string& apk_path, const std::string& entry,
                       const std::string& destination, std::string& error);

std::string default_installs_root();

} // namespace nexus::platform
