#include "model.h"
#include "release.h"
#include <algorithm>
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
    throw std::runtime_error("Cannot create folder: " + p);
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
    throw std::runtime_error("Cannot read folder: " + p);
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
class Archive {
  std::string source;

public:
  explicit Archive(const std::string &p) : source(p) {
    if (!PHYSFS_isInit() && !PHYSFS_init("hardrpg"))
      fail();
    if (!PHYSFS_mount(p.c_str(), "__hardrpg_native__", 0))
      fail();
  }
  ~Archive() { PHYSFS_unmount(source.c_str()); }
  Archive(const Archive &) = delete;
  static void fail() {
    auto *e = PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode());
    throw std::runtime_error(std::string("ZIP error: ") + (e ? e : "unknown"));
  }
  std::string full(const std::string &p) const {
    if (!relative(p, true))
      throw std::runtime_error("Invalid ZIP path");
    return join("__hardrpg_native__", p);
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
      throw std::runtime_error("Truncated ZIP file");
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
          throw std::runtime_error(
              "Cannot write ZIP cache (storage may be full)");
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
struct ExtractFile {
  std::string inner, path;
  int64_t size;
};
void collect(const Node &node, const std::string &prefix,
             std::vector<ExtractFile> &out, int depth = 0) {
  if (depth > 64)
    throw std::runtime_error("ZIP folder nesting exceeds 64 levels");
  for (auto &f : node.files()) {
    if (!relative(f.name))
      throw std::runtime_error("Unsafe ZIP filename");
    if (f.name == "__MACOSX")
      continue;
    auto dest = prefix.empty() ? f.name : join(prefix, f.name);
    if (f.directory)
      collect(node.child(f.name), dest, out, depth + 1);
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

const char *const packs[3] = {"Standard", "RPGVX", "RPGVXAce"};
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
  for (auto n : {"games", "rtp", "config", "cache"})
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
    : std::runtime_error("Missing " + packLabel(p) +
                         " RTP. Choose its folder or ZIP in Settings."),
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
    else if (section == "game") {
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
int zipGameVersion(const Node &node) {
  try {
    // Inspect metadata in one read-only mount, including single-folder
    // wrappers. Keep the original ZIP node, display name and save/cache
    // identity intact.
    Archive archive(node.path());
    auto inner = node.inner;
    for (int depth = 0; depth <= 16; ++depth) {
      auto fs = archive.files(inner);
      std::vector<std::string> folders;
      for (auto &f : fs) {
        if (f.name[0] == '.' || f.name == "__MACOSX")
          continue;
        if (!f.directory && lower(f.name) == "game.ini")
          return gameVersion(parseGameIni(archive.read(
                                 inner.empty() ? f.name : join(inner, f.name))),
                             fs);
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
} // namespace
Json Node::ini() const {
  for (auto &f : files())
    if (lower(f.name) == "game.ini" && !f.directory)
      return parseGameIni(read(f.name));
  return object();
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
bool game(const Node &node, Entry &result) {
  auto fs = node.files();
  bool hasIni = false;
  for (auto &f : fs)
    if (lower(f.name) == "game.ini" && !f.directory)
      hasIni = true;
  if (!hasIni)
    return false;
  auto v = node.ini();
  int version = gameVersion(v, fs);
  if (!version)
    return false;
  auto title = scrubUtf8(jsonString(v, "title"));
  if (title.empty())
    title = basename(node.inner.empty() ? node.path() : node.inner);
  result = {title, Kind::Game, node, version};
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
      out.push_back({f.name, Kind::Zip, child, zipGameVersion(child)});
    }
  }
  sortEntries(out);
  return out;
}
Model::Model(Paths p) : paths(std::move(p)) {
  paths.setup();
  stack.push_back({"", "", paths.at("games"), false});
  refresh();
}
void Model::refresh() {
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
    for (auto &n : nodes)
      try {
        Entry e;
        if (!known.count(n.identity(paths)) && game(n, e)) {
          list.push_back(e);
          known.insert(n.identity(paths));
        }
      } catch (const std::exception &) {
      }
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
      error = "Saved game names could not be read; original names are shown.";
    }
  }
  sortEntries(list);
}
void Model::setDisplayName(const Entry &e, const std::string &displayName) {
  if (e.kind == Kind::Folder)
    throw std::runtime_error("Select a game or ZIP to rename.");
  auto name = trim(displayName);
  if (scrubUtf8(name) != name || name.size() > 512 ||
      name.find_first_of("\r\n\t") != std::string::npos ||
      name.find('\0') != std::string::npos)
    throw std::runtime_error(
        "Use a single-line game name of at most 128 characters.");
  size_t characters = 0;
  for (auto c : name) {
    if (uint8_t(c) < 32 || uint8_t(c) == 127)
      throw std::runtime_error("Game names cannot contain control characters.");
    if ((uint8_t(c) & 0xc0) != 0x80)
      ++characters;
  }
  if (characters > 128)
    throw std::runtime_error("Use at most 128 characters.");
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
      throw std::runtime_error(
          "Folder scan exceeds 16 levels; choose a closer folder");
    if (++visited > 2048)
      throw std::runtime_error(
          "Folder scan exceeds 2048 folders; choose a closer folder");
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
size_t Model::addFolder(const std::string &p, ScanProgress progress) {
  auto found = discover(p, progress);
  if (found.empty())
    throw std::runtime_error("No RPG Maker XP, VX or VX Ace games found");
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
    throw std::runtime_error("Library has more than 2048 added games");
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
    throw std::runtime_error("Choose an existing game folder");
  auto found = discover(p, progress);
  auto old = searchFolders();
  if (std::find(old.begin(), old.end(), p) == old.end())
    old.push_back(p);
  if (old.size() > 32)
    throw std::runtime_error("Choose at most 32 extra game folders");
  saveSearchFolders(old);
  return found.size();
}
void Model::removeSearchFolder(const std::string &p) {
  auto old = searchFolders();
  old.erase(std::remove(old.begin(), old.end(), p), old.end());
  saveSearchFolders(old);
}
Json Model::display() const {
  auto v = object();
  if (regular(paths.at("launcher-config.json"))) {
    v = json5pp::parse(readFile(paths.at("launcher-config.json"), 262144));
    v.as_object();
  }
  return v;
}
void Model::changeDisplay(const std::string &name, const Json &value) {
  if ((name == "fixedAspectRatio" || name == "integerScalingActive")
          ? !value.is_boolean()
          : name != "smoothScaling" || !value.is_integer() ||
                value.as_integer() < 0 || value.as_integer() > 1)
    throw std::runtime_error("Invalid display setting");
  auto v = display();
  v.as_object()[name] = value;
  if (name == "integerScalingActive")
    v.as_object()["integerScalingLastMile"] = !value.as_boolean();
  if (name == "smoothScaling")
    v.as_object()["smoothScalingDown"] = value;
  atomicWrite(paths.at("launcher-config.json"), v.stringify());
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
    throw std::runtime_error(
        "Choose a pack containing Graphics and Audio folders.");
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
      std::string graphics, audio;
      for (auto &f : fs)
        if (f.directory) {
          folders.push_back(f.name);
          if (lower(f.name) == "graphics")
            graphics = f.name;
          if (lower(f.name) == "audio")
            audio = f.name;
        }
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
    std::vector<ExtractFile> files;
    collect(node, "", files);
    uint64_t total = 0, copied = 0;
    for (auto &f : files) {
      if (uint64_t(f.size) > std::numeric_limits<uint64_t>::max() - total)
        throw std::runtime_error("ZIP is too large");
      total += f.size;
    }
    if (progress)
      progress(copied, total);
    Archive archive(node.path());
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
Json Model::launch(const Entry &e, Progress progress) {
  if (e.kind != Kind::Game || e.version < 1 || e.version > 3)
    throw std::runtime_error("Choose a valid game");
  auto mounts = resolveRtp(e.node);
  auto folder = e.node.archive ? prepareZip(e.node, progress)
                : e.node.root == paths.at("games") ? e.node.relative
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
  v["source"] = e.node.archive                     ? "cache"
                : e.node.root == paths.at("games") ? "games"
                                                   : "external";
  v["path"] = folder;
  v["config"] = key(e.node.identity(paths));
  v["rgss"] = Json::integer_type(e.version);
  v["rtp"] = rtps;
  auto encoded = selection.stringify();
  if (encoded.size() > 4096)
    throw std::runtime_error(
        "Launch paths are too long; choose a closer game or RTP folder.");
  auto active = object();
  active.as_object()["name"] = e.name;
  active.as_object()["engine"] = Json::integer_type(e.version);
  atomicWrite(paths.at("active-game.json"), active.stringify());
  atomicWrite(paths.at("selection.json"), encoded);
  return selection;
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
std::vector<std::string> Model::takeError(bool retained) {
  auto path = paths.at(retained ? "last-error.txt.prev" : "last-error.txt");
  if (!regular(path))
    return {};
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
    error = std::string("Error history could not be saved: ") + e.what();
  }
  if (!retained) {
    auto previous = path + ".prev";
    if (regular(previous) && unlink(previous.c_str()))
      return lines;
    if (rename(path.c_str(), previous.c_str()))
      return lines;
  }
  return lines;
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
    throw std::runtime_error("No retained game error to export.");
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
      throw std::runtime_error(
          "Cannot create exported report. Check free space.");
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
      throw std::runtime_error(
          "Cannot write exported report. Check free space.");
    }
    return destination;
  }
  throw std::runtime_error(
      "Report folder is full. Move older exported reports.");
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
} // namespace hardrpg
