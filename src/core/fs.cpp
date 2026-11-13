#include <nexus/core/fs.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <nexus/core/log.hpp>
#include <nexus/core/strings.hpp>

namespace stdfs = std::filesystem;

namespace nexus::fs {

bool exists(const std::string& path) {
    std::error_code ec;
    return stdfs::exists(path, ec);
}

bool is_directory(const std::string& path) {
    std::error_code ec;
    return stdfs::is_directory(path, ec);
}

bool is_regular_file(const std::string& path) {
    std::error_code ec;
    return stdfs::is_regular_file(path, ec);
}

bool ensure_directory(const std::string& path) {
    std::error_code ec;
    if (stdfs::exists(path, ec)) return stdfs::is_directory(path, ec);
    return stdfs::create_directories(path, ec) || stdfs::is_directory(path, ec);
}

bool ensure_parent(const std::string& path) {
    const std::string parent = dirname(path);
    if (parent.empty() || parent == ".") return true;
    return ensure_directory(parent);
}

uint64_t file_size(const std::string& path) {
    std::error_code ec;
    const auto size = stdfs::file_size(path, ec);
    return ec ? 0 : static_cast<uint64_t>(size);
}

std::string absolute(const std::string& path) {
    std::error_code ec;
    const auto result = stdfs::absolute(path, ec);
    return ec ? path : result.string();
}

std::string canonical(const std::string& path) {
    std::error_code ec;
    const auto result = stdfs::weakly_canonical(path, ec);
    return ec ? absolute(path) : result.string();
}

std::string join(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    return (stdfs::path(a) / b).string();
}

std::string home_dir() {
    const char* home = std::getenv("HOME");
    if (home && *home) return home;
    const char* userprofile = std::getenv("USERPROFILE");
    if (userprofile && *userprofile) return userprofile;
    return ".";
}

std::string temp_dir() {
    std::error_code ec;
    const auto p = stdfs::temp_directory_path(ec);
    return ec ? "/tmp" : p.string();
}

std::string executable_path() {
#if defined(__linux__)
    std::error_code ec;
    const auto p = stdfs::read_symlink("/proc/self/exe", ec);
    if (!ec) return p.string();
#endif
    return "";
}

std::string current_dir() {
    std::error_code ec;
    const auto p = stdfs::current_path(ec);
    return ec ? "." : p.string();
}

std::string nexus_home() {
    const char* env = std::getenv("NEXUS_HOME");
    if (env && *env) return env;
    return join(home_dir(), ".nexus-mcpe");
}

std::string game_dir() {
    const char* env = std::getenv("NEXUS_GAME_DIR");
    if (env && *env) return env;
    return join(nexus_home(), "game");
}

std::string mods_dir() { return join(nexus_home(), "mods"); }
std::string texture_packs_dir() { return join(nexus_home(), "texture-packs"); }
std::string shader_packs_dir() { return join(nexus_home(), "shader-packs"); }
std::string config_dir() { return join(nexus_home(), "config"); }
std::string logs_dir() { return join(nexus_home(), "logs"); }

bool read_file(const std::string& path, std::string& out, uint64_t max_bytes) {
    std::vector<uint8_t> data;
    if (!read_file_binary(path, data, max_bytes)) return false;
    out.assign(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

bool read_file_binary(const std::string& path, std::vector<uint8_t>& out, uint64_t max_bytes) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return false;
    const std::streamoff size = in.tellg();
    if (size < 0 || static_cast<uint64_t>(size) > max_bytes) return false;
    in.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) in.read(reinterpret_cast<char*>(out.data()), size);
    return in.good() || in.eof();
}

bool write_file(const std::string& path, const std::string& data) {
    if (!ensure_parent(path)) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool write_file_binary(const std::string& path, const std::vector<uint8_t>& data) {
    if (!ensure_parent(path)) return false;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out.is_open()) return false;
    if (!data.empty()) out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    return out.good();
}

bool copy_file(const std::string& from, const std::string& to) {
    if (!ensure_parent(to)) return false;
    std::error_code ec;
    stdfs::copy_file(from, to, stdfs::copy_options::overwrite_existing, ec);
    return !ec;
}

bool remove_file(const std::string& path) {
    std::error_code ec;
    return stdfs::remove(path, ec);
}

bool remove_all(const std::string& path) {
    std::error_code ec;
    stdfs::remove_all(path, ec);
    return !ec;
}

std::vector<std::string> list_directory(const std::string& path, bool recursive,
                                        const std::string& extension_filter) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!stdfs::is_directory(path, ec)) return out;
    const auto filter = to_lower(extension_filter);
    if (recursive) {
        for (const auto& entry : stdfs::recursive_directory_iterator(path, stdfs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (!filter.empty() && extension(entry.path().string()) != filter) continue;
            out.push_back(entry.path().string());
        }
    } else {
        for (const auto& entry : stdfs::directory_iterator(path, stdfs::directory_options::skip_permission_denied, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (!filter.empty() && extension(entry.path().string()) != filter) continue;
            out.push_back(entry.path().string());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<std::string> list_files(const std::string& path, const std::string& extension) {
    return list_directory(path, false, extension);
}

std::vector<std::string> list_directories(const std::string& path) {
    std::vector<std::string> out;
    std::error_code ec;
    if (!stdfs::is_directory(path, ec)) return out;
    for (const auto& entry : stdfs::directory_iterator(path, stdfs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;
        if (entry.is_directory(ec)) out.push_back(entry.path().string());
    }
    std::sort(out.begin(), out.end());
    return out;
}

bool is_safe_relative_path(const std::string& path) {
    if (path.empty()) return false;
    if (path[0] == '/' || path[0] == '\\') return false;
    if (path.find(':') != std::string::npos) return false;
    const stdfs::path p(path);
    for (const auto& part : p) {
        if (part == "..") return false;
    }
    return true;
}

std::string sanitize_relative_path(const std::string& path) {
    std::string normalized = normalize_slashes(path);
    while (!normalized.empty() && normalized.front() == '/') normalized.erase(normalized.begin());
    std::vector<std::string> parts;
    for (const auto& part : split(normalized, '/')) {
        if (part.empty() || part == ".") continue;
        if (part == "..") continue;
        parts.push_back(part);
    }
    return nexus::join(parts, "/");
}

std::string make_temp_dir(const std::string& prefix) {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    for (int attempt = 0; attempt < 32; ++attempt) {
        std::ostringstream ss;
        ss << prefix << std::hex << dist(gen);
        const std::string path = join(temp_dir(), ss.str());
        std::error_code ec;
        if (stdfs::create_directory(path, ec) && !ec) return path;
    }
    return "";
}

std::string timestamp_string() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &tm);
    return buffer;
}

} // namespace nexus::fs
