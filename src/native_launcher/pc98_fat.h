// SPDX-License-Identifier: GPL-3.0-or-later
// Reads files from PC-98 disk images (HDI, FDI, NHD, HDM and other plain
// sector images) that hold a FAT12/FAT16 file system. Used by the launcher to
// find RPG Maker Dante 98 games inside a disk image, and by the OpenDT backend
// to copy such a game out of the image.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace hardrpg {
class Pc98Fat {
public:
  struct Entry {
    std::string name; // 8.3 name; bytes outside ASCII are written as %XX
    bool dir = false;
    uint32_t size = 0, cluster = 0;
  };

  ~Pc98Fat() { close(); }
  void close() {
    if (f_)
      std::fclose(f_);
    f_ = nullptr;
  }

  // Opens an image and finds its first FAT partition.
  bool open(const std::string &path) {
    close();
    f_ = std::fopen(path.c_str(), "rb");
    if (!f_)
      return false;
    // The file system starts after the image header and, on hard disks, the
    // PC-98 boot code and partition table. Look for its boot sector in the
    // first 2 MB, on 256-byte steps.
    std::vector<uint8_t> head(2 << 20);
    head.resize(std::fread(head.data(), 1, head.size(), f_));
    for (size_t at = 0; at + 512 <= head.size(); at += 256)
      if (boot(head, at))
        return true;
    close();
    return false;
  }

  // Entries of a folder (cluster 0 is the root folder).
  std::vector<Entry> list(uint32_t cluster = 0) {
    std::vector<uint8_t> raw;
    if (cluster == 0) {
      raw.resize(size_t(rootEntries_) * 32);
      if (!readAt(rootOff_, raw.data(), raw.size()))
        return {};
    } else if (!readChain(cluster, 0xffffffffu, raw))
      return {};
    std::vector<Entry> out;
    for (size_t i = 0; i + 32 <= raw.size(); i += 32) {
      const uint8_t *e = &raw[i];
      if (e[0] == 0)
        break;
      if (e[0] == 0xe5 || e[11] == 0x0f || (e[11] & 0x08))
        continue; // deleted, long name or volume label
      Entry r;
      r.name = part(e, 8);
      std::string ext = part(e + 8, 3);
      if (!ext.empty())
        r.name += "." + ext;
      if (r.name == "." || r.name == ".." || r.name.empty())
        continue;
      r.dir = (e[11] & 0x10) != 0;
      r.cluster = uint32_t(e[26] | e[27] << 8);
      r.size = uint32_t(e[28] | e[29] << 8 | e[30] << 16) | uint32_t(e[31]) << 24;
      out.push_back(r);
    }
    return out;
  }

  bool read(const Entry &e, std::vector<uint8_t> &out) {
    out.clear();
    if (e.dir)
      return false;
    if (e.size == 0)
      return true;
    return readChain(e.cluster, e.size, out) && out.size() == e.size;
  }

  // Finds an entry by a /-separated path (ASCII, any case).
  bool find(const std::string &path, Entry &out) {
    uint32_t cluster = 0;
    bool found = false;
    size_t start = 0;
    while (start <= path.size()) {
      size_t slash = path.find('/', start);
      std::string want = path.substr(start, slash == std::string::npos ? std::string::npos
                                                                        : slash - start);
      start = slash == std::string::npos ? path.size() + 1 : slash + 1;
      if (want.empty())
        continue;
      found = false;
      for (auto &e : list(cluster))
        if (same(e.name, want)) {
          out = e;
          found = true;
          break;
        }
      if (!found)
        return false;
      if (start <= path.size()) {
        if (!out.dir)
          return false;
        cluster = out.cluster;
      }
    }
    return found;
  }

  static bool same(const std::string &a, const std::string &b) {
    if (a.size() != b.size())
      return false;
    for (size_t i = 0; i < a.size(); ++i) {
      char x = a[i], y = b[i];
      if (x >= 'a' && x <= 'z')
        x = char(x - 32);
      if (y >= 'a' && y <= 'z')
        y = char(y - 32);
      if (x != y)
        return false;
    }
    return true;
  }

private:
  std::FILE *f_ = nullptr;
  uint64_t base_ = 0, fatOff_ = 0, rootOff_ = 0, dataOff_ = 0;
  uint32_t bps_ = 0, clusterBytes_ = 0, rootEntries_ = 0, clusters_ = 0;
  bool fat16_ = false;
  std::vector<uint8_t> fat_;

  static uint16_t u16(const uint8_t *p) { return uint16_t(p[0] | p[1] << 8); }

  static std::string part(const uint8_t *p, int n) {
    std::string s;
    for (int i = 0; i < n; ++i) {
      uint8_t c = p[i];
      if (i == 0 && c == 0x05)
        c = 0xe5; // a name that really starts with 0xE5
      if (c >= 0x21 && c < 0x7f && c != '%' && c != '/' && c != '\\' && c != ':') {
        s += char(c);
      } else if (c != ' ') {
        char hex[4];
        std::snprintf(hex, sizeof hex, "%%%02X", c);
        s += hex;
      } else {
        s += ' ';
      }
    }
    while (!s.empty() && s.back() == ' ')
      s.pop_back();
    return s;
  }

  bool readAt(uint64_t off, void *dst, size_t n) {
    if (off > 0x7fffffffu || std::fseek(f_, long(off), SEEK_SET) != 0)
      return false;
    return std::fread(dst, 1, n, f_) == n;
  }

  // A FAT boot sector at `at`, checked against its own FAT.
  bool boot(const std::vector<uint8_t> &h, size_t at) {
    const uint8_t *b = &h[at];
    uint32_t bps = u16(b + 11), spc = b[13], res = u16(b + 14), nfats = b[16];
    uint32_t nroot = u16(b + 17), total = u16(b + 19), spf = u16(b + 22);
    uint8_t media = b[21];
    if (!total)
      total = uint32_t(b[32] | b[33] << 8 | b[34] << 16) | uint32_t(b[35]) << 24;
    if (b[0] != 0xeb && b[0] != 0xe9)
      return false;
    if (bps != 256 && bps != 512 && bps != 1024 && bps != 2048)
      return false;
    if (!spc || (spc & (spc - 1)) || spc > 64 || !res || res > 64 || !nfats || nfats > 2)
      return false;
    if (!nroot || nroot > 4096 || (nroot * 32) % bps || !spf || spf > 256 || media < 0xf0)
      return false;
    uint32_t first = res + nfats * spf + nroot * 32 / bps;
    if (total <= first)
      return false;
    base_ = at;
    fatOff_ = base_ + uint64_t(res) * bps;
    fat_.resize(size_t(spf) * bps);
    // The FAT starts with a media byte (PC-98 formats do not always repeat the
    // boot sector's) and 0xFF.
    if (!readAt(fatOff_, fat_.data(), fat_.size()) || fat_[0] < 0xf0 || fat_[1] != 0xff)
      return false;
    bps_ = bps;
    clusterBytes_ = spc * bps;
    rootEntries_ = nroot;
    rootOff_ = fatOff_ + uint64_t(nfats) * spf * bps;
    dataOff_ = rootOff_ + uint64_t(nroot) * 32;
    clusters_ = (total - first) / spc;
    fat16_ = clusters_ >= 4085;
    return true;
  }

  uint32_t next(uint32_t c) const {
    if (fat16_) {
      if (c * 2 + 1 >= fat_.size())
        return 0xffff;
      return u16(&fat_[c * 2]);
    }
    size_t i = c * 3 / 2;
    if (i + 1 >= fat_.size())
      return 0xfff;
    uint16_t v = u16(&fat_[i]);
    return c & 1 ? v >> 4 : v & 0xfff;
  }

  bool readChain(uint32_t c, uint32_t limit, std::vector<uint8_t> &out) {
    const uint32_t end = fat16_ ? 0xfff8 : 0xff8;
    uint32_t guard = clusters_ + 2;
    while (c >= 2 && c < end && out.size() < limit) {
      if (c >= clusters_ + 2 || !guard--)
        return false; // broken chain
      size_t n = std::min<uint64_t>(clusterBytes_, uint64_t(limit) - out.size());
      size_t at = out.size();
      out.resize(at + n);
      if (!readAt(dataOff_ + uint64_t(c - 2) * clusterBytes_, &out[at], n))
        return false;
      c = next(c);
    }
    return limit == 0xffffffffu || out.size() == limit;
  }
};

// Where a Dante 98 game sits in a disk image: the folder that holds its data
// ("" for the root) and its engine (1 = Dante 98, 2 = Dante 98 II).
struct DanteInImage {
  std::string folder;
  int engine = 0;
};

// Same rules as OpenDT's detectGame: Dante 98 II has DAT_RPG/PLAYER.DAT and a
// DAT_MAP folder; Dante 98 has PLAYER.DAT, MAPCHIP.DAT and PALET.DAT.
inline int danteEngineOf(Pc98Fat &fat, const std::vector<Pc98Fat::Entry> &entries) {
  bool player = false, mapchip = false, palet = false, datMap = false;
  const Pc98Fat::Entry *datRpg = nullptr;
  for (auto &e : entries) {
    if (Pc98Fat::same(e.name, "PLAYER.DAT") && !e.dir) player = true;
    if (Pc98Fat::same(e.name, "MAPCHIP.DAT") && !e.dir) mapchip = true;
    if (Pc98Fat::same(e.name, "PALET.DAT") && !e.dir) palet = true;
    if (Pc98Fat::same(e.name, "DAT_MAP") && e.dir) datMap = true;
    if (Pc98Fat::same(e.name, "DAT_RPG") && e.dir) datRpg = &e;
  }
  if (datRpg && datMap)
    for (auto &e : fat.list(datRpg->cluster))
      if (Pc98Fat::same(e.name, "PLAYER.DAT") && !e.dir)
        return 2;
  return player && mapchip && palet ? 1 : 0;
}

// Searches the root and folders up to three levels deep.
inline bool findDanteGame(Pc98Fat &fat, DanteInImage &out) {
  struct Todo {
    uint32_t cluster;
    std::string path;
    int depth;
  };
  std::vector<Todo> todo{{0, "", 0}};
  for (size_t i = 0; i < todo.size() && i < 256; ++i) {
    auto entries = fat.list(todo[i].cluster);
    if (int engine = danteEngineOf(fat, entries)) {
      out = {todo[i].path, engine};
      return true;
    }
    if (todo[i].depth < 3)
      for (auto &e : entries)
        if (e.dir && e.cluster)
          todo.push_back({e.cluster, todo[i].path.empty() ? e.name : todo[i].path + "/" + e.name,
                          todo[i].depth + 1});
  }
  return false;
}
} // namespace hardrpg
