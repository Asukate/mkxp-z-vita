#include "save_backups.h"
#include "i18n.h"
#include "model.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <dirent.h>
#include <map>
#include <sys/stat.h>
#include <unistd.h>

namespace hardrpg {
namespace {
constexpr size_t maxSave = 32 * 1024 * 1024;
std::string join(const std::string &a, const std::string &b) {
  return a + (a.empty() || a.back() == '/' ? "" : "/") + b;
}
std::string parentOf(const std::string &p) {
  auto i = p.find_last_of('/');
  return i == std::string::npos ? std::string() : p.substr(0, i);
}
std::string baseOf(const std::string &p) { return p.substr(p.find_last_of('/') + 1); }
bool statOf(const std::string &p, struct stat &s) { return !stat(p.c_str(), &s); }
bool isFile(const std::string &p) {
  struct stat s;
  return statOf(p, s) && S_ISREG(s.st_mode);
}
bool isDir(const std::string &p) {
  struct stat s;
  return statOf(p, s) && S_ISDIR(s.st_mode);
}
void makeDir(const std::string &p) {
  if (!isDir(p) && mkdir(p.c_str(), 0777) && !isDir(p))
    throw std::runtime_error(trf("Cannot create folder: {}", {p}));
}
std::vector<std::string> names(const std::string &p, bool dirs) {
  std::vector<std::string> out;
  DIR *d = opendir(p.c_str());
  if (!d)
    return out;
  while (auto *e = readdir(d)) {
    std::string n = e->d_name;
    if (n == "." || n == "..")
      continue;
    if (dirs ? isDir(join(p, n)) : isFile(join(p, n)))
      out.push_back(n);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}
void removeAll(const std::string &p) {
  if (isDir(p)) {
    DIR *d = opendir(p.c_str());
    std::vector<std::string> inside;
    if (d) {
      while (auto *e = readdir(d)) {
        std::string n = e->d_name;
        if (n != "." && n != "..")
          inside.push_back(n);
      }
      closedir(d);
    }
    for (auto &n : inside)
      removeAll(join(p, n));
    rmdir(p.c_str());
  } else
    unlink(p.c_str());
}
std::string readText(const std::string &p, size_t limit = 65536) {
  try {
    return isFile(p) ? readFile(p, limit) : std::string();
  } catch (const std::exception &) {
    return {};
  }
}
Json parseObject(const std::string &text) {
  try {
    if (!text.empty()) {
      auto v = json5pp::parse(text);
      v.as_object();
      return v;
    }
  } catch (const std::exception &) {
  }
  return Json(std::initializer_list<Json::pair_type>{});
}
std::string textOf(const Json &v, const char *name) {
  auto &o = v.as_object();
  auto i = o.find(name);
  return i != o.end() && i->second.is_string() ? i->second.as_string() : std::string();
}
int64_t numberOf(const Json &v, const char *name, int64_t fallback = 0) {
  auto &o = v.as_object();
  auto i = o.find(name);
  if (i == o.end())
    return fallback;
  if (i->second.is_integer())
    return i->second.as_integer();
  if (i->second.is_number())
    return int64_t(i->second.as_number());
  return fallback;
}
bool flagOf(const Json &v, const char *name, bool fallback) {
  auto &o = v.as_object();
  auto i = o.find(name);
  return i != o.end() && i->second.is_boolean() ? i->second.as_boolean() : fallback;
}
// A complete copy under a temporary name first, so a stop part way leaves
// no half file under the real name.
void writeWhole(const std::string &p, const std::string &data) {
  auto temp = p + ".hardrpg-tmp";
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    throw std::runtime_error(trf("Cannot write {}", {baseOf(p)}));
  bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
  if (std::fclose(f))
    ok = false;
  if (!ok) {
    unlink(temp.c_str());
    throw std::runtime_error(trf("Cannot write {}", {baseOf(p)}));
  }
  // The Vita's rename does not replace a file: the old one steps aside for
  // the moment between the two renames (safe_save.rb puts it back if a stop
  // falls there).
  auto old = p + ".hardrpg-old";
  bool had = isFile(p);
  if (had) {
    unlink(old.c_str());
    if (std::rename(p.c_str(), old.c_str())) {
      unlink(temp.c_str());
      throw std::runtime_error(trf("Cannot replace {}", {baseOf(p)}));
    }
  }
  if (std::rename(temp.c_str(), p.c_str())) {
    if (had)
      std::rename(old.c_str(), p.c_str());
    unlink(temp.c_str());
    throw std::runtime_error(trf("Cannot replace {}", {baseOf(p)}));
  }
  if (had)
    unlink(old.c_str());
}
std::string stamp(int64_t when) {
  time_t t = time_t(when);
  struct tm local {};
  localtime_r(&t, &local);
  char text[32];
  std::snprintf(text, sizeof text, "%04d%02d%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                local.tm_hour, local.tm_min, local.tm_sec);
  return text;
}
// A kept version's file name: YYYYMMDD-HHMMSS[-n].<ext>, with its .json.
bool versionName(const std::string &n) {
  if (n.size() < 16 || n.compare(n.size() - 5, 5, ".json") == 0 || n.find(".hardrpg-") != std::string::npos)
    return false;
  for (int i = 0; i < 15; ++i)
    if (i == 8 ? n[i] != '-' : !std::isdigit(static_cast<unsigned char>(n[i])))
      return false;
  return n[15] == '.' || n[15] == '-';
}
int64_t nameTime(const std::string &n) {
  struct tm t {};
  auto num = [&](size_t at, size_t len) { return std::atoi(n.substr(at, len).c_str()); };
  t.tm_year = num(0, 4) - 1900;
  t.tm_mon = num(4, 2) - 1;
  t.tm_mday = num(6, 2);
  t.tm_hour = num(9, 2);
  t.tm_min = num(11, 2);
  t.tm_sec = num(13, 2);
  t.tm_isdst = -1;
  return int64_t(mktime(&t));
}
std::string jsonOf(const std::string &versionFile) {
  auto dot = versionFile.find('.', versionFile.find_last_of('/') + 1);
  return versionFile.substr(0, dot) + ".json";
}
std::string extensionOf(const std::string &name) {
  auto dot = name.find_last_of('.');
  return dot == std::string::npos ? std::string() : name.substr(dot);
}
SaveVersion readVersion(const std::string &dir, const std::string &name) {
  SaveVersion v;
  v.file = join(dir, name);
  auto info = parseObject(readText(jsonOf(v.file)));
  v.why = textOf(info, "why");
  v.mapName = textOf(info, "map_name");
  v.leader = textOf(info, "leader");
  v.level = int(numberOf(info, "level"));
  v.playtime = numberOf(info, "playtime_s");
  v.saved = numberOf(info, "saved", nameTime(name));
  v.kept = numberOf(info, "kept", v.saved);
  struct stat s;
  v.size = statOf(v.file, s) ? int64_t(s.st_size) : numberOf(info, "size");
  return v;
}
std::vector<SaveVersion> versionsIn(const std::string &dir) {
  std::vector<SaveVersion> out;
  for (auto &n : names(dir, false))
    if (versionName(n))
      out.push_back(readVersion(dir, n));
  std::sort(out.begin(), out.end(), [](const SaveVersion &a, const SaveVersion &b) {
    return a.saved != b.saved ? a.saved > b.saved : a.file > b.file;
  });
  return out;
}
void dropVersion(const SaveVersion &v) {
  unlink(v.file.c_str());
  unlink(jsonOf(v.file).c_str());
}
// The description of a file as the game last saved it (current.json), when
// it is still that file.
Json described(const std::string &dir, int64_t size, int64_t saved) {
  auto info = parseObject(readText(join(dir, "current.json")));
  if (numberOf(info, "size", -1) != size || std::llabs(numberOf(info, "saved") - saved) > 2)
    return parseObject("");
  return info;
}
bool sameBytes(const std::string &a, const std::string &b) {
  try {
    return readFile(a, maxSave) == readFile(b, maxSave);
  } catch (const std::exception &) {
    return false;
  }
}
// The version already holds this file: same time and size, or same bytes.
bool held(const std::vector<SaveVersion> &list, const std::string &file, int64_t size, int64_t saved) {
  for (auto &v : list)
    if (v.size == size && v.saved == saved)
      return true;
  // A restored file has the restore's time, so compare the bytes too.
  for (auto &v : list)
    if (v.size == size && sameBytes(v.file, file))
      return true;
  return false;
}
bool slotNumber(const std::string &name, int &number) {
  auto l = lower(name);
  auto at = l.find("save");
  if (at == std::string::npos)
    return false;
  size_t i = at + 4;
  while (i < l.size() && !std::isdigit(static_cast<unsigned char>(l[i])) && l[i] != '.')
    ++i;
  if (i >= l.size() || !std::isdigit(static_cast<unsigned char>(l[i])))
    return false;
  number = std::atoi(l.c_str() + i);
  return true;
}
// Save files the game keeps where the launcher can find them.
bool rgssSave(const std::string &name) {
  auto l = lower(name);
  if (l.compare(0, 4, "save") != 0)
    return false;
  for (auto ext : {".rxdata", ".rvdata", ".rvdata2"})
    if (l.size() > std::string(ext).size() && l.compare(l.size() - std::string(ext).size(), std::string::npos, ext) == 0)
      return true;
  return false;
}
bool lcfSave(const std::string &name) {
  auto l = lower(name);
  return l.size() > 8 && l.compare(0, 4, "save") == 0 && l.compare(l.size() - 4, 4, ".lsd") == 0;
}
} // namespace

const int SaveBackups::keepChoices[4] = {3, 5, 10, 20};

SaveBackups::SaveBackups(const std::string &r) : root(r) {}
std::string SaveBackups::dir(const std::string &key) const { return root + "/save-backups/" + key; }

void SaveBackups::record(const std::string &key, const std::string &name, int engine, const std::string &folder,
                         const std::string &saveFolder, bool enabled, int keep) const {
  makeDir(root + "/save-backups");
  makeDir(dir(key));
  auto v = parseObject("");
  auto &o = v.as_object();
  o["name"] = name;
  o["engine"] = Json::integer_type(engine);
  o["folder"] = folder;
  if (!saveFolder.empty())
    o["saves"] = saveFolder;
  o["enabled"] = enabled;
  o["keep"] = Json::integer_type(keep > 0 ? keep : defaultKeep);
  o["updated"] = Json::integer_type(int64_t(std::time(nullptr)));
  atomicWrite(join(dir(key), "game.json"), v.stringify());
}

int SaveBackups::keep(const std::string &key) const {
  auto k = numberOf(parseObject(readText(join(dir(key), "game.json"))), "keep", defaultKeep);
  return k > 0 ? int(k) : defaultKeep;
}

int SaveBackups::sync(const std::string &key, const std::string &why, int keepCount) const {
  auto d = dir(key);
  auto info = parseObject(readText(join(d, "game.json")));
  auto folder = textOf(info, "folder"), saves = textOf(info, "saves");
  if (!isDir(d) && (folder.empty() || !flagOf(info, "enabled", true)))
    return 0;
  if (!flagOf(info, "enabled", true) && why == "library")
    return 0;
  if (keepCount <= 0)
    keepCount = keep(key);
  // Every save this game is known to have: by full path, its slot folder.
  std::map<std::string, std::string> known;
  auto resolve = [&](const std::string &recorded) {
    return recorded.find(':') != std::string::npos || (!recorded.empty() && recorded[0] == '/') ? recorded
                                                                                               : join(folder, recorded);
  };
  for (auto &slot : names(d, true)) {
    auto recorded = readText(join(join(d, slot), "path.txt"), 4096);
    if (!recorded.empty() && (!folder.empty() || recorded.find(':') != std::string::npos))
      known[resolve(recorded)] = slot;
  }
  auto add = [&](const std::string &full, const std::string &recorded) {
    if (known.count(full))
      return;
    makeDir(root + "/save-backups");
    makeDir(d);
    auto name = baseOf(full);
    for (auto id : {name, name + "~" + hardrpg::key(recorded).substr(0, 8)}) {
      auto slotDir = join(d, id);
      if (isDir(slotDir))
        continue;
      makeDir(slotDir);
      atomicWrite(join(slotDir, "path.txt"), recorded);
      known[full] = id;
      return;
    }
  };
  if (!folder.empty() && isDir(folder))
    for (auto &n : names(folder, false))
      if (rgssSave(n))
        add(join(folder, n), n);
  if (!saves.empty() && isDir(saves))
    for (auto &n : names(saves, false))
      if (lcfSave(n))
        add(join(saves, n), join(saves, n));
  int copied = 0;
  for (auto &k : known) {
    struct stat s;
    if (!statOf(k.first, s) || !S_ISREG(s.st_mode) || size_t(s.st_size) > maxSave)
      continue;
    auto slotDir = join(d, k.second);
    auto list = versionsIn(slotDir);
    int64_t size = s.st_size, saved = s.st_mtime;
    if (held(list, k.first, size, saved))
      continue;
    auto ext = extensionOf(baseOf(k.first));
    auto name = stamp(saved);
    for (int n = 2; isFile(join(slotDir, name + ext)); ++n)
      name = stamp(saved) + "-" + std::to_string(n);
    auto file = join(slotDir, name + ext);
    writeWhole(file, readFile(k.first, maxSave));
    auto meta = described(slotDir, size, saved);
    auto &o = meta.as_object();
    o.erase("format");
    o["saved"] = Json::integer_type(saved);
    o["kept"] = Json::integer_type(int64_t(std::time(nullptr)));
    o["why"] = why;
    o["size"] = Json::integer_type(size);
    atomicWrite(jsonOf(file), meta.stringify());
    ++copied;
    // The copy of the file the game has now does not count against keep.
    list = versionsIn(slotDir);
    for (size_t i = size_t(keepCount) + 1; i < list.size(); ++i)
      dropVersion(list[i]);
  }
  return copied;
}

std::vector<SaveSlot> SaveBackups::slots(const std::string &key) const {
  std::vector<SaveSlot> out;
  auto d = dir(key);
  auto info = parseObject(readText(join(d, "game.json")));
  auto folder = textOf(info, "folder");
  for (auto &id : names(d, true)) {
    SaveSlot slot;
    slot.id = id;
    auto slotDir = join(d, id);
    auto recorded = readText(join(slotDir, "path.txt"), 4096);
    slot.name = recorded.empty() ? id.substr(0, id.find('~')) : baseOf(recorded);
    if (!recorded.empty())
      slot.target = recorded.find(':') != std::string::npos || recorded[0] == '/' ? recorded
                    : folder.empty()                                           ? std::string()
                                                                               : join(folder, recorded);
    slotNumber(slot.name, slot.number);
    slot.versions = versionsIn(slotDir);
    struct stat s;
    if (!slot.target.empty() && statOf(slot.target, s) && S_ISREG(s.st_mode)) {
      slot.present = true;
      slot.now.file = slot.target;
      slot.now.size = s.st_size;
      slot.now.saved = s.st_mtime;
      auto meta = described(slotDir, slot.now.size, slot.now.saved);
      slot.now.mapName = textOf(meta, "map_name");
      slot.now.leader = textOf(meta, "leader");
      slot.now.level = int(numberOf(meta, "level"));
      slot.now.playtime = numberOf(meta, "playtime_s");
      if (numberOf(meta, "restored") > 0)
        slot.now.saved = numberOf(meta, "restored");
    }
    // What the game had last, from current.json: its own save time, or the
    // time of the version restored into it.
    auto last = parseObject(readText(join(slotDir, "current.json")));
    int64_t lastSize = numberOf(last, "size", -1), lastSaved = numberOf(last, "restored");
    if (lastSaved <= 0)
      lastSaved = numberOf(last, "saved");
    for (size_t i = 0; i < slot.versions.size(); ++i)
      if (slot.versions[i].size == lastSize && std::llabs(slot.versions[i].saved - lastSaved) <= 2) {
        slot.last = int(i);
        break;
      }
    if (slot.versions.empty() && !slot.present)
      continue;
    out.push_back(slot);
  }
  std::sort(out.begin(), out.end(), [](const SaveSlot &a, const SaveSlot &b) {
    if ((a.number < 0) != (b.number < 0))
      return b.number < 0;
    return a.number != b.number ? a.number < b.number : lower(a.name) < lower(b.name);
  });
  return out;
}

void SaveBackups::restore(const std::string &key, const SaveSlot &slot, const SaveVersion &version) const {
  if (slot.target.empty())
    throw std::runtime_error(tr("Where the game keeps this save is not known."));
  if (!isDir(parentOf(slot.target)))
    throw std::runtime_error(tr("Add the game to the library again to restore its saves."));
  auto data = readFile(version.file, maxSave);
  auto slotDir = join(dir(key), slot.id);
  // The file being replaced is kept first, unless a version already holds it.
  struct stat s;
  if (statOf(slot.target, s) && S_ISREG(s.st_mode)) {
    auto list = versionsIn(slotDir);
    int64_t size = s.st_size, saved = s.st_mtime;
    if (!held(list, slot.target, size, saved)) {
      auto ext = extensionOf(slot.name);
      auto name = stamp(saved);
      for (int n = 2; isFile(join(slotDir, name + ext)); ++n)
        name = stamp(saved) + "-" + std::to_string(n);
      auto file = join(slotDir, name + ext);
      writeWhole(file, readFile(slot.target, maxSave));
      auto meta = described(slotDir, size, saved);
      auto &o = meta.as_object();
      o["saved"] = Json::integer_type(saved);
      o["kept"] = Json::integer_type(int64_t(std::time(nullptr)));
      o["why"] = "restore";
      o["size"] = Json::integer_type(size);
      atomicWrite(jsonOf(file), meta.stringify());
    }
  }
  writeWhole(slot.target, data);
  // The game's file now holds the restored save: describe it as such.
  auto meta = parseObject(readText(jsonOf(version.file)));
  auto &o = meta.as_object();
  o.erase("why");
  o.erase("kept");
  if (statOf(slot.target, s)) {
    o["size"] = Json::integer_type(int64_t(s.st_size));
    o["saved"] = Json::integer_type(int64_t(s.st_mtime));
  }
  o["restored"] = Json::integer_type(version.saved);
  atomicWrite(join(slotDir, "current.json"), meta.stringify());
  auto list = versionsIn(slotDir);
  for (size_t i = size_t(keep(key)) + 1; i < list.size(); ++i)
    if (list[i].file != version.file)
      dropVersion(list[i]);
}

void SaveBackups::drop(const SaveVersion &v) const { dropVersion(v); }

BackupGame SaveBackups::game(const std::string &key) const {
  BackupGame g;
  g.key = key;
  auto d = dir(key);
  auto info = parseObject(readText(join(d, "game.json")));
  g.name = textOf(info, "name");
  g.folder = textOf(info, "folder");
  g.engine = int(numberOf(info, "engine"));
  g.installed = !g.folder.empty() && (isDir(g.folder) || isFile(g.folder));
  if (g.name.empty())
    g.name = g.folder.empty() ? key : baseOf(g.folder);
  for (auto &slot : names(d, true)) {
    auto list = versionsIn(join(d, slot));
    if (list.empty())
      continue;
    ++g.slots;
    for (auto &v : list) {
      g.bytes += v.size;
      g.newest = std::max(g.newest, v.saved);
    }
  }
  return g;
}

std::vector<BackupGame> SaveBackups::games() const {
  std::vector<BackupGame> out;
  for (auto &key : names(root + "/save-backups", true)) {
    auto g = game(key);
    if (g.slots)
      out.push_back(g);
  }
  std::sort(out.begin(), out.end(), [](const BackupGame &a, const BackupGame &b) {
    return a.newest != b.newest ? a.newest > b.newest : lower(a.name) < lower(b.name);
  });
  return out;
}

void SaveBackups::forget(const std::string &key) const {
  if (key.empty() || key.find('/') != std::string::npos || key == "." || key == "..")
    throw std::runtime_error("Invalid backup");
  removeAll(dir(key));
}

void SaveBackups::adopt(const std::string &from, const std::string &to) const {
  if (from == to)
    return;
  auto a = dir(from), b = dir(to);
  makeDir(root + "/save-backups");
  makeDir(b);
  for (auto &slot : names(a, true)) {
    auto source = join(a, slot), dest = join(b, slot);
    if (!isDir(dest) && !std::rename(source.c_str(), dest.c_str()))
      continue;
    makeDir(dest);
    if (!isFile(join(dest, "path.txt")) && isFile(join(source, "path.txt")))
      atomicWrite(join(dest, "path.txt"), readText(join(source, "path.txt"), 4096));
    for (auto &n : names(source, false))
      if (versionName(n) || (n.size() > 5 && n.compare(n.size() - 5, 5, ".json") == 0 && n != "current.json"))
        if (!isFile(join(dest, n)))
          std::rename(join(source, n).c_str(), join(dest, n).c_str());
  }
  forget(from);
}

void SaveBackups::markRunning(const std::string &key) const {
  makeDir(root + "/save-backups");
  atomicWrite(root + "/save-backups/running.txt", key);
}

std::string SaveBackups::takeRunning() const {
  auto file = root + "/save-backups/running.txt";
  auto key = readText(file, 256);
  unlink(file.c_str());
  while (!key.empty() && (key.back() == '\n' || key.back() == '\r'))
    key.pop_back();
  return key.find('/') == std::string::npos ? key : std::string();
}

std::string backupTime(int64_t when) {
  if (when <= 0)
    return {};
  time_t t = time_t(when), now = std::time(nullptr);
  struct tm a {}, b {};
  localtime_r(&t, &a);
  localtime_r(&now, &b);
  char clock[16], date[32];
  std::snprintf(clock, sizeof clock, "%02d:%02d", a.tm_hour, a.tm_min);
  std::snprintf(date, sizeof date, "%04d-%02d-%02d", a.tm_year + 1900, a.tm_mon + 1, a.tm_mday);
  struct tm y = b;
  y.tm_mday -= 1;
  y.tm_isdst = -1;
  time_t yesterday = mktime(&y);
  struct tm yd {};
  localtime_r(&yesterday, &yd);
  if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday)
    return trf("Today {}", {clock});
  if (a.tm_year == yd.tm_year && a.tm_yday == yd.tm_yday)
    return trf("Yesterday {}", {clock});
  return std::string(date) + " " + clock;
}

std::string backupSize(int64_t bytes) {
  char text[32];
  if (bytes < 1024 * 1024)
    std::snprintf(text, sizeof text, "%d KB", int((bytes + 1023) / 1024));
  else
    std::snprintf(text, sizeof text, "%.1f MB", bytes / (1024.0 * 1024.0));
  return text;
}
} // namespace hardrpg
