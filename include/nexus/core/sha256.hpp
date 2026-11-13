#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexus {

class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t len);
    void update(const std::string& data);
    std::string final_hex();
    static std::string hash(const void* data, size_t len);
    static std::string hash(const std::string& data);
    static std::string file_hex(const std::string& path);

private:
    void transform(const uint8_t* block);
    uint32_t state_[8];
    uint64_t bit_count_;
    uint8_t buffer_[64];
    size_t buffer_len_;
};

std::string sha256_hex(const void* data, size_t len);
std::string sha256_file(const std::string& path);

} // namespace nexus
