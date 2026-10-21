#include "theme.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <vector>

namespace hardrpg {
namespace {
// Tracks longer than this are left alone rather than read into memory.
constexpr uint32_t maxArchivedTrack = 32u << 20;

std::string lowered(std::string s) {
  for (auto &c : s)
    c = char(std::tolower((unsigned char)c));
  return s;
}
std::string extension(const std::string &name) {
  auto dot = name.find_last_of('.');
  auto slash = name.find_last_of("/\\");
  if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
    return "";
  return lowered(name.substr(dot));
}
bool playable(const std::string &ext) { return ext == ".ogg" || ext == ".mp3" || ext == ".wav"; }
bool regularFile(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}
bool isDirectory(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}
std::vector<std::string> listDir(const std::string &dir) {
  std::vector<std::string> names;
  if (DIR *d = opendir(dir.c_str())) {
    while (auto *e = readdir(d))
      if (e->d_name[0] != '.')
        names.push_back(e->d_name);
    closedir(d);
  }
  std::sort(names.begin(), names.end());
  return names;
}
// dir/rel with each part matched without regard to case, as Windows does.
std::string findLoose(const std::string &dir, const std::string &rel) {
  std::string at = dir;
  size_t start = 0;
  while (start <= rel.size()) {
    size_t end = rel.find('/', start);
    auto part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
    std::string next = at + "/" + part;
    struct stat st;
    if (stat(next.c_str(), &st) != 0) {
      next.clear();
      auto want = lowered(part);
      for (auto &name : listDir(at))
        if (lowered(name) == want) {
          next = at + "/" + name;
          break;
        }
      if (next.empty())
        return "";
    }
    at = next;
    if (end == std::string::npos)
      break;
    start = end + 1;
  }
  return at;
}
std::string readWhole(const std::string &path, size_t limit) {
  std::string out;
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return out;
  char buffer[65536];
  size_t n;
  while ((n = std::fread(buffer, 1, sizeof buffer, f)) > 0 && out.size() <= limit)
    out.append(buffer, n);
  std::fclose(f);
  if (out.size() > limit)
    out.clear();
  return out;
}

// ---------------------------------------------------------------- encrypted archives
// Game.rgssad (XP), .rgss2a (VX) and .rgss3a (VX Ace). Names and data are
// XORed with a key that changes every four bytes (key * 7 + 3).
struct Archive {
  struct Item {
    long offset;
    uint32_t size, key;
  };
  std::string path;
  std::map<std::string, Item> items; // lowered, '/'-separated names
  bool open(const std::string &file) {
    path = file;
    FILE *f = std::fopen(file.c_str(), "rb");
    if (!f)
      return false;
    auto u32 = [&](uint32_t &v) {
      unsigned char b[4];
      if (std::fread(b, 1, 4, f) != 4)
        return false;
      v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
      return true;
    };
    auto add = [&](std::string name, long offset, uint32_t size, uint32_t key) {
      std::replace(name.begin(), name.end(), '\\', '/');
      items[lowered(name)] = {offset, size, key};
    };
    char magic[8];
    if (std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, "RGSSAD\0", 7) == 0) {
      if (magic[7] == 1) {
        uint32_t key = 0xDEADCAFE, n, size;
        for (int guard = 0; guard < 200000 && u32(n); ++guard) {
          n ^= key, key = key * 7 + 3;
          if (n == 0 || n > 512)
            break;
          std::string name(n, '\0');
          if (std::fread(&name[0], 1, n, f) != n)
            break;
          for (auto &c : name)
            c = char((unsigned char)c ^ (key & 0xff)), key = key * 7 + 3;
          if (!u32(size))
            break;
          size ^= key, key = key * 7 + 3;
          long offset = std::ftell(f);
          add(name, offset, size, key);
          if (std::fseek(f, long(size), SEEK_CUR) != 0)
            break;
        }
      } else if (magic[7] == 3) {
        uint32_t key, offset, size, start, n;
        if (u32(key)) {
          key = key * 9 + 3;
          for (int guard = 0; guard < 200000 && u32(offset); ++guard) {
            offset ^= key;
            if (offset == 0 || !u32(size) || !u32(start) || !u32(n))
              break;
            size ^= key, start ^= key, n ^= key;
            if (n == 0 || n > 512)
              break;
            std::string name(n, '\0');
            if (std::fread(&name[0], 1, n, f) != n)
              break;
            for (size_t i = 0; i < name.size(); ++i)
              name[i] = char((unsigned char)name[i] ^ ((key >> (8 * (i % 4))) & 0xff));
            add(name, long(offset), size, start);
          }
        }
      }
    }
    std::fclose(f);
    return !items.empty();
  }
  std::string read(const Item &item) const {
    std::string out;
    if (item.size > maxArchivedTrack)
      return out;
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f)
      return out;
    out.resize(item.size);
    if (std::fseek(f, item.offset, SEEK_SET) != 0 || std::fread(&out[0], 1, item.size, f) != item.size)
      out.clear();
    std::fclose(f);
    uint32_t key = item.key;
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = char((unsigned char)out[i] ^ ((key >> (8 * (i % 4))) & 0xff));
      if (i % 4 == 3)
        key = key * 7 + 3;
    }
    return out;
  }
};

// ---------------------------------------------------------------- Ruby Marshal
// Enough of Marshal 4.8 to walk RPG::System and read @title_bgm.
struct Value {
  char type = '0';
  long number = 0;
  std::string text;                                    // strings, symbols, user data
  std::vector<std::pair<std::string, std::shared_ptr<Value>>> fields; // ivars
  const Value *field(const char *name) const {
    for (auto &f : fields)
      if (f.first == name)
        return f.second.get();
    return nullptr;
  }
};
using ValuePtr = std::shared_ptr<Value>;
struct Marshal {
  const unsigned char *p, *end;
  std::vector<std::string> symbols;
  std::vector<ValuePtr> objects;
  bool ok = true;
  int depth = 0;
  int byte() {
    if (p >= end) {
      ok = false;
      return 0;
    }
    return *p++;
  }
  long fixnum() {
    int c = (signed char)byte();
    if (c == 0)
      return 0;
    if (c >= 5)
      return c - 5;
    if (c <= -5)
      return c + 5;
    long x = 0;
    if (c > 0) {
      for (int i = 0; i < c; ++i)
        x |= long(byte()) << (8 * i);
    } else {
      x = -1;
      for (int i = 0; i < -c; ++i) {
        x &= ~(0xffL << (8 * i));
        x |= long(byte()) << (8 * i);
      }
    }
    return x;
  }
  std::string bytes() {
    long n = fixnum();
    if (n < 0 || n > end - p) {
      ok = false;
      return "";
    }
    std::string s(reinterpret_cast<const char *>(p), size_t(n));
    p += n;
    return s;
  }
  std::string symbol() {
    int t = byte();
    if (t == ':') {
      symbols.push_back(bytes());
      return symbols.back();
    }
    if (t == ';') {
      long i = fixnum();
      if (i >= 0 && size_t(i) < symbols.size())
        return symbols[size_t(i)];
    }
    if (t == 'I') { // a symbol with an encoding
      auto s = symbol();
      long n = fixnum();
      for (long i = 0; i < n && ok; ++i) {
        symbol();
        value();
      }
      return s;
    }
    ok = false;
    return "";
  }
  ValuePtr keep(ValuePtr v) {
    objects.push_back(v);
    return v;
  }
  void readFields(Value &v, long n) {
    for (long i = 0; i < n && ok; ++i) {
      auto name = symbol();
      auto field = value();
      v.fields.emplace_back(name, field);
    }
  }
  ValuePtr value() {
    auto v = std::make_shared<Value>();
    if (!ok || ++depth > 64) {
      ok = false;
      return v;
    }
    int t = byte();
    v->type = char(t);
    switch (t) {
    case '0': case 'T': case 'F':
      break;
    case 'i':
      v->number = fixnum();
      break;
    case ':':
      --p;
      v->text = symbol();
      break;
    case ';':
      --p;
      v->text = symbol();
      v->type = ':';
      break;
    case '"':
      keep(v);
      v->text = bytes();
      break;
    case 'f':
      v->text = bytes();
      keep(v);
      break;
    case 'l': {
      keep(v);
      byte(); // sign
      long n = fixnum();
      if (n < 0 || n * 2 > end - p)
        ok = false;
      else
        p += n * 2;
      break;
    }
    case '/':
      keep(v);
      v->text = bytes();
      byte();
      break;
    case 'c': case 'm': case 'M':
      keep(v);
      v->text = bytes();
      break;
    case '[': {
      keep(v);
      long n = fixnum();
      for (long i = 0; i < n && ok; ++i)
        v->fields.emplace_back("", value());
      break;
    }
    case '{': case '}': {
      keep(v);
      long n = fixnum();
      for (long i = 0; i < n && ok; ++i) {
        auto k = value();
        v->fields.emplace_back(k->text, value());
      }
      if (t == '}')
        value();
      break;
    }
    case 'o':
      keep(v);
      v->text = symbol(); // class
      readFields(*v, fixnum());
      break;
    case 'S':
      keep(v);
      v->text = symbol();
      readFields(*v, fixnum());
      break;
    case 'u': {
      symbol();
      v->text = bytes();
      keep(v);
      break;
    }
    case 'U':
      keep(v);
      symbol();
      value();
      break;
    case 'd':
      keep(v);
      symbol();
      value();
      break;
    case 'e': case 'C':
      symbol();
      --depth;
      return value();
    case 'I': {
      auto inner = value();
      Value extra;
      readFields(extra, fixnum());
      --depth;
      return inner;
    }
    case '@': {
      long i = fixnum();
      --depth;
      if (i >= 0 && size_t(i) < objects.size())
        return objects[size_t(i)];
      ok = false;
      return v;
    }
    default:
      ok = false;
    }
    --depth;
    return v;
  }
};

// ---------------------------------------------------------------- LCF (RPG Maker 2000/2003)
struct Lcf {
  const unsigned char *p, *end;
  bool ok = true;
  uint32_t ber() {
    uint32_t v = 0;
    for (int i = 0; i < 5; ++i) {
      if (p >= end) {
        ok = false;
        return 0;
      }
      unsigned char b = *p++;
      v = v << 7 | (b & 0x7f);
      if (!(b & 0x80))
        return v;
    }
    ok = false;
    return 0;
  }
  // Calls found(id, start, size) for each chunk until the end or a 0 id.
  void chunks(const std::function<bool(uint32_t, const unsigned char *, uint32_t)> &found) {
    while (ok && p < end) {
      uint32_t id = ber();
      if (!ok || id == 0)
        return;
      uint32_t size = ber();
      if (!ok || size > uint32_t(end - p)) {
        ok = false;
        return;
      }
      const unsigned char *at = p;
      p += size;
      if (!found(id, at, size))
        return;
    }
  }
};

// ---------------------------------------------------------------- finding the track
struct Game {
  std::string dir;
  std::unique_ptr<Archive> archive;
  // A file of the game, loose first, then from its archive.
  bool locate(const std::string &rel, Theme &out) {
    auto loose = findLoose(dir, rel);
    if (!loose.empty() && regularFile(loose)) {
      out.file = loose;
      out.ext = extension(rel);
      return true;
    }
    if (archive) {
      auto it = archive->items.find(lowered(rel));
      if (it != archive->items.end()) {
        out.bytes = archive->read(it->second);
        out.ext = extension(rel);
        return !out.bytes.empty();
      }
    }
    return false;
  }
  std::string data(const std::string &rel, size_t limit) {
    Theme t;
    if (!locate(rel, t))
      return "";
    return t.file.empty() ? t.bytes : readWhole(t.file, limit);
  }
  // folder/name with any extension the engine accepts, playable ones first.
  bool track(const std::string &folder, const std::string &name, Theme &out) {
    for (auto ext : {".ogg", ".mp3", ".wav"})
      if (locate(folder + "/" + name + ext, out))
        return true;
    return false;
  }
  // A playable track in folder whose name has "title" in it.
  bool titled(const std::string &folder, Theme &out) {
    std::vector<std::string> names;
    auto loose = findLoose(dir, folder);
    if (!loose.empty())
      for (auto &n : listDir(loose))
        names.push_back(folder + "/" + n);
    if (archive) {
      auto prefix = lowered(folder) + "/";
      for (auto &item : archive->items)
        if (item.first.compare(0, prefix.size(), prefix) == 0)
          names.push_back(item.first);
    }
    for (auto &n : names) {
      auto base = lowered(n.substr(n.find_last_of('/') + 1));
      if (base.find("title") != std::string::npos && playable(extension(n)) && locate(n, out)) {
        out.name = base.substr(0, base.find_last_of('.'));
        return true;
      }
    }
    return false;
  }
};

bool findIn(const std::string &dir, Theme &out) {
  Game game;
  game.dir = dir;
  for (auto ext : {".ogg", ".mp3", ".wav"})
    if (game.locate(std::string("hardrpg-theme") + ext, out)) {
      out.name = "hardrpg-theme";
      return true;
    }
  // RPG Maker 2000/2003.
  auto ldb = findLoose(dir, "RPG_RT.ldb");
  if (!ldb.empty()) {
    int volume = 100;
    auto name = lcfTitleMusic(readWhole(ldb, 32u << 20), volume);
    if (!name.empty() && game.track("Music", name, out)) {
      out.name = name;
      out.volume = volume;
      return true;
    }
    return game.titled("Music", out);
  }
  // RPG Maker XP, VX and VX Ace.
  for (auto &n : listDir(dir)) {
    auto ext = extension(n);
    if (ext == ".rgssad" || ext == ".rgss2a" || ext == ".rgss3a") {
      std::unique_ptr<Archive> archive(new Archive);
      if (archive->open(dir + "/" + n)) {
        game.archive = std::move(archive);
        break;
      }
    }
  }
  std::string system;
  for (auto rel : {"Data/System.rvdata2", "Data/System.rvdata", "Data/System.rxdata"}) {
    system = game.data(rel, 4u << 20);
    if (!system.empty())
      break;
  }
  if (system.empty() && !game.archive && findLoose(dir, "Audio/BGM").empty())
    return false; // not a game folder
  int volume = 100;
  auto name = marshalTitleBgm(system, volume);
  if (!name.empty() && game.track("Audio/BGM", name, out)) {
    out.name = name;
    out.volume = volume;
    return true;
  }
  return game.titled("Audio/BGM", out);
}
} // namespace

std::string marshalTitleBgm(const std::string &systemData, int &volume) {
  if (systemData.size() < 3 || systemData[0] != 4 || systemData[1] != 8)
    return "";
  Marshal m;
  m.p = reinterpret_cast<const unsigned char *>(systemData.data()) + 2;
  m.end = reinterpret_cast<const unsigned char *>(systemData.data()) + systemData.size();
  auto root = m.value();
  const Value *bgm = root ? root->field("@title_bgm") : nullptr;
  const Value *name = bgm ? bgm->field("@name") : nullptr;
  if (!name || name->type != '"')
    return "";
  if (auto *v = bgm->field("@volume"))
    if (v->type == 'i' && v->number > 0 && v->number <= 100)
      volume = int(v->number);
  return name->text;
}

std::string lcfTitleMusic(const std::string &database, int &volume) {
  Lcf r;
  r.p = reinterpret_cast<const unsigned char *>(database.data());
  r.end = r.p + database.size();
  uint32_t headerSize = r.ber();
  if (!r.ok || headerSize > uint32_t(r.end - r.p) ||
      std::string(reinterpret_cast<const char *>(r.p), headerSize) != "LcfDataBase")
    return "";
  r.p += headerSize;
  std::string name;
  r.chunks([&](uint32_t id, const unsigned char *at, uint32_t size) {
    if (id != 0x16) // System
      return true;
    Lcf system{at, at + size};
    system.chunks([&](uint32_t id, const unsigned char *at, uint32_t size) {
      if (id != 0x1F) // title_music
        return true;
      Lcf music{at, at + size};
      music.chunks([&](uint32_t id, const unsigned char *at, uint32_t size) {
        if (id == 0x01)
          name.assign(reinterpret_cast<const char *>(at), size);
        else if (id == 0x03) {
          Lcf n{at, at + size};
          uint32_t v = n.ber();
          if (n.ok && v > 0 && v <= 100)
            volume = int(v);
        }
        return true;
      });
      return false;
    });
    return false;
  });
  return name == "(OFF)" ? "" : name;
}

bool findTheme(const std::string &gameDir, Theme &out) {
  out = Theme();
  if (gameDir.empty() || !isDirectory(gameDir))
    return false;
  if (findIn(gameDir, out))
    return true;
  // A ZIP often holds the game in a folder of its own.
  for (auto &n : listDir(gameDir)) {
    auto sub = gameDir + "/" + n;
    if (isDirectory(sub) && findIn(sub, out))
      return true;
  }
  return false;
}
} // namespace hardrpg
