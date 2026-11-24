#include "buffered_io.h"
#include "theme.h"
#include <algorithm>
#include <cstdint>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <physfs.h>
#include <sys/stat.h>
#include <vector>

namespace hardrpg {
namespace {
// Tracks longer than this are left alone rather than read into memory.
constexpr uint32_t maxArchivedTrack = 32u << 20;
// Bumped when the search changes, so cached answers are found again.
constexpr int searchVersion = 4;

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

// ---------------------------------------------------------------- files
// Reading a file: from disk, or from inside a ZIP through PhysFS.
struct Reader {
  virtual ~Reader() {}
  virtual size_t read(void *to, size_t n) = 0;
  virtual bool seek(uint64_t at) = 0;
  virtual uint64_t size() = 0;
};
struct DiskReader : Reader {
  FILE *f;
  explicit DiskReader(FILE *file) : f(file) {}
  ~DiskReader() { std::fclose(f); }
  size_t read(void *to, size_t n) override { return std::fread(to, 1, n, f); }
  bool seek(uint64_t at) override { return fseeko(f, off_t(at), SEEK_SET) == 0; }
  uint64_t size() override {
    struct stat st;
    return fstat(fileno(f), &st) == 0 ? uint64_t(st.st_size) : 0;
  }
};
struct ZipReader : Reader {
  PHYSFS_File *f;
  explicit ZipReader(PHYSFS_File *file) : f(file) {}
  ~ZipReader() { PHYSFS_close(f); }
  size_t read(void *to, size_t n) override {
    auto got = PHYSFS_readBytes(f, to, n);
    return got < 0 ? 0 : size_t(got);
  }
  bool seek(uint64_t at) override { return PHYSFS_seek(f, at) != 0; }
  uint64_t size() override {
    auto n = PHYSFS_fileLength(f);
    return n < 0 ? 0 : uint64_t(n);
  }
};

// The files a game is made of: a folder on disk, or a mounted ZIP.
struct Files {
  bool disk = true;
  // 0 missing, 1 file, 2 folder.
  int kind(const std::string &path) const {
    if (disk) {
      struct stat st;
      if (stat(path.c_str(), &st) != 0)
        return 0;
      return S_ISDIR(st.st_mode) ? 2 : S_ISREG(st.st_mode) ? 1 : 0;
    }
    PHYSFS_Stat st;
    if (!PHYSFS_stat(path.c_str(), &st))
      return 0;
    return st.filetype == PHYSFS_FILETYPE_DIRECTORY ? 2 : st.filetype == PHYSFS_FILETYPE_REGULAR ? 1 : 0;
  }
  std::vector<std::string> list(const std::string &dir) const {
    std::vector<std::string> names;
    if (disk) {
      if (DIR *d = opendir(dir.c_str())) {
        while (auto *e = readdir(d))
          if (e->d_name[0] != '.')
            names.push_back(e->d_name);
        closedir(d);
      }
    } else if (char **found = PHYSFS_enumerateFiles(dir.c_str())) {
      for (char **n = found; *n; ++n)
        if ((*n)[0] != '.')
          names.push_back(*n);
      PHYSFS_freeList(found);
    }
    std::sort(names.begin(), names.end());
    return names;
  }
  std::unique_ptr<Reader> open(const std::string &path) const {
    if (disk) {
      FILE *f = std::fopen(path.c_str(), "rb");
      return f ? std::unique_ptr<Reader>(new DiskReader(f)) : nullptr;
    }
    PHYSFS_File *f = PHYSFS_openRead(path.c_str());
    return f ? std::unique_ptr<Reader>(new ZipReader(f)) : nullptr;
  }
  std::string readWhole(const std::string &path, size_t limit) const {
    std::string out;
    auto r = open(path);
    if (!r)
      return out;
    uint64_t n = r->size();
    if (n > limit)
      return out;
    out.resize(size_t(n));
    if (n && r->read(&out[0], size_t(n)) != size_t(n))
      out.clear();
    return out;
  }
  // dir/rel with each part matched without regard to case, as Windows does.
  std::string find(const std::string &dir, const std::string &rel) const {
    std::string at = dir;
    size_t start = 0;
    while (start <= rel.size()) {
      size_t end = rel.find('/', start);
      auto part = rel.substr(start, end == std::string::npos ? std::string::npos : end - start);
      std::string next = at + "/" + part;
      if (!kind(next)) {
        next.clear();
        auto want = lowered(part);
        for (auto &name : list(at))
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
};

// ---------------------------------------------------------------- encrypted archives
// Game.rgssad (XP), .rgss2a (VX) and .rgss3a (VX Ace). Names and data are
// XORed with a key that changes every four bytes (key * 7 + 3).
struct Archive {
  struct Item {
    uint64_t offset;
    uint32_t size, key;
  };
  const Files *files = nullptr;
  std::string path;
  std::map<std::string, Item> items; // lowered, '/'-separated names
  bool open(const Files &fs, const std::string &file) {
    files = &fs;
    path = file;
    auto f = fs.open(file);
    if (!f)
      return false;
    uint64_t at = 0;
    auto u32 = [&](uint32_t &v) {
      unsigned char b[4];
      if (f->read(b, 4) != 4)
        return false;
      at += 4;
      v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
      return true;
    };
    auto add = [&](std::string name, uint64_t offset, uint32_t size, uint32_t key) {
      std::replace(name.begin(), name.end(), '\\', '/');
      items[lowered(name)] = {offset, size, key};
    };
    uint64_t total = f->size();
    char magic[8];
    if (f->read(magic, 8) == 8 && std::memcmp(magic, "RGSSAD\0", 7) == 0) {
      at = 8;
      if (magic[7] == 1) {
        uint32_t key = 0xDEADCAFE, n, size;
        for (int guard = 0; guard < 200000 && u32(n); ++guard) {
          n ^= key, key = key * 7 + 3;
          if (n == 0 || n > 512)
            break;
          std::string name(n, '\0');
          if (f->read(&name[0], n) != n)
            break;
          at += n;
          for (auto &c : name)
            c = char((unsigned char)c ^ (key & 0xff)), key = key * 7 + 3;
          if (!u32(size))
            break;
          size ^= key, key = key * 7 + 3;
          if (at + size > total)
            break;
          add(name, at, size, key);
          at += size;
          if (!f->seek(at))
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
            if (f->read(&name[0], n) != n)
              break;
            at += n;
            for (size_t i = 0; i < name.size(); ++i)
              name[i] = char((unsigned char)name[i] ^ ((key >> (8 * (i % 4))) & 0xff));
            add(name, offset, size, start);
          }
        }
      }
    }
    return !items.empty();
  }
  std::string read(const Item &item) const {
    std::string out;
    if (item.size > maxArchivedTrack)
      return out;
    auto f = files->open(path);
    if (!f)
      return out;
    out.resize(item.size);
    if (!f->seek(item.offset) || f->read(&out[0], item.size) != item.size)
      return "";
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
  const Files *fs = nullptr;
  std::string dir;
  std::vector<std::string> rtps; // RTP folders on disk
  std::unique_ptr<Archive> archive;
  Files disk;
  // A file of the game, loose first, then from its archive.
  bool locate(const std::string &rel, Theme &out) {
    auto loose = fs->find(dir, rel);
    if (!loose.empty() && fs->kind(loose) == 1) {
      if (fs->disk)
        out.file = loose;
      else
        out.bytes = fs->readWhole(loose, maxArchivedTrack);
      out.ext = extension(rel);
      return fs->disk || !out.bytes.empty();
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
  bool has(const std::string &rel) {
    auto loose = fs->find(dir, rel);
    return (!loose.empty() && fs->kind(loose) == 1) || (archive && archive->items.count(lowered(rel)));
  }
  std::string data(const std::string &rel, size_t limit) {
    auto loose = fs->find(dir, rel);
    if (!loose.empty() && fs->kind(loose) == 1)
      return fs->readWhole(loose, limit);
    Theme t;
    return locate(rel, t) ? t.bytes : "";
  }
  // folder/name with any extension the engine accepts, playable ones first:
  // the game's own files, then the RTP's.
  bool track(const std::string &folder, const std::string &name, Theme &out) {
    for (auto ext : {".ogg", ".mp3", ".wav"})
      if (locate(folder + "/" + name + ext, out))
        return true;
    for (auto &rtp : rtps)
      for (auto ext : {".ogg", ".mp3", ".wav"}) {
        auto p = disk.find(rtp, folder + "/" + name + ext);
        if (!p.empty() && disk.kind(p) == 1) {
          out.file = p;
          out.ext = ext;
          return true;
        }
      }
    return false;
  }
  bool midiTrack(const std::string &folder, const std::string &name) {
    for (auto ext : {".mid", ".midi"}) {
      if (has(folder + "/" + name + ext))
        return true;
      for (auto &rtp : rtps)
        if (!disk.find(rtp, folder + "/" + name + ext).empty())
          return true;
    }
    return false;
  }
  // A playable track in folder whose name has "title" in it.
  bool titled(const std::string &folder, Theme &out) {
    std::vector<std::string> names;
    auto loose = fs->find(dir, folder);
    if (!loose.empty())
      for (auto &n : fs->list(loose))
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
  // Plays name from folder, or says why not, then tries a "title" track.
  bool named(const std::string &folder, const std::string &name, int volume, int pitch, Theme &out) {
    if (name.empty())
      out.note = "the game sets no title music";
    else if (track(folder, name, out)) {
      out.name = name;
      out.volume = volume;
      out.pitch = pitch;
      return true;
    } else if (midiTrack(folder, name))
      out.note = "the title music \"" + name + "\" is MIDI";
    else
      out.note = "the title music \"" + name + "\" is not in the game or the RTP";
    std::string why = out.note;
    if (titled(folder, out)) {
      out.note.clear();
      return true;
    }
    out.note = why;
    return false;
  }
};

std::mutex zipLock;

// Some releases keep their music in a ZIP beside the game rather than in an
// Audio folder (To the Moon's Audio.dat holds Audio/BGM/...). Looks for the
// title track, by name or a "title" track, in each such ZIP.
bool audioPack(const std::string &dir, const std::vector<std::string> &rtps, const std::string &name, int volume,
               int pitch, Theme &out) {
  Files disk;
  for (auto &n : disk.list(dir)) {
    auto ext = extension(n);
    if (ext != ".dat" && ext != ".zip")
      continue;
    auto path = dir + "/" + n;
    char magic[4] = {};
    {
      std::ifstream in(path, std::ios::binary);
      if (!in.read(magic, 4) || std::memcmp(magic, "PK\x03\x04", 4) != 0)
        continue;
    }
    std::lock_guard<std::mutex> hold(zipLock);
    if (!PHYSFS_isInit() && !PHYSFS_init("hardrpg"))
      return false;
    if (PHYSFS_getMountPoint(path.c_str()) || !mountArchive(path, "/hardrpg-audio-pack"))
      continue;
    Files packed;
    packed.disk = false;
    Game pack;
    pack.fs = &packed;
    pack.dir = "/hardrpg-audio-pack";
    pack.rtps = rtps;
    Theme t;
    bool found = !packed.find(pack.dir, "Audio/BGM").empty() &&
                 (name.empty() ? pack.titled("Audio/BGM", t) : pack.named("Audio/BGM", name, volume, pitch, t));
    PHYSFS_unmount(path.c_str());
    if (found) {
      t.note.clear();
      out = t;
      return true;
    }
  }
  return false;
}

// 1 found, 0 a game without title music (out.note says why), -1 not a game.
int findIn(const Files &fs, const std::string &dir, const std::vector<std::string> &rtps, Theme &out) {
  Game game;
  game.fs = &fs;
  game.dir = dir;
  game.rtps = rtps;
  for (auto ext : {".ogg", ".mp3", ".wav"})
    if (game.locate(std::string("hardrpg-theme") + ext, out)) {
      out.name = "hardrpg-theme";
      return 1;
    }
  // RPG Maker 2000/2003.
  auto ldb = fs.find(dir, "RPG_RT.ldb");
  if (!ldb.empty()) {
    int volume = 100, pitch = 100;
    auto name = lcfTitleMusic(fs.readWhole(ldb, 32u << 20), volume, &pitch);
    return game.named("Music", name, volume, pitch, out) ? 1 : 0;
  }
  // RPG Maker XP, VX and VX Ace.
  for (auto &n : fs.list(dir)) {
    auto ext = extension(n);
    if (ext == ".rgssad" || ext == ".rgss2a" || ext == ".rgss3a") {
      std::unique_ptr<Archive> archive(new Archive);
      if (archive->open(fs, dir + "/" + n)) {
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
  if (system.empty() && !game.archive && fs.find(dir, "Audio/BGM").empty())
    return -1;
  int volume = 100, pitch = 100;
  auto name = marshalTitleBgm(system, volume, &pitch);
  if (system.empty())
    out.note = "the game's System data was not found";
  if (system.empty() ? game.titled("Audio/BGM", out) : game.named("Audio/BGM", name, volume, pitch, out)) {
    out.note.clear();
    return 1;
  }
  if (fs.disk && audioPack(dir, rtps, name, volume, pitch, out))
    return 1;
  return 0;
}

// The game itself, or a ZIP's inner folder holding it.
bool search(const Files &fs, const std::string &dir, const std::vector<std::string> &rtps, Theme &out) {
  int found = findIn(fs, dir, rtps, out);
  if (found >= 0)
    return found > 0;
  for (auto &n : fs.list(dir)) {
    auto sub = dir + "/" + n;
    if (fs.kind(sub) != 2)
      continue;
    Theme t;
    found = findIn(fs, sub, rtps, t);
    if (found >= 0) {
      out = t;
      return found > 0;
    }
  }
  out.note = "no RPG Maker game found in it";
  return false;
}

bool searchZip(const std::string &zip, const std::vector<std::string> &rtps, Theme &out) {
  std::lock_guard<std::mutex> hold(zipLock);
  if (!PHYSFS_isInit() && !PHYSFS_init("hardrpg")) {
    out.note = "ZIP reading is unavailable";
    return false;
  }
  // The library may have this ZIP mounted already; then read it there.
  const char *existing = PHYSFS_getMountPoint(zip.c_str());
  std::string point = existing ? existing : "/hardrpg-title-music";
  if (!existing && !mountArchive(zip, point)) {
    out.note = "the ZIP could not be opened";
    return false;
  }
  while (point.size() > 1 && point.back() == '/')
    point.pop_back();
  Files fs;
  fs.disk = false;
  bool found = search(fs, point, rtps, out);
  if (!existing)
    PHYSFS_unmount(zip.c_str());
  return found;
}

// ---------------------------------------------------------------- remembered answers
std::string hashed(const std::string &s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s)
    h = (h ^ c) * 1099511628211ull;
  char text[17];
  std::snprintf(text, sizeof text, "%016llx", (unsigned long long)h);
  return text;
}
// Sizes and times of what decides the answer: the source; the game's
// archives, database and override track; its Data/System files and BGM or
// Music folder; for a folder holding the game one level down (an unpacked
// ZIP), the same for that folder; and the RTP folders. Only these names are
// looked at, never every file: some games hold thousands.
std::string stampOf(const std::string &source, const std::vector<std::string> &rtps) {
  struct stat st;
  if (stat(source.c_str(), &st) != 0)
    return "";
  std::string s = std::to_string(searchVersion) + "|" + std::to_string((long long)st.st_size) + ":" +
                  std::to_string((long long)st.st_mtime);
  Files fs;
  auto add = [&](const std::string &path) {
    struct stat e;
    if (stat(path.c_str(), &e) == 0)
      s += "|" + path.substr(source.size()) + ":" + std::to_string((long long)e.st_size) + ":" +
           std::to_string((long long)e.st_mtime);
  };
  // Returns whether dir looks like a game.
  auto game = [&](const std::string &dir) {
    bool found = false;
    for (auto &n : fs.list(dir)) {
      auto low = lowered(n), ext = extension(n);
      auto path = dir + "/" + n;
      if (ext == ".dat")
        add(path); // a ZIP of the game's audio, see audioPack
      if (ext == ".rgssad" || ext == ".rgss2a" || ext == ".rgss3a" || low == "rpg_rt.ldb" || low == "game.ini" ||
          low.compare(0, 13, "hardrpg-theme") == 0) {
        add(path);
        found = true;
      } else if (low == "data") {
        for (auto &d : fs.list(path))
          if (lowered(d).compare(0, 7, "system.") == 0)
            add(path + "/" + d);
        found = true;
      } else if (low == "audio") {
        for (auto &d : fs.list(path))
          if (lowered(d) == "bgm")
            add(path + "/" + d);
        found = true;
      } else if (low == "music") {
        add(path);
        found = true;
      }
    }
    return found;
  };
  if (S_ISDIR(st.st_mode) && !game(source))
    for (auto &n : fs.list(source))
      if (fs.kind(source + "/" + n) == 2)
        game(source + "/" + n);
  for (auto &rtp : rtps)
    s += "|rtp:" + rtp;
  return s;
}

std::vector<std::string> lines(const std::string &text) {
  std::vector<std::string> out;
  size_t start = 0;
  while (start < text.size()) {
    auto end = text.find('\n', start);
    if (end == std::string::npos)
      end = text.size();
    out.push_back(text.substr(start, end - start));
    start = end + 1;
  }
  return out;
}
std::string readFile(const std::string &path) {
  Files disk;
  return disk.readWhole(path, 64 * 1024);
}
bool writeFile(const std::string &path, const std::string &data) {
  auto temp = path + ".tmp";
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  ok = std::fclose(f) == 0 && ok;
  if (ok) {
    std::remove(path.c_str());
    ok = std::rename(temp.c_str(), path.c_str()) == 0;
  }
  if (!ok)
    std::remove(temp.c_str());
  return ok;
}
void makeDirs(const std::string &path) {
  for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
    mkdir(path.substr(0, at).c_str(), 0777);
  mkdir(path.c_str(), 0777);
}
} // namespace

void themeInit() {
  std::lock_guard<std::mutex> hold(zipLock);
  if (!PHYSFS_isInit())
    PHYSFS_init("hardrpg");
}

std::string marshalTitleBgm(const std::string &systemData, int &volume, int *pitch) {
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
  if (auto *v = bgm->field("@pitch"))
    if (pitch && v->type == 'i' && v->number >= 50 && v->number <= 150)
      *pitch = int(v->number);
  return name->text;
}

std::string lcfTitleMusic(const std::string &database, int &volume, int *pitch) {
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
        Lcf n{at, at + size};
        if (id == 0x01)
          name.assign(reinterpret_cast<const char *>(at), size);
        else if (id == 0x03) {
          uint32_t v = n.ber();
          if (n.ok && v > 0 && v <= 100)
            volume = int(v);
        } else if (id == 0x04) { // tempo
          uint32_t v = n.ber();
          if (n.ok && pitch && v >= 50 && v <= 150)
            *pitch = int(v);
        }
        return true;
      });
      return false;
    });
    return false;
  });
  return name == "(OFF)" ? "" : name;
}

bool findTheme(const std::string &source, Theme &out, const std::string &cache, const std::vector<std::string> &rtps) {
  out = Theme();
  Files disk;
  int kind = source.empty() ? 0 : disk.kind(source);
  // A track the player chose (art/<folder>/theme.ogg): played as it is.
  if (kind == 1 && playable(extension(source))) {
    out.file = source;
    out.ext = extension(source);
    out.name = "theme";
    return true;
  }
  bool zip = kind == 1 && extension(source) == ".zip";
  if (kind != 2 && !zip) {
    out.note = "the game is not there";
    return false;
  }
  std::string key, stamp, record;
  if (!cache.empty()) {
    key = cache + "/" + hashed(source);
    stamp = stampOf(source, rtps);
    auto saved = lines(readFile(key + ".txt"));
    // stamp, found, name, volume, pitch, ext, file, note
    if (saved.size() >= 8 && saved[0] == stamp) {
      out.name = saved[2];
      out.volume = std::atoi(saved[3].c_str());
      out.pitch = std::atoi(saved[4].c_str());
      out.ext = saved[5];
      out.file = saved[6];
      out.note = saved[7];
      if (saved[1] != "1")
        return false;
      if (disk.kind(out.file) == 1)
        return true;
      out = Theme(); // the track went away: look again
    }
  }
  bool found = zip ? searchZip(source, rtps, out) : search(disk, source, rtps, out);
  if (cache.empty())
    return found;
  makeDirs(cache);
  // A track read out of an archive or a ZIP is kept, to play from disk.
  if (found && !out.bytes.empty()) {
    auto file = key + out.ext;
    if (writeFile(file, out.bytes)) {
      out.file = file;
      out.bytes.clear();
    }
  }
  if (!out.bytes.empty())
    return found; // could not be kept: play it from memory this once
  writeFile(key + ".txt", stamp + "\n" + (found ? "1" : "0") + "\n" + out.name + "\n" + std::to_string(out.volume) +
                              "\n" + std::to_string(out.pitch) + "\n" + out.ext + "\n" + out.file + "\n" + out.note +
                              "\n" + source + "\n");
  return found;
}
} // namespace hardrpg
