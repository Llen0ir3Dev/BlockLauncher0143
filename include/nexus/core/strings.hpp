#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace nexus {

std::string to_lower(std::string s);
std::string to_upper(std::string s);
std::string trim(const std::string& s);
bool starts_with(const std::string& s, const std::string& prefix);
bool ends_with(const std::string& s, const std::string& suffix);
std::vector<std::string> split(const std::string& s, char sep);
std::vector<std::string> split_ws(const std::string& s);
std::string join(const std::vector<std::string>& parts, const std::string& sep);
std::string replace_all(std::string s, const std::string& from, const std::string& to);
std::string basename(const std::string& path);
std::string dirname(const std::string& path);
std::string extension(const std::string& path);
std::string normalize_slashes(std::string path);
uint32_t hash32(const void* data, size_t len, uint32_t seed = 2166136261u);
std::string format_size(uint64_t bytes);

} // namespace nexus
