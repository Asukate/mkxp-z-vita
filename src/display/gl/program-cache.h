#ifndef MKXP_PROGRAM_CACHE_H
#define MKXP_PROGRAM_CACHE_H
// Optional acceleration only: reject stale, incomplete and damaged files before
// passing any bytes to the graphics driver. No game assets are stored here.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <new>
#include <zlib.h>
namespace ProgramCache {
constexpr uint32_t MaxBinary = 1024 * 1024, MaxKey = 256 * 1024;
struct Header { char magic[8]; uint32_t keySize, binarySize, checksum; };
inline uint32_t checksum(const void *p, size_t n) {
    return crc32(0, static_cast<const Bytef *>(p), n);
}
inline bool load(const char *path, const std::string &key, std::vector<uint8_t> &binary) {
    FILE *f = std::fopen(path, "rb");
    if (!f) return false;
    bool ok = false;
    try {
        Header h;
        if (std::fread(&h, sizeof(h), 1, f) == 1 &&
            std::memcmp(h.magic, "MKXPVPC1", 8) == 0 &&
            h.keySize == key.size() && h.keySize <= MaxKey &&
            h.binarySize > 0 && h.binarySize <= MaxBinary) {
            std::string stored(h.keySize, '\0');
            if (std::fread(&stored[0], 1, stored.size(), f) == stored.size() && stored == key) {
                binary.resize(h.binarySize);
                ok = std::fread(binary.data(), 1, binary.size(), f) == binary.size() &&
                     std::fgetc(f) == EOF && !std::ferror(f) &&
                     checksum(binary.data(), binary.size()) == h.checksum;
            }
        }
    } catch (const std::bad_alloc &) { ok = false; }
    std::fclose(f);
    if (!ok) binary.clear();
    return ok;
}
inline bool save(const char *path, const std::string &key, const std::vector<uint8_t> &binary) {
    if (key.empty() || key.size() > MaxKey || binary.empty() || binary.size() > MaxBinary)
        return false;
    const std::string tmp = std::string(path) + ".tmp";
    FILE *f = std::fopen(tmp.c_str(), "wb");
    if (!f) return false;
    Header h = {{'M','K','X','P','V','P','C','1'}, static_cast<uint32_t>(key.size()),
                static_cast<uint32_t>(binary.size()), checksum(binary.data(), binary.size())};
    bool ok = std::fwrite(&h, sizeof(h), 1, f) == 1 &&
              std::fwrite(key.data(), 1, key.size(), f) == key.size() &&
              std::fwrite(binary.data(), 1, binary.size(), f) == binary.size();
    if (std::fclose(f) != 0) ok = false;
    if (ok) ok = std::rename(tmp.c_str(), path) == 0;
    if (!ok) std::remove(tmp.c_str());
    return ok;
}
}
#endif
