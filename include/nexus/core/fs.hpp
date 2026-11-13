#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexus::fs {

bool exists(const std::string& path);
bool is_directory(const std::string& path);
bool is_regular_file(const std::string& path);
bool ensure_directory(const std::string& path);
bool ensure_parent(const std::string& path);
uint64_t file_size(const std::string& path);
std::string absolute(const std::string& path);
std::string canonical(const std::string& path);
std::string join(const std::string& a, const std::string& b);
std::string home_dir();
std::string temp_dir();
std::string executable_path();
std::string current_dir();
std::string nexus_home();
std::string game_dir();
std::string mods_dir();
std::string texture_packs_dir();
std::string shader_packs_dir();
std::string config_dir();
std::string logs_dir();

bool read_file(const std::string& path, std::string& out, uint64_t max_bytes = 256ull * 1024 * 1024);
bool read_file_binary(const std::string& path, std::vector<uint8_t>& out, uint64_t max_bytes = 512ull * 1024 * 1024);
bool write_file(const std::string& path, const std::string& data);
bool write_file_binary(const std::string& path, const std::vector<uint8_t>& data);
bool copy_file(const std::string& from, const std::string& to);
bool remove_file(const std::string& path);
bool remove_all(const std::string& path);

std::vector<std::string> list_directory(const std::string& path, bool recursive = false,
                                        const std::string& extension_filter = "");
std::vector<std::string> list_files(const std::string& path, const std::string& extension = "");
std::vector<std::string> list_directories(const std::string& path);

bool is_safe_relative_path(const std::string& path);
std::string sanitize_relative_path(const std::string& path);
std::string make_temp_dir(const std::string& prefix);
std::string timestamp_string();

} // namespace nexus::fs
