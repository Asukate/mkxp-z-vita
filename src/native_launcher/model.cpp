#include "buffered_io.h"
#include "model.h"
#include "i18n.h"
#include "release.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <atomic>
#ifdef __vita__
#include <psp2/appmgr.h>
#include <psp2/kernel/threadmgr.h>
#else
#include <thread>
#endif
#include <physfs.h>
#include <set>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace hardrpg {
namespace {
std::string join(const std::string &a, const std::string &b) {
  return a + (a.empty() || a.back() == '/' ? "" : "/") + b;
}
std::string basename(const std::string &p) {
  return p.substr(p.find_last_of('/') + 1);
}
std::string parent(const std::string &p) {
  return p.substr(0, p.find_last_of('/'));
}
bool suffix(const std::string &p, const std::string &s) {
  return p.size() >= s.size() && lower(p.substr(p.size() - s.size())) == s;
}
bool statPath(const std::string &p, struct stat &s) {
  return !lstat(p.c_str(), &s);
}
bool regular(const std::string &p) {
  struct stat s;
  return statPath(p, s) && S_ISREG(s.st_mode);
}
bool exists(const std::string &p) {
  struct stat s;
  return statPath(p, s);
}
void mkdirOne(const std::string &p) {
  if (!directory(p) && mkdir(p.c_str(), 0777))
    throw std::runtime_error(trf("Cannot create folder: {}", {p}));
}
std::vector<std::string> split(const std::string &s, char delimiter) {
  std::vector<std::string> out;
  size_t start = 0;
  do {
    auto end = s.find(delimiter, start);
    out.push_back(s.substr(start, end - start));
    if (end == std::string::npos)
      break;
    start = end + 1;
  } while (true);
  return out;
}
std::string trim(const std::string &s) {
  auto a = s.find_first_not_of(" \r\n\t");
  return a == std::string::npos
             ? ""
             : s.substr(a, s.find_last_not_of(" \r\n\t") - a + 1);
}
std::string scrubUtf8(const std::string &s) {
  std::string out;
  for (size_t i = 0; i < s.size();) {
    auto first = uint8_t(s[i]);
    if (first < 128) {
      out += s[i++];
      continue;
    }
    int count = first >= 0xc2 && first <= 0xdf   ? 2
                : first >= 0xe0 && first <= 0xef ? 3
                : first >= 0xf0 && first <= 0xf4 ? 4
                                                 : 0;
    uint32_t cp = count ? first & ((1u << (7 - count)) - 1) : 0;
    bool valid = count && i + count <= s.size();
    for (int n = 1; valid && n < count; ++n) {
      auto c = uint8_t(s[i + n]);
      valid = (c & 0xc0) == 0x80;
      cp = (cp << 6) | (c & 63);
    }
    valid = valid &&
            cp >= (count == 2   ? 0x80u
                   : count == 3 ? 0x800u
                                : 0x10000u) &&
            cp <= 0x10ffff && !(cp >= 0xd800 && cp <= 0xdfff);
    if (valid) {
      out.append(s, i, count);
      i += count;
    } else {
      out += '?';
      ++i;
    }
  }
  return out;
}
Json object() { return Json(std::initializer_list<Json::pair_type>{}); }
Json array() { return Json(std::initializer_list<Json>{}); }
std::string jsonString(const Json &v, const std::string &k,
                       const std::string &fallback = "") {
  auto i = v.as_object().find(k);
  return i == v.as_object().end() ? fallback : i->second.as_string();
}
std::vector<File> directoryFiles(const std::string &p) {
  DIR *d = opendir(p.c_str());
  if (!d)
    throw std::runtime_error(trf("Cannot read folder: {}", {p}));
  std::vector<File> out;
  while (auto *e = readdir(d)) {
    std::string name = e->d_name;
    if (!relative(name) || name.find('/') != std::string::npos)
      continue;
    struct stat s;
    if (!statPath(join(p, name), s) || S_ISLNK(s.st_mode))
      continue;
    if (S_ISREG(s.st_mode) || S_ISDIR(s.st_mode))
      out.push_back({name, S_ISDIR(s.st_mode), s.st_size});
  }
  closedir(d);
  return out;
}
// Every archive operation owns a private, read-only mount and closes it before
// returning. The launcher never mounts archives into the game's asset
// namespace.
// Archive views in one operation share the central-directory index. Unique
// mount points also keep background RTP checks separate from game inspection.
struct ArchiveMount {
  std::string source, point;
  explicit ArchiveMount(const std::string &p, const std::string &m) : source(p), point(m) {
    if (!PHYSFS_isInit() && !PHYSFS_init("hardrpg"))
      throw std::runtime_error("Cannot initialize archive reader");
    if (!mountArchive(p, point))
      throw std::runtime_error(tr("Cannot open ZIP archive"));
  }
  ~ArchiveMount() { PHYSFS_unmount(source.c_str()); }
};
// Share archive views across workers without lazy static initialization.
std::atomic_flag archiveLock = ATOMIC_FLAG_INIT;
std::map<std::string, std::shared_ptr<ArchiveMount>> archiveViews;
unsigned archiveSerial = 0;
struct ArchiveGuard {
  ArchiveGuard() {
    while (archiveLock.test_and_set(std::memory_order_acquire)) {
#ifdef __vita__
      sceKernelDelayThread(1000);
#else
      std::this_thread::yield();
#endif
    }
  }
  ~ArchiveGuard() { archiveLock.clear(std::memory_order_release); }
};
class Archive {
  std::shared_ptr<ArchiveMount> mount;
public:
  explicit Archive(const std::string &p) {
    ArchiveGuard lock;
    auto &views = archiveViews;
    auto found = views.find(p);
    if (found != views.end()) mount = found->second;
    else {
      mount = std::make_shared<ArchiveMount>(p, "__hardrpg_native_" + std::to_string(++archiveSerial));
      views[p] = mount;
    }
  }
  ~Archive() {
    ArchiveGuard lock;
    if (mount.use_count() == 2) archiveViews.erase(mount->source);
    mount.reset();
  }
  Archive(const Archive &) = delete;
  static void fail() {
    auto *e = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
    throw std::runtime_error(std::string("ZIP error: ") + (e ? e : "unknown"));
  }
  std::string full(const std::string &p) const {
    if (!relative(p, true))
      throw std::runtime_error("Invalid ZIP path");
    return join(mount->point, p);
  }
  std::vector<File> files(const std::string &p) const {
    std::string f = full(p);
    char **names = PHYSFS_enumerateFiles(f.c_str());
    if (!names)
      fail();
    std::vector<File> out;
    for (char **n = names; *n; ++n) {
      if (!relative(*n) || std::strchr(*n, '/'))
        continue;
      PHYSFS_Stat s;
      if (!PHYSFS_stat(join(f, *n).c_str(), &s))
        continue;
      if (s.filetype == PHYSFS_FILETYPE_REGULAR ||
          s.filetype == PHYSFS_FILETYPE_DIRECTORY)
        out.push_back(
            {*n, s.filetype == PHYSFS_FILETYPE_DIRECTORY, s.filesize});
    }
    PHYSFS_freeList(names);
    return out;
  }
  std::string read(const std::string &p) const {
    PHYSFS_File *f = PHYSFS_openRead(full(p).c_str());
    if (!f)
      fail();
    int64_t size = PHYSFS_fileLength(f);
    if (size < 0 || size > 262144) {
      PHYSFS_close(f);
      throw std::runtime_error("Game.ini exceeds 256 KiB");
    }
    std::string result(size, '\0');
    auto count = size ? PHYSFS_readBytes(f, &result[0], size) : 0;
    PHYSFS_close(f);
    if (count != size)
      throw std::runtime_error(tr("Truncated ZIP file"));
    return result;
  }
  void extract(const std::string &inner, const std::string &dest,
               int64_t expected, uint64_t &copied, uint64_t total,
               const Progress &progress) const {
    PHYSFS_File *f = PHYSFS_openRead(full(inner).c_str());
    if (!f)
      fail();
    FILE *out = nullptr;
    try {
      if (PHYSFS_fileLength(f) != expected)
        throw std::runtime_error("ZIP file size changed");
      out = fopen(dest.c_str(), "wb");
      if (!out)
        throw std::runtime_error("Cannot write ZIP cache");
      char buffer[65536];
      int64_t written = 0;
      for (;;) {
        auto n = PHYSFS_readBytes(f, buffer, sizeof(buffer));
        if (n < 0)
          fail();
        if (!n)
          break;
        if (fwrite(buffer, 1, n, out) != size_t(n))
          throw std::runtime_error(tr("Cannot write ZIP cache (storage may be full)"));
        written += n;
        copied += n;
        if (progress)
          progress(copied, total);
      }
      if (written != expected)
        throw std::runtime_error("ZIP file was truncated");
      int rc = fclose(out);
      out = nullptr;
      if (rc)
        throw std::runtime_error("Cannot finish ZIP cache file");
      PHYSFS_close(f);
    } catch (...) {
      if (out)
        fclose(out);
      PHYSFS_close(f);
      throw;
    }
  }
};
void sortEntries(std::vector<Entry> &list) {
  std::stable_sort(list.begin(), list.end(),
                   [](const Entry &a, const Entry &b) {
                     if ((a.kind == Kind::Folder) != (b.kind == Kind::Folder))
                       return a.kind == Kind::Folder;
                     return lower(a.name) < lower(b.name);
                   });
}
std::vector<std::string> aliases(const std::string &p) {
  if (p == "Standard")
    return {"Standard", "XP"};
  if (p == "RPGVX")
    return {"RPGVX", "VX"};
  if (p == "RPGVXAce")
    return {"RPGVXAce", "VXACE"};
  return {p};
}
bool alias(const std::string &name, const std::string &p, bool zip = false) {
  for (auto &a : aliases(p))
    if (lower(name) == lower(a + (zip ? ".zip" : "")))
      return true;
  return false;
}
void removePartial(const std::string &p) {
  if (!exists(p))
    return;
  if (!directory(p))
    throw std::runtime_error(
        "Refusing to remove a linked or non-directory cache");
  DIR *d = opendir(p.c_str());
  if (!d)
    throw std::runtime_error("Cannot read partial cache");
  std::vector<std::string> names;
  while (auto *e = readdir(d)) {
    std::string n = e->d_name;
    if (n != "." && n != "..")
      names.push_back(n);
  }
  closedir(d);
  for (auto &n : names) {
    auto f = join(p, n);
    if (directory(f))
      removePartial(f);
    else if (unlink(f.c_str()))
      throw std::runtime_error("Cannot remove partial cache file");
  }
  if (rmdir(p.c_str()))
    throw std::runtime_error("Cannot remove partial cache");
}
// Deletes a game folder or an unpacked copy. Links are removed, never
// followed, so nothing outside the folder can be reached.
void removeTree(const std::string &p) {
  struct stat s;
  if (!statPath(p, s))
    return;
  if (!S_ISDIR(s.st_mode)) {
    if (unlink(p.c_str()))
      throw std::runtime_error("Cannot delete " + basename(p));
    return;
  }
  DIR *d = opendir(p.c_str());
  if (!d)
    throw std::runtime_error("Cannot read " + basename(p));
  std::vector<std::string> names;
  while (auto *e = readdir(d)) {
    std::string n = e->d_name;
    if (n != "." && n != "..")
      names.push_back(n);
  }
  closedir(d);
  for (auto &n : names)
    removeTree(join(p, n));
  if (rmdir(p.c_str()))
    throw std::runtime_error("Cannot delete " + basename(p));
}
// The ZIP-level node of a game inside a ZIP: the ZIP as the library lists it.
Node zipOf(const Node &n) {
  Node z = n;
  z.inner.clear();
  z.alias.clear();
  return z;
}
struct ExtractFile {
  std::string inner, path;
  int64_t size;
};
void collect(const Node &node, const std::string &prefix,
             std::vector<ExtractFile> &out, const Progress &progress, int depth = 0) {
  if (depth > 64)
    throw std::runtime_error("ZIP folder nesting exceeds 64 levels");
  if (progress) progress(0, 0);
  for (auto &f : node.files()) {
    if (!relative(f.name))
      throw std::runtime_error("Unsafe ZIP filename");
    if (f.name == "__MACOSX")
      continue;
    auto dest = prefix.empty() ? f.name : join(prefix, f.name);
    if (f.directory)
      collect(node.child(f.name), dest, out, progress, depth + 1);
    else {
      if (f.size < 0)
        throw std::runtime_error("Invalid ZIP file size");
      out.push_back({node.inner.empty() ? f.name : join(node.inner, f.name),
                     dest, f.size});
      if (out.size() > 100000)
        throw std::runtime_error("ZIP has more than 100000 files");
    }
  }
}
} // namespace

const char *const packs[rtpPackCount] = {"Standard", "RPGVX", "RPGVXAce",
                                         "2000", "2003"};
std::string lower(std::string s) {
  for (char &c : s)
    if (c >= 'A' && c <= 'Z')
      c += 32;
  return s;
}
bool relative(const std::string &p, bool empty) {
  if (p.empty())
    return empty;
  for (unsigned char c : p)
    if (c < 32 || c == '\\' || c == ':')
      return false;
  for (auto &part : split(p, '/'))
    if (part.empty() || part == "." || part == "..")
      return false;
  return true;
}
bool Paths::external(const std::string &p) const {
  for (auto &r : storage)
    if (p.compare(0, r.size(), r) == 0 && relative(p.substr(r.size())))
      return true;
  return false;
}
bool directory(const std::string &p) {
  struct stat s;
  return statPath(p, s) && S_ISDIR(s.st_mode);
}
void Paths::setup() const {
  mkdirOne(parent(root));
  mkdirOne(root);
  for (auto n : {"games", "rtp", "config", "cache", "saves"})
    mkdirOne(at(n));
  for (auto p : packs)
    mkdirOne(at(std::string("rtp/") + p));
}
std::string key(const std::string &p) {
  uint64_t v = 14695981039346656037ULL;
  for (unsigned char c : p)
    v = (v ^ c) * 1099511628211ULL;
  std::ostringstream s;
  s << std::hex << std::setfill('0') << std::setw(16) << v;
  return s.str();
}
std::string readFile(const std::string &p, size_t limit) {
  std::ifstream f(p, std::ios::binary);
  if (!f)
    throw std::runtime_error("Cannot read file: " + p);
  std::string out;
  char b[4096];
  while (f) {
    f.read(b, std::min(sizeof(b), limit - out.size()));
    out.append(b, f.gcount());
    if (out.size() == limit) {
      if (f.peek() != EOF)
        throw std::runtime_error("File is too large: " + p);
      break;
    }
  }
  if (f.bad())
    throw std::runtime_error("Cannot read file: " + p);
  return out;
}
void atomicWrite(const std::string &p, const std::string &s) {
  FILE *f = fopen((p + ".tmp").c_str(), "wb");
  if (!f)
    throw std::runtime_error("Cannot write settings or library index");
  bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
  if (fclose(f))
    ok = false;
  if (!ok)
    throw std::runtime_error("Cannot save settings or library index");
  if (!rename((p + ".tmp").c_str(), p.c_str()))
    return;
#ifdef __vita__
  // Vita rename cannot replace an existing destination. Publish a complete
  // temporary file and restore the original if the second rename fails.
  const auto previous = p + ".prev";
  if (regular(p) &&
      (!exists(previous) || (regular(previous) && !unlink(previous.c_str()))) &&
      !rename(p.c_str(), previous.c_str())) {
    if (!rename((p + ".tmp").c_str(), p.c_str()))
      return;
    rename(previous.c_str(), p.c_str());
  }
#endif
  throw std::runtime_error("Cannot save settings or library index");
}
std::string packName(const std::string &name) {
  if (!relative(name) || name.find('/') != std::string::npos)
    throw std::runtime_error("Invalid RTP pack name");
  for (auto p : packs)
    if (alias(name, p))
      return p;
  return name;
}
std::string packLabel(const std::string &n) {
  return n == "Standard"   ? "XP"
         : n == "RPGVX"    ? "VX"
         : n == "RPGVXAce" ? "VX Ace"
                           : n;
}
MissingRtp::MissingRtp(const std::string &p)
    : std::runtime_error(trf("Missing {} RTP. Choose its folder or ZIP in Settings.", {packLabel(p)})),
      pack(p) {}
Node Node::fromPath(const Paths &p, const std::string &path, bool zip,
                    const std::string &inner) {
  if (!p.external(path) || !hardrpg::relative(inner, true))
    throw std::runtime_error("Invalid external path");
  auto games = p.at("games");
  if (path.compare(0, games.size() + 1, games + "/") == 0)
    return {path.substr(games.size() + 1), inner, games, zip};
  return zip ? Node{basename(path), inner, parent(path), true}
             : Node{"", "", path, false};
}
std::string Node::path() const {
  return relative.empty() ? root : join(root, relative);
}
std::string Node::identity(const Paths &p) const {
  if (!alias.empty())
    return alias;
  return (root == p.at("games") ? relative : path()) +
         (archive ? "!/" + inner : "");
}
std::string Node::label(const Paths &p) const {
  return (root == p.at("games") ? "games/" + relative : path()) +
         (archive ? "!/" + inner : "");
}
Node Node::child(const std::string &name, bool zip) const {
  if (!hardrpg::relative(name) || name.find('/') != std::string::npos)
    throw std::runtime_error("Invalid child path");
  return archive ? Node{relative, inner.empty() ? name : join(inner, name),
                        root, true}
                 : Node{relative.empty() ? name : join(relative, name), "",
                        root, zip};
}
std::vector<File> Node::files() const {
  if (archive) {
    Archive a(path());
    return a.files(inner);
  }
  return directoryFiles(path());
}
std::string Node::read(const std::string &name) const {
  if (archive) {
    Archive a(path());
    return a.read(inner.empty() ? name : join(inner, name));
  }
  return readFile(join(path(), name), 262144);
}
namespace {
Json parseGameIni(const std::string &text) {
  auto result = object();
  std::istringstream stream(text);
  std::string line, section;
  while (std::getline(stream, line)) {
    auto s = trim(line);
    if (s.empty() || s[0] == ';' || s[0] == '#')
      continue;
    if (s.front() == '[' && s.back() == ']')
      section = lower(s.substr(1, s.size() - 2));
    else if (section == "game" || section == "rpg_rt") {
      auto eq = s.find('=');
      if (eq != std::string::npos)
        result.as_object()[lower(trim(s.substr(0, eq)))] =
            trim(s.substr(eq + 1));
    }
  }
  return result;
}
int gameVersion(const Json &v, const std::vector<File> &fs) {
  auto lib = lower(jsonString(v, "library"));
  auto scripts = lower(jsonString(v, "scripts"));
  bool database = false, mapTree = false;
  for (const auto &f : fs) {
    if (!f.directory && lower(f.name) == "rpg_rt.ldb")
      database = true;
    if (!f.directory && lower(f.name) == "rpg_rt.lmt")
      mapTree = true;
  }
  if (database && mapTree)
    return 4;
  int version = 0;
  if (lib.find("rgss30") != std::string::npos || suffix(scripts, ".rvdata2"))
    version = 3;
  else if (lib.find("rgss20") != std::string::npos ||
           suffix(scripts, ".rvdata"))
    version = 2;
  else if (lib.find("rgss10") != std::string::npos ||
           suffix(scripts, ".rxdata"))
    version = 1;
  for (int i = 3; !version && i >= 1; --i)
    for (auto &f : fs)
      if (suffix(f.name, i == 3 ? ".rgss3a" : i == 2 ? ".rgss2a" : ".rgssad"))
        version = i;
  return version;
}
std::string gameIniName(const std::vector<File> &fs,
                        const std::function<std::string(const std::string &)> &read) {
  // The engine loads "<name>.ini" and the archive "<name>.rgssad" (or .rgss2a,
  // .rgss3a). A renamed release can keep a stale Game.ini beside its real
  // "Title.ini" and "Title.rgssad" (the Windows To the Moon does): Game.ini
  // only wins when the game's data can actually be found under that name.
  auto hasData = [&](const std::string &ini) {
    auto base = lower(ini.substr(0, ini.size() - 4));
    for (const auto &f : fs) {
      auto name = lower(f.name);
      if (f.directory ? name == "data"
                      : name == base + ".rgssad" || name == base + ".rgss2a" ||
                            name == base + ".rgss3a")
        return true;
    }
    return false;
  };
  std::string game;
  for (const auto &f : fs)
    if (!f.directory && lower(f.name) == "game.ini")
      game = f.name;
  if (!game.empty() && hasData(game))
    return game;
  std::string selected;
  for (const auto &f : fs) {
    if (f.directory || !suffix(f.name, ".ini") || lower(f.name) == "game.ini" ||
        !relative(f.name) || f.name.find('/') != std::string::npos)
      continue;
    auto values = parseGameIni(read(f.name));
    if (values.as_object().empty() || !gameVersion(values, fs))
      continue;
    if (!game.empty() && !hasData(f.name))
      continue;
    if (!selected.empty())
      return game; // Several projects need an explicit Game.ini.
    selected = f.name;
  }
  return selected.empty() ? game : selected;
}
int zipGameVersion(const Node &node) {
  try {
    // Inspect metadata in one read-only mount, including single-folder
    // wrappers. Keep the original ZIP node, display name and save/cache
    // identity intact.
    Archive archive(node.path());
    auto inner = node.inner;
    for (int depth = 0; depth <= 16; ++depth) {
      auto fs = archive.files(inner);
      auto ini = gameIniName(fs, [&](const std::string &name) {
        return archive.read(inner.empty() ? name : join(inner, name));
      });
      if (!ini.empty())
        return gameVersion(parseGameIni(archive.read(
                               inner.empty() ? ini : join(inner, ini))), fs);
      std::vector<std::string> folders;
      if (gameVersion(object(), fs) == 4)
        return 4;
      for (auto &f : fs) {
        if (f.name[0] == '.' || f.name == "__MACOSX")
          continue;
        if (f.directory)
          folders.push_back(f.name);
      }
      if (folders.size() != 1)
        return 0; // Collections have no single engine.
      inner = inner.empty() ? folders.front() : join(inner, folders.front());
    }
  } catch (const std::exception &) {
    // A bad ZIP stays visible; opening it retains the existing error message.
  }
  return 0;
}
/* Finding a ZIP's engine means mounting it, and PhysFS reads the whole
 * central directory to do that: seconds per archive on a memory card, on
 * every launcher start and every return from a game. Remember the answer on
 * disk for as long as the file's size and times stay the same. Only a
 * recognized game is remembered, so a ZIP that failed to open is tried again. */
struct ZipScan {
  int64_t size, modified, changed;
  int version;
};
std::string zipScanFile;
std::map<std::string, ZipScan> zipScans;
bool zipScansLoaded = false, zipScansDirty = false;
void loadZipScans() {
  if (zipScansLoaded)
    return;
  zipScansLoaded = true;
  try {
    for (auto &line : split(readFile(zipScanFile, 262144), '\n')) {
      auto f = split(line, '\t');
      if (f.size() != 6)
        continue;
      int version = std::atoi(f[5].c_str());
      if (version >= 1 && version <= 4 && zipScans.size() < 512)
        zipScans[f[0] + "\t" + f[1]] = {std::atoll(f[2].c_str()), std::atoll(f[3].c_str()),
                                        std::atoll(f[4].c_str()), version};
    }
  } catch (const std::exception &) {
    zipScans.clear();
  }
}
int rememberedZipGameVersion(const Node &node) {
  struct stat st{};
  if (zipScanFile.empty() || !statPath(node.path(), st) ||
      node.path().find_first_of("\t\n") != std::string::npos ||
      node.inner.find_first_of("\t\n") != std::string::npos)
    return zipGameVersion(node);
  loadZipScans();
  auto id = node.path() + "\t" + node.inner;
  auto known = zipScans.find(id);
  if (known != zipScans.end() && known->second.size == int64_t(st.st_size) &&
      known->second.modified == int64_t(st.st_mtime) && known->second.changed == int64_t(st.st_ctime))
    return known->second.version;
  int version = zipGameVersion(node);
  if (known != zipScans.end())
    zipScans.erase(known);
  if (version) {
    if (zipScans.size() >= 512)
      zipScans.clear();
    zipScans[id] = {int64_t(st.st_size), int64_t(st.st_mtime), int64_t(st.st_ctime), version};
  }
  zipScansDirty = true;
  return version;
}
void saveZipScans() {
  if (!zipScansDirty || zipScanFile.empty())
    return;
  zipScansDirty = false;
  std::string text;
  for (auto &scan : zipScans)
    text += scan.first + "\t" + std::to_string(scan.second.size) + "\t" + std::to_string(scan.second.modified) +
            "\t" + std::to_string(scan.second.changed) + "\t" + std::to_string(scan.second.version) + "\n";
  try {
    atomicWrite(zipScanFile, text);
  } catch (const std::exception &) {
    // The scan still worked; it is only repeated next time.
  }
}
} // namespace
std::string Node::iniName() const {
  return gameIniName(files(), [&](const std::string &name) { return read(name); });
}
Json Node::ini() const {
  auto name = iniName();
  return name.empty() ? object() : parseGameIni(read(name));
}
std::vector<std::string> Node::rtps() const {
  auto values = ini();
  std::vector<std::string> out;
  for (auto k : {"rtp", "rtp1", "rtp2", "rtp3"}) {
    auto v = jsonString(values, k);
    if (!v.empty())
      out.push_back(v);
  }
  return out;
}
struct CachedZipGame {
  int64_t size, modified, changed;
  Entry entry;
};
std::map<std::string, CachedZipGame> zipGames;
std::map<std::string, CachedZipGame> zipSources;
bool game(const Node &node, Entry &result) {
  struct stat st{};
  auto id = node.path() + "!/" + node.inner;
  std::unique_ptr<Archive> archive;
  if (node.archive) {
    if (!statPath(node.path(), st)) return false;
    auto cached = zipGames.find(id);
    if (cached != zipGames.end() && cached->second.size == st.st_size &&
        cached->second.modified == st.st_mtime && cached->second.changed == st.st_ctime) {
      result = cached->second.entry;
      result.node = node;
      return true;
    }
    archive.reset(new Archive(node.path()));
  }
  auto fs = node.files();
  auto ini = gameIniName(fs, [&](const std::string &name) { return node.read(name); });
  if (ini.empty() && gameVersion(object(), fs) != 4)
    return false;
  auto v = ini.empty() ? object() : parseGameIni(node.read(ini));
  int version = gameVersion(v, fs);
  if (!version)
    return false;
  auto title = scrubUtf8(jsonString(v, "title", jsonString(v, "gametitle")));
  if (title.empty())
    title = basename(node.inner.empty() ? node.path() : node.inner);
  result = {title, Kind::Game, node, version};
  if (node.archive) {
    if (zipGames.size() >= 64) zipGames.clear();
    zipGames[id] = {st.st_size, st.st_mtime, st.st_ctime, result};
  }
  return true;
}
std::vector<Entry> entries(const Node &n) {
  std::vector<Entry> out;
  for (auto &f : n.files()) {
    if (f.name[0] == '.' || f.name == "__MACOSX")
      continue;
    if (f.directory) {
      auto child = n.child(f.name);
      Entry e;
      if (game(child, e))
        out.push_back(e);
      else
        out.push_back({f.name + "/", Kind::Folder, child});
    } else if (!n.archive && suffix(f.name, ".zip")) {
      auto child = n.child(f.name, true);
      out.push_back({f.name, Kind::Zip, child, rememberedZipGameVersion(child)});
    }
  }
  sortEntries(out);
  return out;
}
bool configurableGame(const Entry &entry, Entry &result) {
  if (entry.kind == Kind::Game) {
    result = entry;
    return true;
  }
  if (entry.kind != Kind::Zip)
    return false;
  Node node = entry.node;
  std::unique_ptr<Archive> archive;
  struct stat st{};
  auto id = node.path() + "!/" + node.inner;
  auto cached = zipSources.find(id);
  if (cached != zipSources.end() && statPath(node.path(), st) &&
      cached->second.size == st.st_size && cached->second.modified == st.st_mtime &&
      cached->second.changed == st.st_ctime) {
    result = cached->second.entry;
    return true;
  }
  cached = zipGames.find(id);
  if (cached == zipGames.end() || !statPath(node.path(), st) ||
      cached->second.size != st.st_size || cached->second.modified != st.st_mtime ||
      cached->second.changed != st.st_ctime)
    archive.reset(new Archive(node.path()));
  for (int depth = 0; depth <= 16; ++depth) {
    if (game(node, result)) {
      if (zipSources.size() >= 64) zipSources.clear();
      if (statPath(node.path(), st))
        zipSources[id] = {st.st_size, st.st_mtime, st.st_ctime, result};
      return true;
    }
    auto children = entries(node);
    if (children.size() != 1 || children.front().kind == Kind::Zip)
      return false;
    node = children.front().node;
  }
  return false;
}
Model::Model(Paths p, bool scan) : paths(std::move(p)) {
  paths.setup();
  zipScanFile = paths.at("zip-scan-cache.txt");
  stack.push_back({"", "", paths.at("games"), false});
  if (scan)
    refresh();
}
void Model::refresh() {
  zipGames.clear();
  zipSources.clear();
  error.clear();
  try {
    list = entries(current());
    if (stack.size() != 1) {
      applyNames();
      return;
    }
    std::set<std::string> known;
    for (auto &e : list)
      known.insert(e.node.identity(paths));
    auto nodes = references();
    for (auto &f : searchFolders())
      try {
        auto found = discover(f);
        nodes.insert(nodes.end(), found.begin(), found.end());
      } catch (const std::exception &) {
      }
    auto unpacked = unpackedGames();
    nodes.insert(nodes.end(), unpacked.begin(), unpacked.end());
    for (auto &n : nodes)
      try {
        Entry e;
        if (!known.count(n.identity(paths)) && game(n, e)) {
          list.push_back(e);
          known.insert(n.identity(paths));
        }
      } catch (const std::exception &) {
      }
    auto hide = hidden();
    if (!hide.empty())
      list.erase(std::remove_if(list.begin(), list.end(), [&](const Entry &e) {
                   return e.kind != Kind::Folder &&
                          hide.count(key(e.node.identity(paths)));
                 }),
                 list.end());
    saveZipScans();
    applyNames();
  } catch (const std::exception &e) {
    error = e.what();
    list.clear();
  }
}
void Model::applyNames() {
  auto file = paths.at("game-names.json");
  if (regular(file)) {
    try {
      auto names = json5pp::parse(readFile(file, 262144));
      for (auto &e : list) {
        if (e.kind == Kind::Folder)
          continue;
        auto i = names.as_object().find(key(e.node.identity(paths)));
        if (i != names.as_object().end() && i->second.is_string()) {
          auto name = trim(scrubUtf8(i->second.as_string()));
          if (!name.empty() && name.size() <= 512 &&
              name.find_first_of("\r\n\t") == std::string::npos &&
              name.find('\0') == std::string::npos)
            e.name = name;
        }
      }
    } catch (const std::exception &) {
      error = tr("Saved game names could not be read; original names are shown.");
    }
  }
  sortEntries(list);
}
void Model::setDisplayName(const Entry &e, const std::string &displayName) {
  if (e.kind == Kind::Folder)
    throw std::runtime_error(tr("Select a game or ZIP to rename."));
  auto name = trim(displayName);
  if (scrubUtf8(name) != name || name.size() > 512 ||
      name.find_first_of("\r\n\t") != std::string::npos ||
      name.find('\0') != std::string::npos)
    throw std::runtime_error(tr("Use a single-line game name of at most 128 characters."));
  size_t characters = 0;
  for (auto c : name) {
    if (uint8_t(c) < 32 || uint8_t(c) == 127)
      throw std::runtime_error(tr("Game names cannot contain control characters."));
    if ((uint8_t(c) & 0xc0) != 0x80)
      ++characters;
  }
  if (characters > 128)
    throw std::runtime_error(tr("Use at most 128 characters."));
  auto file = paths.at("game-names.json");
  auto names =
      regular(file) ? json5pp::parse(readFile(file, 262144)) : object();
  auto id = key(e.node.identity(paths));
  if (name.empty())
    names.as_object().erase(id);
  else
    names.as_object()[id] = name;
  auto data = names.stringify();
  if (data.size() > 262144)
    throw std::runtime_error("Saved game names are too large.");
  atomicWrite(file, data);
  // Rebuild labels from the existing nodes, without scanning the game library.
  for (auto &entry : list) {
    if (entry.node.identity(paths) != e.node.identity(paths))
      continue;
    Entry original;
    if (entry.kind == Kind::Game && game(entry.node, original))
      entry.name = original.name;
    else if (entry.kind == Kind::Zip)
      entry.name = basename(entry.node.path());
  }
  applyNames();
}
bool Model::back() {
  if (stack.size() == 1)
    return false;
  stack.pop_back();
  refresh();
  return true;
}
bool Model::enter(const Entry &e, Entry &selected) {
  if (e.kind == Kind::Game) {
    selected = e;
    return true;
  }
  if (e.kind == Kind::Zip && game(e.node, selected))
    return true;
  stack.push_back(e.node);
  refresh();
  return false;
}
std::vector<Node> Model::references() const {
  std::vector<Node> out;
  if (!regular(paths.at("library-paths.txt")))
    return out;
  std::istringstream s(readFile(paths.at("library-paths.txt"), 524288));
  std::string line;
  for (size_t count = 0; count < 2048 && std::getline(s, line); ++count) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto f = split(line, '\t');
    if (f.size() != 3 || (f[1] != "dir" && f[1] != "zip") ||
        !paths.external(f[0]))
      continue;
    try {
      out.push_back(Node::fromPath(paths, f[0], f[1] == "zip", f[2]));
    } catch (const std::exception &) {
    }
  }
  return out;
}
std::vector<std::string> Model::searchFolders() const {
  std::vector<std::string> out;
  if (!regular(paths.at("game-folders.txt")))
    return out;
  std::istringstream s(readFile(paths.at("game-folders.txt"), 65536));
  std::string line;
  for (size_t count = 0; count < 32 && std::getline(s, line); ++count) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (paths.external(line) &&
        std::find(out.begin(), out.end(), line) == out.end())
      out.push_back(line);
  }
  return out;
}
std::vector<Node> Model::discover(const std::string &p,
                                  ScanProgress progress) const {
  std::vector<Node> found;
  std::vector<std::pair<Node, int>> pending = {{Node::fromPath(paths, p), 0}};
  size_t visited = 0;
  while (!pending.empty()) {
    auto item = pending.back();
    pending.pop_back();
    if (item.second > 16)
      throw std::runtime_error(tr("Folder scan exceeds 16 levels; choose a closer folder"));
    if (++visited > 2048)
      throw std::runtime_error(tr("Folder scan exceeds 2048 folders; choose a closer folder"));
    if (progress)
      progress(visited, item.first.label(paths));
    try {
      Entry e;
      if (game(item.first, e)) {
        found.push_back(e.node);
        continue;
      }
      auto children = entries(item.first);
      for (auto i = children.rbegin(); i != children.rend(); ++i) {
        if (i->kind == Kind::Game)
          found.push_back(i->node);
        else
          pending.push_back({i->node, item.second + 1});
      }
    } catch (const std::exception &) {
      continue;
    }
  }
  return found;
}
std::vector<Node> Model::unpackedGames() const {
  std::vector<Node> out;
  auto cache = paths.at("cache");
  if (!directory(cache))
    return out;
  for (auto &f : directoryFiles(cache)) {
    if (!f.directory || f.name.compare(0, 4, "zip-") || suffix(f.name, ".partial"))
      continue;
    auto marker = join(join(cache, f.name), ".hardrpg-source");
    if (!regular(marker))
      continue;
    try {
      auto id = json5pp::parse(readFile(marker, 262144)).as_array().at(0).as_string();
      auto bang = id.find("!/");
      if (bang == std::string::npos || "zip-" + key(id) != f.name)
        continue;
      auto zip = id.substr(0, bang);
      // While its ZIP is there the game is listed through the ZIP.
      if (exists(paths.external(zip) ? zip : join(paths.at("games"), zip)))
        continue;
      out.push_back(Node{f.name, "", cache, false, id});
    } catch (const std::exception &) {
    }
  }
  return out;
}
std::set<std::string> Model::hidden() const {
  std::set<std::string> out;
  auto file = paths.at("hidden-games.txt");
  if (!regular(file))
    return out;
  std::istringstream s(readFile(file, 262144));
  std::string line;
  for (size_t count = 0; count < 4096 && std::getline(s, line); ++count)
    if (!line.empty())
      out.insert(line);
  return out;
}
void Model::saveHidden(const std::set<std::string> &keys) {
  std::string text;
  for (auto &k : keys)
    text += k + "\n";
  atomicWrite(paths.at("hidden-games.txt"), text);
}
// Drops a game's added-game reference, so a removed game is not re-added.
void Model::forget(const Entry &e) {
  auto id = e.node.identity(paths), zip = zipOf(e.node).identity(paths);
  auto old = references();
  auto kept = old;
  kept.erase(std::remove_if(kept.begin(), kept.end(), [&](const Node &n) {
               return n.identity(paths) == id || n.identity(paths) == zip;
             }),
             kept.end());
  if (kept.size() == old.size())
    return;
  std::string text;
  for (auto &n : kept)
    text += n.path() + "\t" + (n.archive ? "zip" : "dir") + "\t" + n.inner + "\n";
  atomicWrite(paths.at("library-paths.txt"), text);
}
void Model::hide(const Entry &e) {
  auto keys = hidden();
  keys.insert(key(e.node.identity(paths)));
  if (e.node.archive)
    keys.insert(key(zipOf(e.node).identity(paths)));
  if (keys.size() > 4096)
    throw std::runtime_error("Too many hidden games");
  forget(e);
  saveHidden(keys);
  stack.resize(1);
  refresh();
}
Model::Removal Model::removal(const Entry &e) const {
  Removal r;
  const Node &n = e.node;
  auto cache = paths.at("cache");
  if (!n.archive && n.root == cache) {
    r.unpacked = n.path();
    return r;
  }
  if (n.archive) {
    auto unpacked = paths.at("cache/zip-" + key(n.identity(paths)));
    if (directory(unpacked))
      r.unpacked = unpacked;
    // Only a ZIP that holds just this game may be deleted with it.
    Entry inner;
    Entry whole{basename(n.path()), Kind::Zip, zipOf(n)};
    if (regular(n.path()) && configurableGame(whole, inner) &&
        inner.node.identity(paths) == n.identity(paths))
      r.zip = n.path();
    return r;
  }
  // A game folder: never a library root, storage root or HardRPG's own data.
  auto p = n.path();
  std::vector<std::string> roots = {paths.at("games"), cache, paths.root,
                                    parent(paths.root)};
  for (auto &f : searchFolders())
    roots.push_back(f);
  for (auto &s : paths.storage)
    roots.push_back(s.substr(0, s.size() - 1));
  bool root = false;
  for (auto &x : roots)
    if (p == x || x.compare(0, p.size() + 1, p + "/") == 0)
      root = true;
  Entry check;
  if (!root && paths.external(p) && directory(p) && game(n, check))
    r.folder = p;
  return r;
}
void Model::deleteZip(const Entry &e) {
  auto r = removal(e);
  if (r.zip.empty() || r.unpacked.empty())
    throw std::runtime_error(tr("This game has no unpacked copy to play from"));
  if (unlink(r.zip.c_str()))
    throw std::runtime_error(tr("Cannot delete the ZIP"));
  forget(e);
  stack.resize(1);
  refresh();
}
void Model::remove(const Entry &e, bool files, bool unpacked) {
  auto r = removal(e);
  bool orphan = r.folder.empty() && r.zip.empty();
  // The game files of a game whose ZIP is gone are its unpacked copy.
  if (orphan && files)
    unpacked = true;
  if (files && !orphan) {
    if (!r.folder.empty())
      removeTree(r.folder);
    else if (unlink(r.zip.c_str()))
      throw std::runtime_error(tr("Cannot delete the ZIP"));
  }
  if (unpacked && !r.unpacked.empty())
    removeTree(r.unpacked);
  bool remains = (!orphan && !files) ||
                 (!r.unpacked.empty() && !unpacked) ||
                 (e.node.archive && r.zip.empty() && regular(e.node.path()));
  if (remains) {
    hide(e);
    return;
  }
  // Nothing is left: forget the game entirely, so a copy added later shows.
  forget(e);
  auto keys = hidden();
  if (keys.erase(key(e.node.identity(paths))) +
      keys.erase(key(zipOf(e.node).identity(paths))))
    saveHidden(keys);
  stack.resize(1);
  refresh();
}
size_t Model::addFolder(const std::string &p, ScanProgress progress) {
  auto found = discover(p, progress);
  if (found.empty())
    throw std::runtime_error(tr("No supported RPG Maker games found"));
  {
    // Adding games again shows any that were removed from the list.
    auto keys = hidden();
    auto before = keys.size();
    for (auto &n : found) {
      keys.erase(key(n.identity(paths)));
      keys.erase(key(zipOf(n).identity(paths)));
    }
    if (keys.size() != before)
      saveHidden(keys);
  }
  auto old = references();
  std::set<std::string> known;
  for (auto &n : old)
    known.insert(n.identity(paths));
  size_t added = 0;
  for (auto &n : found)
    if (known.insert(n.identity(paths)).second) {
      old.push_back(n);
      ++added;
    }
  if (old.size() > 2048)
    throw std::runtime_error(tr("Library has more than 2048 added games"));
  std::string text;
  for (auto &n : old)
    text +=
        n.path() + "\t" + (n.archive ? "zip" : "dir") + "\t" + n.inner + "\n";
  atomicWrite(paths.at("library-paths.txt"), text);
  stack.resize(1);
  refresh();
  return added;
}
void Model::saveSearchFolders(const std::vector<std::string> &list) {
  std::string text;
  for (auto &p : list)
    text += p + "\n";
  atomicWrite(paths.at("game-folders.txt"), text);
  stack.resize(1);
  refresh();
}
size_t Model::addSearchFolder(const std::string &p, ScanProgress progress) {
  if (!directory(p))
    throw std::runtime_error(tr("Choose an existing game folder"));
  auto found = discover(p, progress);
  {
    // Adding a folder again shows the games that were removed from the list.
    auto keys = hidden();
    auto before = keys.size();
    for (auto &n : found) {
      keys.erase(key(n.identity(paths)));
      keys.erase(key(zipOf(n).identity(paths)));
    }
    if (keys.size() != before)
      saveHidden(keys);
  }
  auto old = searchFolders();
  if (std::find(old.begin(), old.end(), p) == old.end())
    old.push_back(p);
  if (old.size() > 32)
    throw std::runtime_error(tr("Choose at most 32 extra game folders"));
  saveSearchFolders(old);
  return found.size();
}
void Model::removeSearchFolder(const std::string &p) {
  auto old = searchFolders();
  old.erase(std::remove(old.begin(), old.end(), p), old.end());
  saveSearchFolders(old);
}
ControlProfile Model::controls(const Entry &entry) const {
  auto path=paths.at("controls/"+key(entry.node.identity(paths))+".json");
  if(!regular(path))return {};
  return ControlProfile::parse(json5pp::parse(readFile(path,8192)));
}
void Model::saveControls(const Entry &entry,const ControlProfile &profile) {
  if(entry.kind!=Kind::Game)throw std::runtime_error(tr("Select a game to configure controls."));
  auto validated=ControlProfile::parse(profile.json());
  mkdirOne(paths.at("controls"));
  atomicWrite(paths.at("controls/"+key(entry.node.identity(paths))+".json"),validated.json().stringify());
}
Json Model::display() const {
  auto v = object();
  if (regular(paths.at("launcher-config.json"))) {
    v = json5pp::parse(readFile(paths.at("launcher-config.json"), 262144));
    v.as_object();
  }
  return v;
}
// Shrinking a picture uses Bilinear when Sharp is chosen: Sharp only helps when enlarging.
static Json downFilter(const Json &value) {
  return value.is_integer() && value.as_integer() == 5 ? Json(Json::integer_type(1)) : value;
}
void Model::changeDisplay(const std::string &name, const Json &value) {
  if (name == "performanceMetrics") {
    if (!value.is_integer() || value.as_integer() < 0 || value.as_integer() > 3)
      throw std::runtime_error("Invalid performance metrics mode");
  } else if ((name == "fixedAspectRatio" || name == "integerScalingActive" ||
              name == "fastForward")
          ? !value.is_boolean()
          : name != "smoothScaling" || filterIndex(value) < 0)
    throw std::runtime_error("Invalid display setting");
  auto v = display();
  v.as_object()[name] = value;
  if (name == "integerScalingActive")
    v.as_object()["integerScalingLastMile"] = !value.as_boolean();
  if (name == "smoothScaling")
    v.as_object()["smoothScalingDown"] = downFilter(value);
  atomicWrite(paths.at("launcher-config.json"), v.stringify());
}
Json Model::gameSettings(const Entry &entry) const {
  auto result = display();
  auto file = paths.at("config/" + key(entry.node.identity(paths)) + ".json");
  if (regular(file)) {
    auto saved = json5pp::parse(readFile(file, 262144));
    for (const auto &item : saved.as_object())
      result.as_object()[item.first] = item.second;
  }
  return result;
}
void Model::changeGameSetting(const Entry &entry, const std::string &name,
                              const Json &value, bool reset) {
  if (entry.kind != Kind::Game)
    throw std::runtime_error(tr("Select a game to change its settings."));
  bool boolean = name == "fixedAspectRatio" || name == "integerScalingActive" ||
                 name == "enableAudio" || name == "midiChorus" ||
                 name == "midiReverb" || name == "fastForward";
  bool integer = name == "smoothScaling" || name == "performanceMetrics";
  if ((!boolean && !integer) ||
      (!reset && (boolean ? !value.is_boolean()
                          : name == "smoothScaling"
                                ? filterIndex(value) < 0
                                : !value.is_integer() || value.as_integer() < 0 ||
                                      value.as_integer() > 3)))
    throw std::runtime_error("Invalid game setting.");
  auto file = paths.at("config/" + key(entry.node.identity(paths)) + ".json");
  auto saved =
      regular(file) ? json5pp::parse(readFile(file, 262144)) : object();
  auto &values = saved.as_object();
  const std::string paired = name == "integerScalingActive"
                                 ? "integerScalingLastMile"
                             : name == "smoothScaling" ? "smoothScalingDown"
                                                       : "";
  if (reset) {
    values.erase(name);
    if (!paired.empty())
      values.erase(paired);
  } else {
    values[name] = value;
    if (name == "integerScalingActive")
      values[paired] = !value.as_boolean();
    if (name == "smoothScaling")
      values[paired] = downFilter(value);
  }
  auto text = saved.stringify();
  if (text.size() > 262144)
    throw std::runtime_error("Game settings are too large.");
  atomicWrite(file, text);
}
Json Model::rtpPaths() const {
  auto v = object();
  if (!regular(paths.at("rtp-paths.txt")))
    return v;
  std::istringstream s(readFile(paths.at("rtp-paths.txt"), 524288));
  std::string line;
  while (std::getline(s, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    auto f = split(line, '\t');
    if (f.size() == 2 &&
        std::find(std::begin(packs), std::end(packs), f[0]) !=
            std::end(packs) &&
        paths.external(f[1]))
      v.as_object()[f[0]] = f[1];
  }
  return v;
}
void Model::setRtp(const std::string &name, const std::string &p) {
  clearRtpCache();
  auto pack = packName(name);
  if (std::find(std::begin(packs), std::end(packs), pack) == std::end(packs))
    throw std::runtime_error("Unknown RTP setting");
  Mount m;
  if (!p.empty() && (!paths.external(p) || !inspectRtp(p, pack, m)))
    throw std::runtime_error(tr("Choose a pack containing Graphics/Audio or System/Music folders."));
  auto v = rtpPaths();
  if (p.empty())
    v.as_object().erase(pack);
  else
    v.as_object()[pack] = p;
  std::string text;
  for (auto &i : v.as_object())
    text += i.first + "\t" + i.second.as_string() + "\n";
  atomicWrite(paths.at("rtp-paths.txt"), text);
}
bool Model::inspectRtp(const std::string &p, const std::string &name,
                       Mount &mount, int depth) const {
  struct stat st{};
  if (!statPath(p, st))
    return false;
  auto id = p + "\n" + name;
  auto i = rtpCache.find(id);
  if (i != rtpCache.end() && i->second.size == st.st_size &&
      i->second.modified == st.st_mtime) {
    if (i->second.found) {
      struct stat live{};
      if (statPath(i->second.mount.path, live)) {
        mount = i->second.mount;
        return true;
      }
    } else
      return false;
  }
  bool found = inspectRtpRaw(p, name, mount, depth);
  rtpCache[id] = {mount, st.st_size, st.st_mtime, found};
  return found;
}
bool Model::inspectRtpRaw(const std::string &p, const std::string &name,
                          Mount &mount, int depth) const {
  if (depth > 8)
    return false;
  bool zip = regular(p) && suffix(p, ".zip");
  if (!zip && !directory(p))
    return false;
  try {
    // Keep one ZIP mount for the whole inspection. Each Node::files call used
    // to reopen and rebuild its central directory, including Graphics/Audio.
    std::unique_ptr<Archive> archive;
    if (zip)
      archive.reset(new Archive(p));
    auto files = [&](const std::string &inner) {
      return zip ? archive->files(inner)
                 : directoryFiles(inner.empty() ? p : join(p, inner));
    };
    auto hasAssets = [&](const std::string &inner) {
      if (zip) {
        auto path = archive->full(inner);
        char **names = PHYSFS_enumerateFiles(path.c_str());
        if (!names)
          return false;
        bool found = false;
        for (char **n = names; *n && !found; ++n)
          found = relative(*n);
        PHYSFS_freeList(names);
        return found;
      }
      auto path = join(p, inner);
      DIR *dir = opendir(path.c_str());
      if (!dir)
        return false;
      bool found = false;
      while (auto *entry = readdir(dir)) {
        std::string n = entry->d_name;
        if (!relative(n))
          continue;
        struct stat st;
        if (statPath(join(path, n), st) && !S_ISLNK(st.st_mode) &&
            (S_ISREG(st.st_mode) || S_ISDIR(st.st_mode))) {
          found = true;
          break;
        }
      }
      closedir(dir);
      return found;
    };
    std::string inner;
    for (int step = 0; step < 9 - depth; ++step) {
      auto fs = files(inner);
      std::vector<std::string> folders;
      std::string graphics, audio, system;
      for (auto &f : fs)
        if (f.directory) {
          folders.push_back(f.name);
          if (lower(f.name) == "graphics")
            graphics = f.name;
          if (lower(f.name) == "audio" ||
              ((name == "2000" || name == "2003") && lower(f.name) == "music"))
            audio = f.name;
          if (lower(f.name) == "system")
            system = f.name;
        }
      if (name == "2000" || name == "2003")
        graphics = system;
      if (!graphics.empty() && !audio.empty() &&
          hasAssets(inner.empty() ? graphics : join(inner, graphics)) &&
          hasAssets(inner.empty() ? audio : join(inner, audio))) {
        mount = {zip ? p : (inner.empty() ? p : join(p, inner)),
                 zip ? inner : ""};
        return true;
      }
      if (!zip && inner.empty()) {
        bool candidates = false;
        for (auto &f : fs)
          if (alias(f.name, name, !f.directory)) {
            candidates = true;
            if (inspectRtp(join(p, f.name), name, mount, depth + 1))
              return true;
          }
        if (candidates)
          return false;
      }
      std::string chosen;
      for (auto &f : folders)
        if (alias(f, name)) {
          chosen = f;
          break;
        }
      if (chosen.empty() && folders.size() == 1)
        chosen = folders.front();
      if (chosen.empty()) {
        if (!zip)
          for (auto &f : fs)
            if (!f.directory && alias(f.name, name, true))
              return inspectRtp(
                  join(inner.empty() ? p : join(p, inner), f.name), name, mount,
                  depth + 1);
        return false;
      }
      inner = inner.empty() ? chosen : join(inner, chosen);
    }
  } catch (const std::exception &) {
  }
  return false;
}
std::vector<Mount> Model::resolveRtp(const Node &node) const {
  std::vector<Mount> out;
  auto choices = rtpPaths();
  std::set<std::string> seen;
  for (auto &n : node.rtps()) {
    auto name = packName(n);
    if (!seen.insert(name).second)
      continue;
    std::vector<std::string> candidates;
    auto c = choices.as_object().find(name);
    if (c != choices.as_object().end())
      candidates.push_back(c->second.as_string());
    else
      for (auto &a : aliases(name)) {
        candidates.push_back(paths.at("rtp/" + a));
        candidates.push_back(paths.at("rtp/" + a + ".zip"));
      }
    Mount m;
    bool found = false;
    for (auto &p : candidates)
      if (inspectRtp(p, name, m)) {
        found = true;
        break;
      }
    if (!found)
      throw MissingRtp(name);
    out.push_back(m);
  }
  return out;
}
std::string Model::rtpStatus(const std::string &name) const {
  auto choices = rtpPaths();
  auto c = choices.as_object().find(name);
  Mount m;
  if (c != choices.as_object().end())
    return inspectRtp(c->second.as_string(), name, m) ? "Detected" : "Missing";
  for (auto &a : aliases(name))
    for (auto &p : {paths.at("rtp/" + a), paths.at("rtp/" + a + ".zip")})
      if (inspectRtp(p, name, m))
        return "Detected";
  return "Missing";
}
std::string Model::prepareZip(const Node &node, Progress progress) {
  auto cache = "zip-" + key(node.identity(paths));
  auto dest = paths.at("cache/" + cache);
  struct stat s;
  if (!statPath(node.path(), s))
    throw std::runtime_error("ZIP is unavailable");
  auto fingerprint = '[' + Json(node.identity(paths)).stringify() + "," +
                     std::to_string(s.st_size) + "," +
                     std::to_string(s.st_mtime) + ']';
  auto marker = join(dest, ".hardrpg-source");
  if (regular(marker)) {
    if (readFile(marker, 262144) != fingerprint)
      throw std::runtime_error("ZIP changed. Back up saves and remove its "
                               "cache folder before preparing it again.");
    return cache;
  }
  if (exists(dest))
    throw std::runtime_error(
        "Existing ZIP cache is incomplete; move it aside first.");
  auto stage = dest + ".partial";
  removePartial(stage);
  mkdirOne(stage);
  try {
    if (progress) progress(0, 0);
    Archive archive(node.path());
    std::vector<ExtractFile> files;
    collect(node, "", files, progress);
    uint64_t total = 0, copied = 0;
    for (auto &f : files) {
      if (uint64_t(f.size) > std::numeric_limits<uint64_t>::max() - total)
        throw std::runtime_error("ZIP is too large");
      total += f.size;
    }
    if (progress)
      progress(copied, total);
    for (auto &f : files) {
      auto components = split(f.path, '/');
      std::string dir = stage;
      for (size_t i = 0; i + 1 < components.size(); ++i) {
        dir = join(dir, components[i]);
        mkdirOne(dir);
      }
      archive.extract(f.inner, join(stage, f.path), f.size, copied, total,
                      progress);
    }
    atomicWrite(join(stage, ".hardrpg-source"), fingerprint);
    if (rename(stage.c_str(), dest.c_str()))
      throw std::runtime_error("Cannot publish ZIP cache");
  } catch (...) {
    removePartial(stage);
    throw;
  }
  return cache;
}
Json Model::launch(const Entry &e, Progress progress, PreparationStatus status) {
  if (status) status(tr(e.node.archive ? "Preparing game ZIP" : "Starting game"));
  const auto *engine = engineFor(e.version);
  if (e.kind != Kind::Game || !engine)
    throw std::runtime_error("Choose a valid game");
  if (backendAvailable && !backendAvailable(*engine))
    throw std::runtime_error(trf("The {} runtime is missing from this package.", {engine->id}));
  auto mounts = e.version == 4 ? std::vector<Mount>{} : resolveRtp(e.node);
  // A game unpacked from a ZIP that is gone runs from its unpacked copy.
  bool unpacked = !e.node.archive && e.node.root == paths.at("cache");
  auto folder = e.node.archive ? prepareZip(e.node, progress)
                : unpacked || e.node.root == paths.at("games") ? e.node.relative
                                                               : e.node.path();
  auto rtps = array();
  for (auto &m : mounts) {
    auto mount = object();
    mount.as_object()["path"] = m.path;
    mount.as_object()["root"] = m.root;
    rtps.as_array().push_back(mount);
  }
  auto selection = object();
  auto &v = selection.as_object();
  v["source"] = e.node.archive || unpacked          ? "cache"
                : e.node.root == paths.at("games") ? "games"
                                                   : "external";
  v["path"] = folder;
  v["config"] = key(e.node.identity(paths));
  v["rgss"] = Json::integer_type(e.version);
  v["rtp"] = rtps;
  if (e.version == 4) {
    v.erase("rgss");
    v["backend"] = "easyrpg";
    v["schema"] = Json::integer_type(1);
    auto saveBase = paths.at("saves/easyrpg");
    mkdirOne(saveBase);
    auto save = join(saveBase, key(e.node.identity(paths)));
    mkdirOne(save);
    v["save"] = save;
    // EasyRPG receives absolute paths, including prepared ZIP projects.
    v["project"] = e.node.archive || unpacked ? paths.at("cache/" + folder) : e.node.path();
    // Seed existing RPG Maker saves without modifying the source project or
    // replacing a slot already owned by HardRPG.
    auto project = v["project"].as_string();
    for (const auto &file : directoryFiles(project)) {
      auto name = lower(file.name);
      if (file.directory || name.size() < 9 || name.compare(0, 4, "save") ||
          !suffix(name, ".lsd"))
        continue;
      auto slot = name.substr(4, name.size() - 8);
      if (slot.empty() ||
          slot.find_first_not_of("0123456789") != std::string::npos)
        continue;
      bool present = false;
      for (const auto &existing : directoryFiles(save))
        if (lower(existing.name) == name)
          present = true;
      if (!present)
        atomicWrite(join(save, file.name),
                    readFile(join(project, file.name), 16 * 1024 * 1024));
    }
    auto choices = rtpPaths();
    for (const auto &name : {"2000", "2003"}) {
      if (status) status(trf("Checking {} RTP", {name}));
      auto chosen = choices.as_object().find(name);
      std::string candidate = chosen == choices.as_object().end()
                                  ? paths.at(std::string("rtp/") + name)
                                  : chosen->second.as_string();
      Mount mount;
      if (inspectRtp(candidate, name, mount)) {
        // Reuse the safe extraction cache for ZIP RTPs.
        auto path = mount.path;
        if (regular(path)) {
          if (status) status(trf("Preparing {} RTP ZIP", {name}));
          auto node = Node::fromPath(paths, path, true, mount.root);
          path = paths.at("cache/" + prepareZip(node, progress));
        }
        auto pack = object();
        pack.as_object()["engine"] = name;
        pack.as_object()["path"] = path;
        rtps.as_array().push_back(pack);
      }
    }
    v["rtp"] = rtps;
  } else {
    auto ini = e.node.iniName();
    if (!ini.empty() && lower(ini) != "game.ini")
      v["execName"] = ini.substr(0, ini.size() - 4);
  }

  auto encoded = selection.stringify();
  if (encoded.size() > 4096)
    throw std::runtime_error(tr("Launch paths are too long; choose a closer game or RTP folder."));
  auto active = object();
  auto &a = active.as_object();
  a["name"] = e.name;
  a["engine"] = Json::integer_type(e.version);
  // Enough to find the game again from inside it (the in-game menu).
  a["kind"] = Json::integer_type(int(e.kind));
  a["relative"] = e.node.relative;
  a["inner"] = e.node.inner;
  a["root"] = e.node.root;
  a["archive"] = e.node.archive;
  a["alias"] = e.node.alias;
  a["config"] = key(e.node.identity(paths));
  atomicWrite(paths.at("active-game.json"), active.stringify());
  atomicWrite(paths.at("selection.json"), encoded);
  return selection;
}
bool Model::activeGame(Entry &out, const std::string &config) const {
  try {
    auto text = readFile(paths.at("active-game.json"), 65536);
    auto v = json5pp::parse(text);
    auto &o = v.as_object();
    Entry e;
    e.name = o.at("name").as_string();
    e.version = int(o.at("engine").as_integer());
    e.kind = Kind(o.at("kind").as_integer());
    e.node.relative = o.at("relative").as_string();
    e.node.inner = o.at("inner").as_string();
    e.node.root = o.at("root").as_string();
    e.node.archive = o.at("archive").as_boolean();
    e.node.alias = o.at("alias").as_string();
    // It must be the game that is running: its settings file is config.
    if (key(e.node.identity(paths)) != config || o.at("config").as_string() != config)
      return false;
    out = e;
    return true;
  } catch (const std::exception &) {
    return false;
  }
}
std::vector<std::string> Model::libraryRoots() const {
  std::vector<std::string> out{paths.at("games")};
  try {
    for (auto &folder : searchFolders())
      if (std::find(out.begin(), out.end(), folder) == out.end())
        out.push_back(folder);
    for (auto &node : references()) {
      auto folder = parent(node.path());
      bool covered = false;
      for (auto &root : out)
        if (folder == root || folder.find(root + "/") == 0)
          covered = true;
      if (!covered)
        out.push_back(folder);
    }
  } catch (const std::exception &) {
  }
  return out;
}
std::vector<std::string> Model::readError(const std::string &path) const {
  try {
    std::ifstream report(path, std::ios::binary);
    if (!report)
      return {};
    std::string body(32800, '\0');
    report.read(&body[0], body.size());
    if (report.bad())
      return {};
    body.resize(report.gcount());
    body = scrubUtf8(body);
    std::replace(body.begin(), body.end(), '\0', ' ');
    std::vector<std::string> out;
    for (auto &line : split(body, '\n')) {
      if (!line.empty() && line.back() == '\r')
        line.pop_back();
      if (line.empty()) {
        out.push_back("");
        continue;
      }
      size_t begin = 0, count = 0;
      for (size_t i = 0; i < line.size(); ++i)
        if ((uint8_t(line[i]) & 0xc0) != 0x80 && count++ == 65) {
          out.push_back(line.substr(begin, i - begin));
          begin = i;
          count = 1;
        }
      out.push_back(line.substr(begin));
    }
    return out;
  } catch (const std::exception &) {
    return {};
  }
}
// This app's title ID ("HARDRPG01", or a test build's); empty off the Vita.
static std::string ownTitle() {
#ifdef __vita__
  char title[16] = {};
  if (sceAppMgrAppParamGetString(0, 12, title, sizeof(title)) >= 0)
    return title;
#endif
  return "";
}
std::vector<std::string> Model::takeError(bool retained) {
  auto path = paths.at(retained ? "last-error.txt.prev" : "last-error.txt");
  if (!regular(path))
    return {};
  // Which app's game failed (see vitaErrorReportSave). A report from another
  // HardRPG app, or one too old to say, goes to the history without being
  // shown: it is not about anything played here.
  std::string owner;
  if (!retained) {
    try {
      owner = readFile(path + ".owner", 64);
    } catch (const std::exception &) {
    }
    while (!owner.empty() && std::isspace(static_cast<unsigned char>(owner.back())))
      owner.pop_back();
  }
  bool foreign = !retained && owner != ownTitle();
  activeErrorPath = path;
  auto lines = readError(path);
  if (lines.empty())
    return lines;
  // Archive before consuming the handoff; a failed archive never loses the
  // retained trace. Its filesystem mtime records when the engine wrote it.
  try {
    auto body = readFile(path, 1024 * 1024);
    struct stat st{};
    statPath(path, st);
    std::time_t time = st.st_mtime;
    std::tm *utc = std::gmtime(&time);
    char stamp[40]{};
    if (utc)
      std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", utc);
    auto folder = paths.at("errors");
    mkdirOne(folder);
    auto fingerprint = key(body + std::to_string(st.st_mtime));
    auto markerPath = paths.at("last-error-history.json");
    bool alreadyArchived = false;
    if (retained)
      try {
        auto marker = json5pp::parse(readFile(markerPath, 65536));
        auto name = jsonString(marker, "report");
        if (jsonString(marker, "fingerprint") == fingerprint &&
            relative(name) && name.find('/') == std::string::npos &&
            regular(join(folder, name))) {
          activeErrorPath = join(folder, name);
          alreadyArchived = true;
        }
      } catch (const std::exception &) {
      }
    auto id = fingerprint;
    if (!alreadyArchived) {
      // Separate identical crashes even if the filesystem has one-second mtime.
      for (unsigned i = 1; regular(join(folder, id + ".json")); ++i)
        id = fingerprint + "-" + std::to_string(i);
      auto target = join(folder, id + ".txt"),
           meta = join(folder, id + ".json");
      if (!regular(meta)) {
        std::string title = "Earlier game error";
        if (!retained)
          try {
            title = jsonString(
                json5pp::parse(readFile(paths.at("active-game.json"), 65536)),
                "name", title);
          } catch (const std::exception &) {
          }
        auto v = object();
        v.as_object()["game"] = title;
        v.as_object()["timestamp"] = std::string(stamp);
        v.as_object()["summary"] = lines.front();
        if (!owner.empty())
          v.as_object()["app"] = owner;
        v.as_object()["release"] =
            retained ? "Unknown (older report)" : HARDRPG_VERSION;
#ifdef MKXPZ_GIT_HASH
        v.as_object()["build"] =
            retained ? "Unknown (older report)" : MKXPZ_GIT_HASH;
#endif
        atomicWrite(target, body);
        atomicWrite(meta, v.stringify());
      }
      activeErrorPath = target;
      auto marker = object();
      marker.as_object()["fingerprint"] = fingerprint;
      marker.as_object()["report"] = basename(target);
      atomicWrite(markerPath, marker.stringify());
    }
  } catch (const std::exception &e) {
    error = trf("Error history could not be saved: {}", {e.what()});
  }
  if (!retained) {
    unlink((path + ".owner").c_str());
    auto previous = path + ".prev";
    if (regular(previous) && unlink(previous.c_str()))
      return foreign ? std::vector<std::string>{} : lines;
    if (rename(path.c_str(), previous.c_str()))
      return foreign ? std::vector<std::string>{} : lines;
  }
  return foreign ? std::vector<std::string>{} : lines;
}
std::vector<ErrorRecord> Model::errorHistory() const {
  std::vector<ErrorRecord> out;
  auto folder = paths.at("errors");
  if (directory(folder))
    for (auto &f : directoryFiles(folder)) {
      if (f.directory || !suffix(f.name, ".json"))
        continue;
      try {
        auto meta = json5pp::parse(readFile(join(folder, f.name), 65536));
        auto report =
            join(folder, f.name.substr(0, f.name.size() - 5) + ".txt");
        if (regular(report))
          out.push_back({report, jsonString(meta, "game"),
                         jsonString(meta, "timestamp"),
                         jsonString(meta, "summary")});
      } catch (const std::exception &) {
      }
    }
  // Existing manual exports from earlier versions remain browsable in place.
  folder = paths.at("reports");
  if (directory(folder))
    for (auto &f : directoryFiles(folder)) {
      if (f.directory || !suffix(f.name, ".txt"))
        continue;
      auto path = join(folder, f.name);
      std::ifstream exported(path);
      std::string line;
      bool archived = false;
      for (int i = 0; i < 14 && std::getline(exported, line); ++i)
        if (line.find("History entry: ") == 0)
          archived = true;
      if (archived)
        continue;
      struct stat st{};
      statPath(path, st);
      std::time_t time = st.st_mtime;
      char stamp[40]{};
      if (auto *utc = std::gmtime(&time))
        std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S UTC", utc);
      out.push_back({path, "Earlier exported report", stamp, f.name});
    }
  std::sort(out.begin(), out.end(),
            [](const ErrorRecord &a, const ErrorRecord &b) {
              return a.timestamp == b.timestamp ? a.path > b.path
                                                : a.timestamp > b.timestamp;
            });
  return out;
}
std::string Model::exportError() const {
  auto source = activeErrorPath.empty() ? paths.at("last-error.txt.prev")
                                        : activeErrorPath;
  if (!regular(source))
    throw std::runtime_error(tr("No retained game error to export."));
  // Copy the complete retained trace, rather than the wrapped on-screen prefix.
  auto body = readFile(source, 1024 * 1024);
  std::string header =
      "HardRPG " HARDRPG_VERSION " error report\nLauncher: native\n";
#ifdef MKXPZ_VERSION
  header += "Engine: mkxp-z " MKXPZ_VERSION "\n";
#endif
#ifdef MKXPZ_GIT_HASH
  header += "Exporter build: " MKXPZ_GIT_HASH "\n";
#endif
  if (source.find(paths.at("errors/")) == 0) {
    try {
      auto meta = json5pp::parse(
          readFile(source.substr(0, source.size() - 4) + ".json", 65536));
      header += "Game: " + jsonString(meta, "game") +
                "\nCaptured UTC: " + jsonString(meta, "timestamp") + "\n";
      header += "History entry: " + basename(source) + "\n";
      header +=
          "Captured release: " + jsonString(meta, "release", "Unknown") + "\n";
      header +=
          "Captured build: " + jsonString(meta, "build", "Unknown") + "\n";
    } catch (const std::exception &) {
    }
  }
  header += "\n";
  auto data = header + body;
  auto folder = paths.at("reports");
  mkdirOne(folder);
  // Exclusive creation prevents repeated exports from overwriting reports.
  for (unsigned int index = 1; index <= 100000; ++index) {
    char name[32];
    std::snprintf(name, sizeof(name), "/error-%06u.txt", index);
    auto destination = folder + name;
    int fd = open(destination.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
    if (fd < 0) {
      if (errno == EEXIST)
        continue;
      throw std::runtime_error(tr("Cannot create exported report. Check free space."));
    }
    FILE *file = fdopen(fd, "wb");
    if (!file) {
      close(fd);
      unlink(destination.c_str());
      throw std::runtime_error("Cannot open exported report.");
    }
    bool ok = std::fwrite(data.data(), 1, data.size(), file) == data.size();
    if (std::fclose(file))
      ok = false;
    if (!ok) {
      unlink(destination.c_str());
      throw std::runtime_error(tr("Cannot write exported report. Check free space."));
    }
    return destination;
  }
  throw std::runtime_error(tr("Report folder is full. Move older exported reports."));
}
Browser::Browser(const Paths &p, bool includeZips)
    : paths(p), zips(includeZips) {
  for (auto &r : p.storage)
    if (directory(r)) {
      path = r;
      break;
    }
  refresh();
}
void Browser::refresh() {
  error.clear();
  list.clear();
  try {
    if (path.empty()) {
      for (auto &r : paths.storage)
        if (directory(r))
          list.push_back({r, true, 0});
      return;
    }
    for (auto &f : directoryFiles(path))
      if (f.name[0] != '.' && (f.directory || (zips && suffix(f.name, ".zip"))))
        list.push_back(f);
    std::sort(list.begin(), list.end(), [](const File &a, const File &b) {
      return lower(a.name) < lower(b.name);
    });
  } catch (const std::exception &e) {
    error = e.what();
    list.clear();
  }
}
std::string Browser::selectedPath(size_t i) const {
  return path.empty() ? list.at(i).name : join(path, list.at(i).name);
}
void Browser::enter(const std::string &p) {
  if (std::find(paths.storage.begin(), paths.storage.end(), p) ==
          paths.storage.end() &&
      !paths.external(p))
    throw std::runtime_error("Invalid storage folder");
  path = p;
  refresh();
}
bool Browser::back() {
  if (path.empty())
    return false;
  if (std::find(paths.storage.begin(), paths.storage.end(), path) !=
      paths.storage.end())
    path.clear();
  else {
    path = parent(path);
    for (auto &r : paths.storage)
      if (path + "/" == r)
        path = r;
  }
  refresh();
  return true;
}
// activity.txt: one "key last seconds" line per game, plus "running key start"
// while a game is out. Times are Unix seconds.
namespace {
struct ActivityFile {
  std::map<std::string, Activity> games;
  std::string running;
  int64_t started = 0;
};
ActivityFile readActivity(const Paths &paths) {
  ActivityFile out;
  auto file = paths.at("activity.txt");
  if (!regular(file))
    return out;
  std::istringstream s(readFile(file, 524288));
  std::string line;
  for (size_t count = 0; count < 8192 && std::getline(s, line); ++count) {
    std::istringstream row(line);
    std::string first, second;
    long long a = 0, b = 0;
    if (!(row >> first))
      continue;
    if (first == "running") {
      if (row >> second >> a) {
        out.running = second;
        out.started = a;
      }
    } else if (row >> a >> b)
      out.games[first] = {a, std::max(0LL, b)};
  }
  return out;
}
void writeActivity(const Paths &paths, const ActivityFile &a) {
  std::string text;
  for (auto &g : a.games)
    text += g.first + " " + std::to_string(g.second.last) + " " + std::to_string(g.second.seconds) + "\n";
  if (!a.running.empty())
    text += "running " + a.running + " " + std::to_string(a.started) + "\n";
  atomicWrite(paths.at("activity.txt"), text);
}
} // namespace
std::map<std::string, Activity> Model::activity() const { return readActivity(paths).games; }
void Model::recordLaunch(const std::string &id) {
  auto a = readActivity(paths);
  int64_t now = std::time(nullptr);
  a.games[id].last = now;
  a.running = id;
  a.started = now;
  writeActivity(paths, a);
}
void Model::settleActivity(bool returning) {
  auto a = readActivity(paths);
  if (a.running.empty())
    return;
  // Returning from a game counts the session. A fresh boot means the game
  // was closed from the home screen, so its length is unknown and dropped.
  int64_t now = std::time(nullptr), spent = now - a.started;
  if (returning && spent > 0 && spent < 24 * 3600)
    a.games[a.running].seconds += spent;
  a.running.clear();
  writeActivity(paths, a);
}
std::set<std::string> Model::favourites() const {
  std::set<std::string> out;
  auto file = paths.at("favourite-games.txt");
  if (!regular(file))
    return out;
  std::istringstream s(readFile(file, 262144));
  std::string line;
  for (size_t count = 0; count < 4096 && std::getline(s, line); ++count)
    if (!line.empty())
      out.insert(line);
  return out;
}
void Model::setFavourite(const std::string &id, bool on) {
  auto keys = favourites();
  if (on)
    keys.insert(id);
  else
    keys.erase(id);
  std::string text;
  for (auto &k : keys)
    text += k + "\n";
  atomicWrite(paths.at("favourite-games.txt"), text);
}
} // namespace hardrpg
