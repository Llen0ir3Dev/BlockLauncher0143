#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexus {

struct ZipEntry {
    std::string name;
    uint16_t method = 0;
    uint16_t flags = 0;
    uint32_t crc32 = 0;
    uint64_t compressed_size = 0;
    uint64_t uncompressed_size = 0;
    uint64_t local_header_offset = 0;
    bool is_directory = false;
};

// Minimal, dependency-light ZIP reader used for .zip shader packs, texture
// packs and APK inspection.  Supports stored and deflated entries, CRC
// verification and path traversal rejection.
class ZipReader {
public:
    ZipReader() = default;
    ~ZipReader();
    ZipReader(const ZipReader&) = delete;
    ZipReader& operator=(const ZipReader&) = delete;
    ZipReader(ZipReader&& other) noexcept;
    ZipReader& operator=(ZipReader&& other) noexcept;

    bool open(const std::string& path, std::string& error);
    void close();
    bool is_open() const { return file_ != nullptr; }

    const std::vector<ZipEntry>& entries() const { return entries_; }
    bool has(const std::string& name) const;
    const ZipEntry* find(const std::string& name) const;
    std::vector<const ZipEntry*> entries_under(const std::string& prefix) const;

    bool read(const std::string& name, std::vector<uint8_t>& out, std::string& error,
              uint64_t max_bytes = 128ull * 1024 * 1024) const;
    bool read_text(const std::string& name, std::string& out, std::string& error,
                   uint64_t max_bytes = 32ull * 1024 * 1024) const;

    const std::string& path() const { return path_; }
    uint64_t archive_size() const { return archive_size_; }

private:
    bool read_entry(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error,
                    uint64_t max_bytes) const;

    std::string path_;
    void* file_ = nullptr;
    uint64_t archive_size_ = 0;
    std::vector<ZipEntry> entries_;
};

} // namespace nexus
