#include <nexus/core/strings.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace nexus {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string to_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return s;
}

std::string trim(const std::string& s) {
    size_t begin = 0;
    while (begin < s.size() && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    size_t end = s.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

bool starts_with(const std::string& s, const std::string& prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : s) {
        if (c == sep) {
            parts.push_back(current);
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    parts.push_back(current);
    return parts;
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> parts;
    std::istringstream ss(s);
    std::string token;
    while (ss >> token) parts.push_back(token);
    return parts;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string basename(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string dirname(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

std::string extension(const std::string& path) {
    const std::string base = basename(path);
    const size_t dot = base.find_last_of('.');
    if (dot == std::string::npos || dot == 0) return "";
    return to_lower(base.substr(dot));
}

std::string normalize_slashes(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}

uint32_t hash32(const void* data, size_t len, uint32_t seed) {
    const auto* p = static_cast<const uint8_t*>(data);
    uint32_t h = seed;
    for (size_t i = 0; i < len; ++i) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

std::string format_size(uint64_t bytes) {
    static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    std::ostringstream ss;
    ss.setf(std::ios::fixed);
    ss.precision(unit == 0 ? 0 : 2);
    ss << value << " " << units[unit];
    return ss.str();
}

} // namespace nexus
