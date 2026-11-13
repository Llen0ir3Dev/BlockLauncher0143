#include <nexus/platform/provisioning.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/log.hpp>
#include <nexus/core/sha256.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/time.hpp>
#include <nexus/core/version.hpp>
#include <nexus/core/zip.hpp>

namespace nexus::platform {

namespace {

std::string short_hash(const std::string& hash) {
    return hash.size() >= 12 ? hash.substr(0, 12) : hash;
}

std::string installation_id(const ApkInfo& apk) {
    std::string abi = "unknown";
    if (apk.has_x86()) abi = "x86";
    else if (apk.has_abi("armeabi-v7a")) abi = "armeabiv7a";
    else if (apk.has_arm()) abi = "arm";
    return "mcpe-" + apk.manifest.version_name + "-" +
           std::to_string(apk.manifest.version_code) + "-" + abi + "-" + short_hash(apk.sha256);
}

bool write_manifest(const GameInstallation& installation, std::string& error) {
    const std::string path = fs::join(installation.root, "installation.json");
    if (!fs::write_file(path, installation.to_json().dump(2) + "\n")) {
        error = "cannot write " + path;
        return false;
    }
    return true;
}

} // namespace

nlohmann::json GameInstallation::to_json() const {
    return {
        {"id", id}, {"name", name}, {"root", root}, {"apkPath", apk_path},
        {"sha256", sha256}, {"versionName", version_name}, {"versionCode", version_code},
        {"abis", abis}, {"x86Ready", x86_ready},
        {"librariesExtracted", libraries_extracted},
        {"assetsExtracted", assets_extracted}, {"apkSize", apk_size},
        {"createdAt", created_at}, {"sourcePath", source_path}, {"warnings", warnings}
    };
}

bool GameInstallation::from_json(const nlohmann::json& json, GameInstallation& out,
                                 std::string& error) {
    if (!json.is_object()) {
        error = "installation manifest is not an object";
        return false;
    }
    auto string_or = [&](const char* key) -> std::string {
        const auto it = json.find(key);
        return it != json.end() && it->is_string() ? it->get<std::string>() : "";
    };
    out.id = string_or("id");
    out.name = string_or("name");
    out.root = string_or("root");
    out.apk_path = string_or("apkPath");
    out.sha256 = string_or("sha256");
    out.version_name = string_or("versionName");
    out.created_at = string_or("createdAt");
    out.source_path = string_or("sourcePath");
    out.version_code = json.value("versionCode", -1);
    out.x86_ready = json.value("x86Ready", false);
    out.libraries_extracted = json.value("librariesExtracted", false);
    out.assets_extracted = json.value("assetsExtracted", false);
    out.apk_size = json.value("apkSize", static_cast<uint64_t>(0));
    if (json.contains("abis") && json["abis"].is_array()) {
        for (const auto& abi : json["abis"]) {
            if (abi.is_string()) out.abis.push_back(abi.get<std::string>());
        }
    }
    if (json.contains("warnings") && json["warnings"].is_array()) {
        for (const auto& warning : json["warnings"]) {
            if (warning.is_string()) out.warnings.push_back(warning.get<std::string>());
        }
    }
    if (out.id.empty() || out.apk_path.empty()) {
        error = "installation manifest is missing id or apkPath";
        return false;
    }
    return true;
}

std::string default_installs_root() {
    return fs::join(fs::nexus_home(), "installs");
}

bool create_installation(const std::string& apk_path, const std::string& installs_root,
                         const InstallationOptions& options, GameInstallation& installation,
                         std::string& error) {
    if (!fs::is_regular_file(apk_path)) {
        error = "APK does not exist: " + apk_path;
        return false;
    }
    ApkInfo apk = probe_apk(apk_path);
    if (!apk.valid_apk) {
        error = "APK is not a valid Minecraft PE package";
        for (const auto& issue : apk.issues) error += "; " + issue;
        return false;
    }
    installation = GameInstallation{};
    installation.id = installation_id(apk);
    installation.name = "Minecraft PE " + apk.manifest.version_name + " (" +
                        std::to_string(apk.manifest.version_code) + ")";
    installation.root = fs::join(installs_root, installation.id);
    installation.apk_path = fs::join(installation.root, "game.apk");
    installation.sha256 = apk.sha256;
    installation.version_name = apk.manifest.version_name;
    installation.version_code = apk.manifest.version_code;
    installation.x86_ready = apk.x86_port_ready();
    installation.apk_size = apk.size;
    installation.created_at = iso8601_now();
    installation.source_path = fs::absolute(apk_path);
    installation.warnings = apk.warnings;
    for (const auto& abi : apk.abis) installation.abis.push_back(abi.abi);

    if (fs::exists(installation.root) && !options.overwrite) {
        error = "installation already exists: " + installation.root +
                " (use overwrite to replace it)";
        return false;
    }
    if (!fs::ensure_directory(installation.root)) {
        error = "cannot create installation directory: " + installation.root;
        return false;
    }

    if (options.copy_apk) {
        if (fs::canonical(apk_path) != fs::canonical(installation.apk_path)) {
            if (!fs::copy_file(apk_path, installation.apk_path)) {
                error = "cannot copy APK into installation directory";
                return false;
            }
        }
    } else {
        installation.apk_path = fs::absolute(apk_path);
    }

    ZipReader zip;
    std::string zip_error;
    if (!zip.open(apk_path, zip_error)) {
        error = "cannot open APK for extraction: " + zip_error;
        return false;
    }

    if (options.extract_libraries) {
        bool extracted = false;
        for (const auto& entry : zip.entries()) {
            if (entry.is_directory) continue;
            if (!starts_with(entry.name, "lib/") || !ends_with(entry.name, ".so")) continue;
            const auto first = entry.name.find('/');
            const auto second = entry.name.find('/', first + 1);
            if (first == std::string::npos || second == std::string::npos) continue;
            const std::string abi = entry.name.substr(first + 1, second - first - 1);
            const std::string library = basename(entry.name);
            const std::string destination =
                fs::join(installation.root, fs::join("libraries", fs::join(abi, library)));
            std::vector<uint8_t> data;
            std::string read_error;
            if (!zip.read(entry.name, data, read_error, options.max_entry_bytes)) {
                error = "cannot extract " + entry.name + ": " + read_error;
                return false;
            }
            if (!fs::write_file_binary(destination, data)) {
                error = "cannot write " + destination;
                return false;
            }
            extracted = true;
        }
        installation.libraries_extracted = extracted;
    }

    if (options.extract_assets) {
        bool extracted = false;
        for (const auto& entry : zip.entries()) {
            if (entry.is_directory || !starts_with(entry.name, "assets/")) continue;
            const std::string relative = fs::sanitize_relative_path(entry.name.substr(7));
            if (relative.empty()) continue;
            const std::string destination = fs::join(installation.root, fs::join("assets", relative));
            if (!fs::is_safe_relative_path(fs::join("assets", relative))) continue;
            std::vector<uint8_t> data;
            std::string read_error;
            if (!zip.read(entry.name, data, read_error, options.max_entry_bytes)) {
                installation.warnings.push_back("skipped " + entry.name + ": " + read_error);
                continue;
            }
            if (!fs::write_file_binary(destination, data)) {
                error = "cannot write " + destination;
                return false;
            }
            extracted = true;
        }
        installation.assets_extracted = extracted;
    }

    if (!write_manifest(installation, error)) return false;
    NEXUS_LOG_INFO("installed " + installation.name + " at " + installation.root);
    return true;
}

bool discover_installations(const std::string& installs_root,
                            std::vector<GameInstallation>& installations) {
    installations.clear();
    if (!fs::is_directory(installs_root)) return true;
    for (const auto& directory : fs::list_directories(installs_root)) {
        const std::string manifest_path = fs::join(directory, "installation.json");
        std::string text;
        if (!fs::read_file(manifest_path, text, 4 * 1024 * 1024)) continue;
        nlohmann::json json;
        try {
            json = nlohmann::json::parse(text);
        } catch (const std::exception&) {
            continue;
        }
        GameInstallation installation;
        std::string error;
        if (!GameInstallation::from_json(json, installation, error)) continue;
        if (installation.root.empty()) installation.root = directory;
        installations.push_back(std::move(installation));
    }
    std::sort(installations.begin(), installations.end(),
              [](const GameInstallation& a, const GameInstallation& b) {
                  return a.created_at > b.created_at;
              });
    return true;
}

bool verify_installation(const GameInstallation& installation, std::string& error) {
    if (!fs::is_regular_file(installation.apk_path)) {
        error = "installed APK is missing: " + installation.apk_path;
        return false;
    }
    const std::string hash = Sha256::file_hex(installation.apk_path);
    if (!installation.sha256.empty() && hash != installation.sha256) {
        error = "installed APK hash mismatch (expected " + installation.sha256 + ", got " +
                hash + ")";
        return false;
    }
    const ApkInfo apk = probe_apk(installation.apk_path);
    if (!apk.valid_apk) {
        error = "installed APK no longer validates as a Minecraft PE package";
        return false;
    }
    if (apk.manifest.version_code != installation.version_code) {
        error = "installed APK version code changed";
        return false;
    }
    if (installation.libraries_extracted) {
        for (const auto& abi : installation.abis) {
            const std::string directory =
                fs::join(installation.root, fs::join("libraries", abi));
            if (fs::list_files(directory, ".so").empty()) {
                error = "extracted libraries are missing for ABI " + abi;
                return false;
            }
        }
    }
    return true;
}

bool remove_installation(const GameInstallation& installation, std::string& error,
                         const std::string& installs_root) {
    const std::string root = fs::canonical(installation.root);
    const std::string installs =
        fs::canonical(installs_root.empty() ? default_installs_root() : installs_root);
    if (root == installs || root.find(installs + "/") != 0) {
        error = "refusing to remove a directory outside the NEXUS installs root";
        return false;
    }
    if (!fs::remove_all(root)) {
        error = "cannot remove installation directory: " + root;
        return false;
    }
    return true;
}

bool extract_apk_entry(const std::string& apk_path, const std::string& entry,
                       const std::string& destination, std::string& error) {
    if (!fs::is_safe_relative_path(entry)) {
        error = "unsafe APK entry path: " + entry;
        return false;
    }
    ZipReader zip;
    std::string zip_error;
    if (!zip.open(apk_path, zip_error)) {
        error = "cannot open APK: " + zip_error;
        return false;
    }
    std::vector<uint8_t> data;
    std::string read_error;
    if (!zip.read(entry, data, read_error, 256ull * 1024 * 1024)) {
        error = "cannot read APK entry '" + entry + "': " + read_error;
        return false;
    }
    if (!fs::write_file_binary(destination, data)) {
        error = "cannot write " + destination;
        return false;
    }
    return true;
}

} // namespace nexus::platform
