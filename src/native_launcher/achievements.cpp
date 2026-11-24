#include "achievements.h"
#include "model.h"

#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

namespace hardrpg {
namespace {
constexpr char hex[] = "0123456789abcdef";
constexpr size_t maxId = 100;
constexpr size_t maxCatalog = 256 * 1024;
constexpr size_t maxEntries = 1024;

bool validGame(const std::string &game) {
  return game.size() == 16 && game.find_first_not_of(hex) == std::string::npos;
}

bool validId(const std::string &id) {
  if (id.empty() || id.size() > maxId)
    return false;
  for (unsigned char c : id)
    if (c < 32 || c == 127)
      return false;
  return true;
}

std::string encode(const std::string &id) {
  // FAT is case-insensitive. Encode the original bytes to preserve case.
  std::string out;
  out.reserve(id.size() * 2);
  for (unsigned char c : id) {
    out += hex[c >> 4];
    out += hex[c & 15];
  }
  return out;
}

std::string decode(const std::string &file) {
  if (file.size() > maxId * 2 || file.size() % 2 ||
      file.find_first_not_of(hex) != std::string::npos)
    return {};
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  std::string id;
  for (size_t i = 0; i < file.size(); i += 2)
    id += char(digit(file[i]) * 16 + digit(file[i + 1]));
  return validId(id) ? id : std::string();
}

bool markerExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size == 0;
}

bool ensureDir(const std::string &path) {
  return mkdir(path.c_str(), 0777) == 0 || (errno == EEXIST && directory(path));
}

std::string markerDir(const Paths &paths, const std::string &game) {
  return paths.at("achievements/" + game + "/unlocked");
}

std::string field(const Json &row, const char *name) {
  auto it = row.as_object().find(name);
  return it != row.as_object().end() && it->second.is_string() ? it->second.as_string() : "";
}

std::string catalog(const Paths &paths, const Entry &entry, const std::string &game) {
  try {
    return readFile(paths.at("achievements/" + game + "/catalog.json"), maxCatalog);
  } catch (const std::exception &) {
  }
  try {
    return entry.node.read("hardrpg-achievements.json");
  } catch (const std::exception &) {
    return {};
  }
}
} // namespace

std::string achievementGameKey(const Paths &paths, const std::string &configPath,
                               const std::string &gameFolder) {
  auto slash = configPath.find_last_of('/');
  auto file = configPath.substr(slash == std::string::npos ? 0 : slash + 1);
  if (file.size() == 21 && file.compare(16, 5, ".json") == 0 && validGame(file.substr(0, 16)))
    return file.substr(0, 16);
  if (gameFolder.empty() || gameFolder == "app0:/stub")
    return {};
  auto folder = gameFolder;
  while (!folder.empty() && folder.back() == '/')
    folder.pop_back();
  return paths.external(folder) ? key(Node::fromPath(paths, folder).identity(paths)) : std::string();
}

bool registerAchievements(const Paths &paths, const std::string &game) {
  if (!validGame(game))
    return false;
  auto parent = paths.at("achievements");
  return ensureDir(paths.root) && ensureDir(parent) && ensureDir(parent + "/" + game) &&
         ensureDir(markerDir(paths, game));
}

bool unlockAchievement(const Paths &paths, const std::string &game, const std::string &id) {
  if (!validId(id) || !registerAchievements(paths, game))
    return false;

  // The marker itself is the whole record. Exclusive creation keeps a repeated
  // award or concurrent writer from replacing the original unlock.
  auto path = markerDir(paths, game) + "/" + encode(id);
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return errno == EEXIST && markerExists(path);
  return close(fd) == 0;
}

bool achievementUnlocked(const Paths &paths, const std::string &game, const std::string &id) {
  return validGame(game) && validId(id) && markerExists(markerDir(paths, game) + "/" + encode(id));
}

GameAchievements gameAchievements(const Paths &paths, const Entry &entry) {
  const auto game = key(entry.node.identity(paths));
  const auto dir = markerDir(paths, game);
  std::set<std::string> unlocked;
  auto closeDir = [](DIR *handle) { closedir(handle); };
  std::unique_ptr<DIR, decltype(closeDir)> files(opendir(dir.c_str()), closeDir);
  if (files) {
    while (auto *item = readdir(files.get())) {
      auto id = decode(item->d_name);
      if (!id.empty() && markerExists(dir + "/" + item->d_name))
        unlocked.insert(id);
    }
  }

  GameAchievements result;
  auto &rows = result.entries;
  std::set<std::string> seen;
  try {
    auto data = catalog(paths, entry, game);
    if (!data.empty() && data.size() <= maxCatalog) {
      auto list = json5pp::parse(data);
      if (list.is_array() && list.as_array().size() <= maxEntries)
        for (const auto &row : list.as_array()) {
          if (!row.is_object())
            continue;
          auto id = field(row, "id");
          if (!validId(id) || !seen.insert(id).second)
            continue;
          auto name = field(row, "name");
          auto hidden = row.as_object().find("hidden");
          rows.push_back({id, name.empty() ? id : name, field(row, "description"),
                          hidden != row.as_object().end() && hidden->second.is_boolean() &&
                              hidden->second.as_boolean(),
                          unlocked.erase(id) != 0});
        }
    }
  } catch (const std::exception &) {
    // A missing or invalid catalog still leaves locally earned IDs visible.
  }
  result.total = rows.size();
  result.earned = result.total ? std::count_if(rows.begin(), rows.end(), [](const Achievement &a) { return a.unlocked; })
                               : unlocked.size();
  result.available = result.total || bool(files);
  for (const auto &item : unlocked)
    rows.push_back({item, item, "", false, true});
  return result;
}
} // namespace hardrpg
