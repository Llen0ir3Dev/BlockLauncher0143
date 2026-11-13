#include <nexus/platform/apk.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

#include <nexus/core/fs.hpp>
#include <nexus/core/sha256.hpp>
#include <nexus/core/strings.hpp>
#include <nexus/core/version.hpp>
#include <nexus/core/zip.hpp>

namespace nexus::platform {

namespace {

constexpr uint16_t RES_STRING_POOL = 0x0001;
constexpr uint16_t RES_XML_START_ELEMENT = 0x0102;
constexpr uint16_t RES_XML_RESOURCE_MAP = 0x0180;

constexpr uint8_t TYPE_REFERENCE = 0x01;
constexpr uint8_t TYPE_STRING = 0x03;
constexpr uint8_t TYPE_INT_DEC = 0x10;
constexpr uint8_t TYPE_INT_HEX = 0x11;
constexpr uint8_t TYPE_INT_BOOLEAN = 0x12;

uint16_t rd16(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 2 > data.size()) return 0;
    return static_cast<uint16_t>(data[pos] | (data[pos + 1] << 8));
}

uint32_t rd32(const std::vector<uint8_t>& data, size_t pos) {
    if (pos + 4 > data.size()) return 0;
    return static_cast<uint32_t>(data[pos]) | (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) | (static_cast<uint32_t>(data[pos + 3]) << 24);
}

struct AxmlValue {
    std::string name;
    std::string string_value;
    int64_t int_value = 0;
    uint8_t type = 0;
    uint32_t raw = 0xffffffffu;

    bool as_bool() const { return type == TYPE_INT_BOOLEAN ? int_value != 0 : false; }
    std::string display() const {
        if (type == TYPE_STRING) return string_value;
        if (type == TYPE_INT_DEC || type == TYPE_INT_HEX) return std::to_string(int_value);
        if (type == TYPE_INT_BOOLEAN) return int_value ? "true" : "false";
        if (type == TYPE_REFERENCE) {
            std::ostringstream out;
            out << "reference:0x" << std::hex << static_cast<uint32_t>(int_value);
            return out.str();
        }
        return std::to_string(int_value);
    }
};

struct AxmlElement {
    std::string name;
    std::vector<AxmlValue> attributes;
    const AxmlValue* attribute(const std::string& attr) const {
        for (const auto& value : attributes) {
            if (value.name == attr) return &value;
        }
        return nullptr;
    }
};

class AxmlParser {
public:
    bool parse(const std::vector<uint8_t>& data, std::vector<AxmlElement>& elements,
               std::string& error) {
        if (data.size() < 8 || rd32(data, 0) != 0x00080003) {
            error = "AndroidManifest.xml is not a binary XML document";
            return false;
        }
        strings_.clear();
        elements.clear();
        size_t pos = 8;
        while (pos + 8 <= data.size()) {
            const uint16_t type = rd16(data, pos);
            const uint16_t header_size = rd16(data, pos + 2);
            const uint32_t chunk_size = rd32(data, pos + 4);
            if (chunk_size < 8 || pos + chunk_size > data.size()) {
                error = "truncated binary XML chunk";
                return false;
            }
            if (type == RES_STRING_POOL) {
                if (!parse_string_pool(data, pos, header_size, chunk_size, error)) return false;
            } else if (type == RES_XML_START_ELEMENT) {
                if (!parse_start_element(data, pos, header_size, elements, error)) return false;
            }
            pos += chunk_size;
        }
        return true;
    }

private:
    bool parse_string_pool(const std::vector<uint8_t>& data, size_t pos, uint16_t header_size,
                           uint32_t chunk_size, std::string& error) {
        (void)header_size;
        (void)chunk_size;
        const uint32_t count = rd32(data, pos + 8);
        const uint32_t flags = rd32(data, pos + 16);
        const uint32_t strings_start = rd32(data, pos + 20);
        const bool utf8 = (flags & 0x100u) != 0;
        if (count > 200000 || pos + 28 + static_cast<uint64_t>(count) * 4 > data.size()) {
            error = "invalid string pool";
            return false;
        }
        strings_.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint32_t offset = rd32(data, pos + 28 + i * 4);
            size_t cursor = pos + strings_start + offset;
            if (cursor >= data.size()) {
                strings_.push_back("");
                continue;
            }
            if (utf8) {
                auto read_len8 = [&]() -> uint32_t {
                    uint32_t value = data[cursor++];
                    if (value & 0x80) {
                        if (cursor >= data.size()) return 0;
                        value = ((value & 0x7f) << 8) | data[cursor++];
                    }
                    return value;
                };
                read_len8(); // UTF-16 length (unused)
                const uint32_t byte_length = read_len8();
                if (cursor + byte_length > data.size()) {
                    strings_.push_back("");
                    continue;
                }
                strings_.push_back(std::string(reinterpret_cast<const char*>(data.data() + cursor),
                                               byte_length));
            } else {
                uint32_t length = rd16(data, cursor);
                cursor += 2;
                if (length & 0x8000) {
                    length = ((length & 0x7fff) << 16) | rd16(data, cursor);
                    cursor += 2;
                }
                if (cursor + static_cast<uint64_t>(length) * 2 > data.size()) {
                    strings_.push_back("");
                    continue;
                }
                std::string value;
                value.reserve(length);
                for (uint32_t c = 0; c < length; ++c) {
                    const uint16_t ch = rd16(data, cursor + c * 2);
                    if (ch < 0x80) value.push_back(static_cast<char>(ch));
                    else if (ch < 0x800) {
                        value.push_back(static_cast<char>(0xc0 | (ch >> 6)));
                        value.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
                    } else {
                        value.push_back(static_cast<char>(0xe0 | (ch >> 12)));
                        value.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3f)));
                        value.push_back(static_cast<char>(0x80 | (ch & 0x3f)));
                    }
                }
                strings_.push_back(std::move(value));
            }
        }
        return true;
    }

    bool parse_start_element(const std::vector<uint8_t>& data, size_t pos, uint16_t header_size,
                             std::vector<AxmlElement>& elements, std::string& error) {
        const size_t ext = pos + header_size;
        if (ext + 20 > data.size()) {
            error = "truncated start element";
            return false;
        }
        const uint32_t name_index = rd32(data, ext + 4);
        const uint16_t attr_start = rd16(data, ext + 8);
        const uint16_t attr_size = rd16(data, ext + 10);
        const uint16_t attr_count = rd16(data, ext + 12);
        if (attr_size < 20 || attr_count > 1000) {
            error = "invalid attribute table";
            return false;
        }
        AxmlElement element;
        element.name = name_index < strings_.size() ? strings_[name_index] : "?";
        size_t attr_pos = ext + attr_start;
        for (uint16_t i = 0; i < attr_count; ++i) {
            if (attr_pos + 20 > data.size()) {
                error = "truncated attribute";
                return false;
            }
            AxmlValue value;
            value.raw = rd32(data, attr_pos + 8);
            const uint8_t data_type = data[attr_pos + 15];
            const uint32_t raw_data = rd32(data, attr_pos + 16);
            const uint32_t attr_name = rd32(data, attr_pos + 4);
            value.name = attr_name < strings_.size() ? strings_[attr_name] : "?";
            value.type = data_type;
            if (data_type == TYPE_STRING && value.raw != 0xffffffffu &&
                value.raw < strings_.size()) {
                value.string_value = strings_[value.raw];
            } else {
                value.int_value = static_cast<int64_t>(static_cast<int32_t>(raw_data));
            }
            element.attributes.push_back(std::move(value));
            attr_pos += attr_size;
        }
        elements.push_back(std::move(element));
        return true;
    }

    std::vector<std::string> strings_;
};

int int_attr(const AxmlElement& element, const std::string& name, int fallback = -1) {
    const AxmlValue* value = element.attribute(name);
    if (value == nullptr) return fallback;
    if (value->type == TYPE_INT_DEC || value->type == TYPE_INT_HEX ||
        value->type == TYPE_INT_BOOLEAN) {
        return static_cast<int>(value->int_value);
    }
    if (value->type == TYPE_STRING) {
        try {
            return std::stoi(value->string_value);
        } catch (const std::exception&) {
            return fallback;
        }
    }
    return fallback;
}

std::string string_attr(const AxmlElement& element, const std::string& name) {
    const AxmlValue* value = element.attribute(name);
    if (value == nullptr) return "";
    if (value->type == TYPE_STRING) return value->string_value;
    return value->display();
}

void parse_manifest(const std::vector<uint8_t>& data, ApkManifest& manifest,
                    std::vector<std::string>& issues) {
    AxmlParser parser;
    std::vector<AxmlElement> elements;
    std::string error;
    if (!parser.parse(data, elements, error)) {
        issues.push_back(error);
        return;
    }
    for (const auto& element : elements) {
        if (element.name == "manifest") {
            manifest.package = string_attr(element, "package");
            manifest.version_code = int_attr(element, "versionCode");
            manifest.version_name = string_attr(element, "versionName");
        } else if (element.name == "uses-sdk") {
            manifest.min_sdk = int_attr(element, "minSdkVersion");
            manifest.target_sdk = int_attr(element, "targetSdkVersion");
        } else if (element.name == "uses-feature") {
            const int gl = int_attr(element, "glEsVersion");
            if (gl > 0) manifest.gl_es_version = gl;
        } else if (element.name == "application") {
            const AxmlValue* debug = element.attribute("debuggable");
            manifest.debuggable = debug != nullptr && debug->as_bool();
            manifest.application_label = string_attr(element, "label");
        } else if (element.name == "activity") {
            const std::string name = string_attr(element, "name");
            if (!name.empty() && manifest.main_activity.empty()) manifest.main_activity = name;
        } else if (element.name == "uses-permission") {
            if (string_attr(element, "name") == "android.permission.INTERNET") {
                manifest.has_internet_permission = true;
            }
        } else if (element.name == "meta-data") {
            if (string_attr(element, "name") == "android.app.lib_name") {
                manifest.native_lib_name = string_attr(element, "value");
            }
        }
    }
}

std::string abi_from_path(const std::string& path) {
    // lib/<abi>/<library>.so
    const auto first = path.find('/');
    if (first == std::string::npos) return "";
    const auto second = path.find('/', first + 1);
    if (second == std::string::npos) return "";
    return path.substr(first + 1, second - first - 1);
}

std::string library_from_path(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string directory_of(const std::string& path) {
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? "" : path.substr(0, slash);
}

bool ends_with_so(const std::string& path) { return ends_with(path, ".so"); }

} // namespace

const char* elf_machine_name(uint16_t machine) {
    switch (machine) {
        case 3: return "x86 (EM_386)";
        case 40: return "ARM (EM_ARM)";
        case 62: return "x86_64 (EM_X86_64)";
        case 183: return "AArch64 (EM_AARCH64)";
        case 8: return "MIPS";
        case 243: return "RISC-V";
        default: return "unknown";
    }
}

ElfInfo inspect_elf_header(const uint8_t* data, size_t size) {
    ElfInfo info;
    if (data == nullptr || size < 20) return info;
    if (data[0] != 0x7f || data[1] != 'E' || data[2] != 'L' || data[3] != 'F') return info;
    info.valid = true;
    info.elf32 = data[4] == 1;
    info.elf64 = data[4] == 2;
    info.little_endian = data[5] == 1;
    info.machine = static_cast<uint16_t>(data[18] | (data[19] << 8));
    info.machine_name = elf_machine_name(info.machine);
    return info;
}

bool ApkInfo::has_abi(const std::string& abi) const {
    return std::any_of(abis.begin(), abis.end(),
                       [&](const ApkAbi& entry) { return entry.abi == abi; });
}

bool ApkInfo::has_arm() const {
    return has_abi("armeabi") || has_abi("armeabi-v7a") || has_abi("arm64-v8a");
}

const ApkAbi* ApkInfo::find_abi(const std::string& abi) const {
    for (const auto& entry : abis) {
        if (entry.abi == abi) return &entry;
    }
    return nullptr;
}

bool ApkInfo::x86_port_ready() const {
    return valid_apk && has_x86() && is_supported_x86_version(manifest.version_code) &&
           manifest.package == "com.mojang.minecraftpe";
}

nlohmann::json ApkInfo::to_json() const {
    nlohmann::json j;
    j["path"] = path;
    j["sha256"] = sha256;
    j["size"] = size;
    j["valid_apk"] = valid_apk;
    j["manifest"] = {
        {"package", manifest.package},
        {"version_name", manifest.version_name},
        {"version_code", manifest.version_code},
        {"min_sdk", manifest.min_sdk},
        {"target_sdk", manifest.target_sdk},
        {"gl_es_version", manifest.gl_es_version},
        {"main_activity", manifest.main_activity},
        {"native_lib_name", manifest.native_lib_name},
        {"debuggable", manifest.debuggable},
        {"has_internet_permission", manifest.has_internet_permission}
    };
    if (const GameVersionCode* code = find_game_version_code(manifest.version_code)) {
        j["manifest"]["version_code_architecture"] = game_architecture_name(code->architecture);
        j["manifest"]["version_code_runtime"] = code->android_runtime;
    } else {
        j["manifest"]["version_code_architecture"] = "unknown";
        j["manifest"]["version_code_runtime"] = "unknown";
    }
    j["abis"] = nlohmann::json::array();
    for (const auto& abi : abis) {
        nlohmann::json entry;
        entry["abi"] = abi.abi;
        entry["libraries"] = abi.libraries;
        entry["primary_library"] = abi.primary_library;
        entry["elf"] = {
            {"valid", abi.primary_elf.valid},
            {"class", abi.primary_elf.elf64 ? "ELF64" : (abi.primary_elf.elf32 ? "ELF32" : "unknown")},
            {"endian", abi.primary_elf.little_endian ? "little" : "big"},
            {"machine", abi.primary_elf.machine},
            {"machine_name", abi.primary_elf.machine_name}
        };
        j["abis"].push_back(entry);
    }
    j["shaders"] = nlohmann::json::array();
    for (const auto& file : shaders) {
        j["shaders"].push_back({{"path", file.path}, {"size", file.size}, {"crc32", file.crc32}});
    }
    j["materials"] = materials.size();
    j["ui_files"] = ui_files.size();
    j["asset_directories"] = asset_directories;
    j["issues"] = issues;
    j["warnings"] = warnings;
    j["x86_port_ready"] = x86_port_ready();
    return j;
}

std::string ApkInfo::to_markdown() const {
    std::ostringstream out;
    out << "# APK verification\n\n";
    out << "- Path: `" << path << "`\n";
    out << "- SHA256: `" << sha256 << "`\n";
    out << "- Size: " << size << " bytes\n";
    out << "- Package: `" << manifest.package << "`\n";
    out << "- Version: `" << manifest.version_name << "` (code " << manifest.version_code << ")\n";
    out << "- Min SDK: " << manifest.min_sdk << ", target SDK: " << manifest.target_sdk << "\n";
    out << "- ABIs: ";
    for (size_t i = 0; i < abis.size(); ++i) {
        if (i) out << ", ";
        out << "`" << abis[i].abi << "`";
    }
    out << "\n";
    if (const GameVersionCode* code = find_game_version_code(manifest.version_code)) {
        out << "- Version-code architecture: " << game_architecture_name(code->architecture) << "\n";
    }
    out << "- x86 native libraries: " << (has_x86() ? "yes" : "no") << "\n";
    out << "- x86 port ready: " << (x86_port_ready() ? "yes" : "no") << "\n\n";
    if (!issues.empty()) {
        out << "## Issues\n\n";
        for (const auto& issue : issues) out << "- " << issue << "\n";
        out << "\n";
    }
    if (!warnings.empty()) {
        out << "## Warnings\n\n";
        for (const auto& warning : warnings) out << "- " << warning << "\n";
    }
    return out.str();
}

ApkInfo probe_apk(const std::string& path) {
    ApkInfo info;
    info.path = path;
    if (!fs::is_regular_file(path)) {
        info.issues.push_back("APK file does not exist: " + path);
        return info;
    }
    info.size = fs::file_size(path);
    info.sha256 = Sha256::file_hex(path);

    ZipReader zip;
    std::string error;
    if (!zip.open(path, error)) {
        info.issues.push_back("cannot open APK: " + error);
        return info;
    }
    const ZipEntry* manifest_entry = zip.find("AndroidManifest.xml");
    if (manifest_entry == nullptr) {
        info.issues.push_back("APK has no AndroidManifest.xml");
        return info;
    }
    std::vector<uint8_t> manifest_data;
    if (!zip.read(manifest_entry->name, manifest_data, error, 4ull * 1024 * 1024)) {
        info.issues.push_back("cannot read AndroidManifest.xml: " + error);
        return info;
    }
    parse_manifest(manifest_data, info.manifest, info.issues);

    std::map<std::string, std::vector<const ZipEntry*>> libraries;
    std::set<std::string> asset_dirs;
    for (const auto& entry : zip.entries()) {
        if (!entry.is_directory && starts_with(entry.name, "lib/") && ends_with_so(entry.name)) {
            libraries[abi_from_path(entry.name)].push_back(&entry);
        }
        if (starts_with(entry.name, "assets/")) {
            const std::string dir = directory_of(entry.name);
            if (!dir.empty()) asset_dirs.insert(dir);
            ApkAssetFile file{entry.name, entry.uncompressed_size, entry.crc32};
            if (starts_with(entry.name, "assets/shaders/")) info.shaders.push_back(file);
            else if (starts_with(entry.name, "assets/materials/")) info.materials.push_back(file);
            else if (starts_with(entry.name, "assets/ui/")) info.ui_files.push_back(file);
        }
    }
    info.asset_directories.assign(asset_dirs.begin(), asset_dirs.end());

    for (auto& pair : libraries) {
        ApkAbi abi;
        abi.abi = pair.first;
        std::sort(pair.second.begin(), pair.second.end(),
                  [](const ZipEntry* a, const ZipEntry* b) { return a->name < b->name; });
        for (const ZipEntry* entry : pair.second) abi.libraries.push_back(library_from_path(entry->name));
        const std::string wanted = "libminecraftpe.so";
        abi.primary_library = abi.libraries.empty() ? "" : abi.libraries.front();
        for (const ZipEntry* entry : pair.second) {
            if (library_from_path(entry->name) == wanted) {
                abi.primary_library = wanted;
                std::vector<uint8_t> header;
                std::string read_error;
                if (zip.read(entry->name, header, read_error, 4ull * 1024 * 1024)) {
                    abi.primary_elf = inspect_elf_header(header.data(),
                                                         std::min<size_t>(header.size(), 64));
                }
                break;
            }
        }
        if (!abi.primary_elf.valid && !pair.second.empty()) {
            std::vector<uint8_t> header;
            std::string read_error;
            if (zip.read(pair.second.front()->name, header, read_error, 4ull * 1024 * 1024)) {
                abi.primary_elf = inspect_elf_header(header.data(),
                                                     std::min<size_t>(header.size(), 64));
            }
        }
        info.abis.push_back(std::move(abi));
    }
    std::sort(info.abis.begin(), info.abis.end(),
              [](const ApkAbi& a, const ApkAbi& b) { return a.abi < b.abi; });

    info.valid_apk = !info.manifest.package.empty() && !info.abis.empty();
    if (info.manifest.package != "com.mojang.minecraftpe") {
        info.issues.push_back("unexpected package: " + info.manifest.package);
    }
    if (info.manifest.version_name != kGameVersion) {
        info.warnings.push_back("versionName is " + info.manifest.version_name + ", expected " +
                                kGameVersion);
    }
    if (info.manifest.version_code < 0) {
        info.issues.push_back("manifest has no versionCode");
    } else if (find_game_version_code(info.manifest.version_code) == nullptr) {
        info.warnings.push_back("unrecognized 0.14.3 version code: " +
                                std::to_string(info.manifest.version_code));
    }
    if (!info.has_x86()) {
        std::string abi_text;
        for (const auto& abi : info.abis) {
            if (!abi_text.empty()) abi_text += ", ";
            abi_text += abi.abi;
        }
        info.issues.push_back("APK has no x86 native libraries (found: " +
                              (abi_text.empty() ? "none" : abi_text) + "). " +
                              "The x86 port requires version code 780140300 or 780140301.");
    }
    if (info.has_x86() && !is_supported_x86_version(info.manifest.version_code)) {
        info.issues.push_back("x86 libraries are present but version code " +
                              std::to_string(info.manifest.version_code) +
                              " is not a verified x86 0.14.3 code");
    }
    if (info.has_x86()) {
        const ApkAbi* x86 = info.find_abi("x86");
        if (x86 != nullptr && x86->primary_elf.valid && x86->primary_elf.machine != 3) {
            info.issues.push_back("lib/x86 primary library is not an EM_386 ELF: " +
                                  x86->primary_elf.machine_name);
        }
    }
    if (info.shaders.empty()) {
        info.warnings.push_back("no assets/shaders files found");
    }
    return info;
}

} // namespace nexus::platform
