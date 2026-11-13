#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace nexus::platform {

struct ApkManifest {
    std::string package = "";
    std::string version_name = "";
    std::string application_label = "";
    std::string main_activity = "";
    std::string native_lib_name = "";
    int version_code = -1;
    int min_sdk = -1;
    int target_sdk = -1;
    int gl_es_version = -1;
    bool debuggable = false;
    bool has_internet_permission = false;
};

struct ElfInfo {
    bool valid = false;
    bool elf32 = false;
    bool elf64 = false;
    bool little_endian = true;
    uint16_t machine = 0;
    std::string machine_name = "unknown";
};

struct ApkAbi {
    std::string abi;
    std::vector<std::string> libraries;
    std::string primary_library;
    ElfInfo primary_elf;
};

struct ApkAssetFile {
    std::string path;
    uint64_t size = 0;
    uint32_t crc32 = 0;
};

struct ApkInfo {
    std::string path;
    std::string sha256;
    uint64_t size = 0;
    bool valid_apk = false;
    ApkManifest manifest;
    std::vector<ApkAbi> abis;
    std::vector<ApkAssetFile> shaders;
    std::vector<ApkAssetFile> materials;
    std::vector<ApkAssetFile> ui_files;
    std::vector<std::string> asset_directories;
    std::vector<std::string> issues;
    std::vector<std::string> warnings;

    bool has_abi(const std::string& abi) const;
    bool has_x86() const { return has_abi("x86"); }
    bool has_x86_64() const { return has_abi("x86_64"); }
    bool has_arm() const;
    bool x86_port_ready() const;
    const ApkAbi* find_abi(const std::string& abi) const;
    nlohmann::json to_json() const;
    std::string to_markdown() const;
};

// Inspects an APK without extracting or modifying it.  Reads the binary
// AndroidManifest.xml, enumerates native libraries and checks their ELF
// headers.  All errors are reported through issues; missing data never causes
// fabricated values.
ApkInfo probe_apk(const std::string& path);

ElfInfo inspect_elf_header(const uint8_t* data, size_t size);
const char* elf_machine_name(uint16_t machine);

} // namespace nexus::platform
