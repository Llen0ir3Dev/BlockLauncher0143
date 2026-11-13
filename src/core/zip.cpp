#include <nexus/core/zip.hpp>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

#include <zlib.h>

#include <nexus/core/fs.hpp>
#include <nexus/core/strings.hpp>

namespace nexus {

namespace {
constexpr uint32_t kLocalHeaderSig = 0x04034b50u;
constexpr uint32_t kCentralHeaderSig = 0x02014b50u;
constexpr uint32_t kEocdSig = 0x06054b50u;
constexpr uint32_t kZip64EocdSig = 0x06064b50u;
constexpr uint32_t kZip64LocatorSig = 0x07064b50u;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) {
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

bool read_at(FILE* f, uint64_t offset, void* data, size_t len) {
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0) return false;
    return std::fread(data, 1, len, f) == len;
}

bool inflate_raw(const uint8_t* input, size_t input_len, uint8_t* output, size_t output_len) {
    z_stream zs{};
    if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) return false;
    zs.next_in = const_cast<Bytef*>(input);
    zs.avail_in = static_cast<uInt>(input_len);
    zs.next_out = output;
    zs.avail_out = static_cast<uInt>(output_len);
    int rc = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    return (rc == Z_STREAM_END || rc == Z_OK) && zs.total_out == output_len;
}
} // namespace

ZipReader::~ZipReader() { close(); }

ZipReader::ZipReader(ZipReader&& other) noexcept
    : path_(std::move(other.path_)), file_(other.file_), archive_size_(other.archive_size_),
      entries_(std::move(other.entries_)) {
    other.file_ = nullptr;
    other.archive_size_ = 0;
}

ZipReader& ZipReader::operator=(ZipReader&& other) noexcept {
    if (this != &other) {
        close();
        path_ = std::move(other.path_);
        file_ = other.file_;
        archive_size_ = other.archive_size_;
        entries_ = std::move(other.entries_);
        other.file_ = nullptr;
        other.archive_size_ = 0;
    }
    return *this;
}

void ZipReader::close() {
    if (file_) {
        std::fclose(static_cast<FILE*>(file_));
        file_ = nullptr;
    }
    entries_.clear();
    archive_size_ = 0;
    path_.clear();
}

bool ZipReader::open(const std::string& path, std::string& error) {
    close();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        error = "cannot open archive: " + path;
        return false;
    }
    file_ = f;
    path_ = path;
    if (std::fseek(f, 0, SEEK_END) != 0) {
        error = "cannot seek archive";
        close();
        return false;
    }
    const long size = std::ftell(f);
    if (size < 0) {
        error = "cannot determine archive size";
        close();
        return false;
    }
    archive_size_ = static_cast<uint64_t>(size);

    const uint64_t search_len = std::min<uint64_t>(archive_size_, 65557);
    std::vector<uint8_t> tail(static_cast<size_t>(search_len));
    if (!read_at(f, archive_size_ - search_len, tail.data(), tail.size())) {
        error = "cannot read archive tail";
        close();
        return false;
    }
    long eocd_pos = -1;
    for (long i = static_cast<long>(tail.size()) - 22; i >= 0; --i) {
        if (rd32(tail.data() + i) == kEocdSig) {
            eocd_pos = i;
            break;
        }
    }
    if (eocd_pos < 0) {
        error = "ZIP end-of-central-directory record not found";
        close();
        return false;
    }
    const uint8_t* eocd = tail.data() + eocd_pos;
    uint64_t entry_count = rd16(eocd + 10);
    uint64_t cd_size = rd32(eocd + 12);
    uint64_t cd_offset = rd32(eocd + 16);

    // ZIP64 support, needed by some larger resource packs and APKs.
    const uint64_t eocd_abs = archive_size_ - search_len + static_cast<uint64_t>(eocd_pos);
    if (entry_count == 0xffff || cd_size == 0xffffffffu || cd_offset == 0xffffffffu) {
        uint8_t locator[20];
        if (eocd_abs >= 20 && read_at(f, eocd_abs - 20, locator, sizeof(locator)) &&
            rd32(locator) == kZip64LocatorSig) {
            const uint64_t zip64_offset = rd64(locator + 8);
            uint8_t zip64[56];
            if (read_at(f, zip64_offset, zip64, sizeof(zip64)) && rd32(zip64) == kZip64EocdSig) {
                entry_count = rd64(zip64 + 32);
                cd_size = rd64(zip64 + 40);
                cd_offset = rd64(zip64 + 48);
            }
        }
    }
    if (cd_offset + cd_size > archive_size_) {
        error = "central directory is out of bounds";
        close();
        return false;
    }
    std::vector<uint8_t> cd(static_cast<size_t>(cd_size));
    if (cd_size > 0 && !read_at(f, cd_offset, cd.data(), cd.size())) {
        error = "cannot read central directory";
        close();
        return false;
    }

    size_t pos = 0;
    for (uint64_t index = 0; index < entry_count; ++index) {
        if (pos + 46 > cd.size() || rd32(cd.data() + pos) != kCentralHeaderSig) {
            error = "malformed central directory entry";
            close();
            return false;
        }
        const uint8_t* h = cd.data() + pos;
        ZipEntry entry;
        entry.flags = rd16(h + 8);
        entry.method = rd16(h + 10);
        entry.crc32 = rd32(h + 16);
        entry.compressed_size = rd32(h + 20);
        entry.uncompressed_size = rd32(h + 24);
        const uint16_t name_len = rd16(h + 28);
        const uint16_t extra_len = rd16(h + 30);
        const uint16_t comment_len = rd16(h + 32);
        entry.local_header_offset = rd32(h + 42);
        if (pos + 46 + name_len + extra_len + comment_len > cd.size()) {
            error = "central directory entry exceeds bounds";
            close();
            return false;
        }
        entry.name.assign(reinterpret_cast<const char*>(h + 46), name_len);
        entry.name = normalize_slashes(entry.name);
        entry.is_directory = !entry.name.empty() && entry.name.back() == '/';

        // ZIP64 extended information extra field.
        size_t ep = pos + 46 + name_len;
        const size_t ep_end = ep + extra_len;
        while (ep + 4 <= ep_end) {
            const uint16_t tag = rd16(cd.data() + ep);
            const uint16_t len = rd16(cd.data() + ep + 2);
            if (ep + 4 + len > ep_end) break;
            if (tag == 0x0001) {
                size_t q = ep + 4;
                if (entry.uncompressed_size == 0xffffffffu && q + 8 <= ep + 4 + len) {
                    entry.uncompressed_size = rd64(cd.data() + q); q += 8;
                }
                if (entry.compressed_size == 0xffffffffu && q + 8 <= ep + 4 + len) {
                    entry.compressed_size = rd64(cd.data() + q); q += 8;
                }
                if (entry.local_header_offset == 0xffffffffu && q + 8 <= ep + 4 + len) {
                    entry.local_header_offset = rd64(cd.data() + q);
                }
            }
            ep += 4 + len;
        }

        if (!fs::is_safe_relative_path(entry.name) && !entry.is_directory) {
            error = "unsafe path in archive: " + entry.name;
            close();
            return false;
        }
        if (entry.is_directory) {
            entries_.push_back(std::move(entry));
        } else {
            entries_.push_back(std::move(entry));
        }
        pos += 46 + name_len + extra_len + comment_len;
    }
    if (entries_.empty()) {
        error = "archive contains no entries";
        close();
        return false;
    }
    return true;
}

bool ZipReader::has(const std::string& name) const {
    return find(name) != nullptr;
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    const std::string normalized = normalize_slashes(name);
    for (const auto& entry : entries_) {
        if (entry.name == normalized) return &entry;
    }
    return nullptr;
}

std::vector<const ZipEntry*> ZipReader::entries_under(const std::string& prefix) const {
    std::vector<const ZipEntry*> out;
    std::string normalized = normalize_slashes(prefix);
    if (!normalized.empty() && normalized.back() != '/') normalized += '/';
    for (const auto& entry : entries_) {
        if (entry.name.compare(0, normalized.size(), normalized) == 0) out.push_back(&entry);
    }
    return out;
}

bool ZipReader::read(const std::string& name, std::vector<uint8_t>& out, std::string& error,
                     uint64_t max_bytes) const {
    const ZipEntry* entry = find(name);
    if (!entry) {
        error = "entry not found: " + name;
        return false;
    }
    return read_entry(*entry, out, error, max_bytes);
}

bool ZipReader::read_text(const std::string& name, std::string& out, std::string& error,
                          uint64_t max_bytes) const {
    std::vector<uint8_t> data;
    if (!read(name, data, error, max_bytes)) return false;
    out.assign(reinterpret_cast<const char*>(data.data()), data.size());
    return true;
}

bool ZipReader::read_entry(const ZipEntry& entry, std::vector<uint8_t>& out, std::string& error,
                           uint64_t max_bytes) const {
    if (!file_) {
        error = "archive is not open";
        return false;
    }
    if (entry.uncompressed_size > max_bytes) {
        error = "entry exceeds size limit: " + entry.name;
        return false;
    }
    uint8_t local[30];
    if (!read_at(static_cast<FILE*>(file_), entry.local_header_offset, local, sizeof(local)) ||
        rd32(local) != kLocalHeaderSig) {
        error = "invalid local header for " + entry.name;
        return false;
    }
    const uint16_t name_len = rd16(local + 26);
    const uint16_t extra_len = rd16(local + 28);
    const uint64_t data_offset = entry.local_header_offset + 30 + name_len + extra_len;
    if (data_offset + entry.compressed_size > archive_size_) {
        error = "entry data is out of bounds: " + entry.name;
        return false;
    }
    std::vector<uint8_t> compressed(static_cast<size_t>(entry.compressed_size));
    if (entry.compressed_size > 0 &&
        !read_at(static_cast<FILE*>(file_), data_offset, compressed.data(), compressed.size())) {
        error = "cannot read entry data: " + entry.name;
        return false;
    }
    out.resize(static_cast<size_t>(entry.uncompressed_size));
    if (entry.method == 0) {
        if (compressed.size() != out.size()) {
            error = "stored entry size mismatch: " + entry.name;
            return false;
        }
        out = std::move(compressed);
    } else if (entry.method == 8) {
        if (!inflate_raw(compressed.data(), compressed.size(), out.data(), out.size())) {
            error = "deflate failed for " + entry.name;
            return false;
        }
    } else {
        error = "unsupported compression method " + std::to_string(entry.method) + " for " + entry.name;
        return false;
    }
    const uint32_t crc = crc32(0L, Z_NULL, 0);
    const uint32_t actual = crc32(crc, out.data(), static_cast<uInt>(out.size()));
    if (actual != entry.crc32) {
        error = "CRC mismatch for " + entry.name;
        return false;
    }
    return true;
}

} // namespace nexus
