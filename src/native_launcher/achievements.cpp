#include "achievements.h"
#include "model.h"
#include "theme.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
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

// A marker holds its unlock time as decimal Unix seconds. Markers written
// before times were kept are empty; their file time stands in.
constexpr off_t maxMarker = 32;

bool markerExists(const std::string &path, int64_t *when = nullptr) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size > maxMarker)
    return false;
  if (when) {
    *when = int64_t(st.st_mtime);
    if (st.st_size > 0) {
      try {
        auto text = readFile(path, maxMarker);
        char *end = nullptr;
        long long t = std::strtoll(text.c_str(), &end, 10);
        if (end != text.c_str() && t > 0)
          *when = t;
      } catch (const std::exception &) {
      }
    }
  }
  return true;
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

bool boolean(const Json &row, const char *name) {
  auto it = row.as_object().find(name);
  return it != row.as_object().end() && it->second.is_boolean() && it->second.as_boolean();
}

std::string gameDir(const Paths &paths, const std::string &game) { return paths.at("achievements/" + game); }

// The catalog to show, in priority order: one put in the achievements folder,
// one shipped beside Game.ini, then the one fetched from Steam.
std::string catalog(const Paths &paths, const Entry &entry, const std::string &game) {
  try {
    return readFile(gameDir(paths, game) + "/catalog.json", maxCatalog);
  } catch (const std::exception &) {
  }
  // Reading inside a ZIP mounts it, which is slow on the Vita: only for a
  // ZIP game that uses achievements (it has its achievements folder).
  if (entry.node.archive && !directory(gameDir(paths, game)))
    return {};
  try {
    auto data = entry.node.read("hardrpg-achievements.json");
    if (!data.empty())
      return data;
  } catch (const std::exception &) {
  }
  return {};
}

// The fetched Steam record's rows ("achievements"), or null.
Json steamRows(const Paths &paths, const std::string &game) {
  try {
    auto data = readFile(gameDir(paths, game) + "/steam.json", maxCatalog);
    auto doc = json5pp::parse(data);
    if (doc.is_object()) {
      auto it = doc.as_object().find("achievements");
      if (it != doc.as_object().end() && it->second.is_array() && it->second.as_array().size() <= maxEntries)
        return it->second;
    }
  } catch (const std::exception &) {
  }
  return Json();
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

bool unlockAchievement(const Paths &paths, const std::string &game, const std::string &id, bool *fresh) {
  if (fresh)
    *fresh = false;
  if (!validId(id) || !registerAchievements(paths, game))
    return false;

  // The marker itself is the whole record: the unlock time. Exclusive
  // creation keeps a repeated award or concurrent writer from replacing the
  // original unlock.
  auto path = markerDir(paths, game) + "/" + encode(id);
  int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0)
    return errno == EEXIST && markerExists(path);
  char when[24];
  int n = std::snprintf(when, sizeof when, "%lld\n", (long long)std::time(nullptr));
  // A failed write leaves an empty marker: still unlocked, dated by the file.
  bool written = n > 0 && write(fd, when, size_t(n)) == n;
  (void)written;
  if (fresh)
    *fresh = true;
  return close(fd) == 0;
}

bool achievementUnlocked(const Paths &paths, const std::string &game, const std::string &id) {
  return validGame(game) && validId(id) && markerExists(markerDir(paths, game) + "/" + encode(id));
}

GameAchievements gameAchievements(const Paths &paths, const Entry &entry) {
  const auto game = key(entry.node.identity(paths));
  const auto dir = markerDir(paths, game);
  std::map<std::string, int64_t> unlocked;
  auto closeDir = [](DIR *handle) { closedir(handle); };
  std::unique_ptr<DIR, decltype(closeDir)> files(opendir(dir.c_str()), closeDir);
  if (files) {
    while (auto *item = readdir(files.get())) {
      auto id = decode(item->d_name);
      int64_t when = 0;
      if (!id.empty() && markerExists(dir + "/" + item->d_name, &when))
        unlocked[id] = when;
    }
  }

  GameAchievements result;
  result.game = game;
  result.iconDir = gameDir(paths, game) + "/icons/";
  auto &rows = result.entries;
  std::set<std::string> seen;
  // Icons come from the Steam record whichever catalog gives the text.
  std::map<std::string, std::pair<std::string, std::string>> icons;
  auto steam = steamRows(paths, game);
  if (steam.is_array())
    for (const auto &row : steam.as_array())
      if (row.is_object()) {
        auto icon = field(row, "icon"), locked = field(row, "iconLocked");
        icons[field(row, "id")] = {safeIconName(icon) ? icon : "", safeIconName(locked) ? locked : ""};
      }
  auto take = [&](const Json &list) {
    if (!list.is_array() || list.as_array().size() > maxEntries)
      return;
    for (const auto &row : list.as_array()) {
      if (!row.is_object())
        continue;
      auto id = field(row, "id");
      if (!validId(id) || !seen.insert(id).second)
        continue;
      Achievement a;
      a.id = id;
      a.name = field(row, "name");
      if (a.name.empty())
        a.name = id;
      a.description = field(row, "description");
      a.hidden = boolean(row, "hidden");
      auto icon = icons.find(id);
      if (icon != icons.end()) {
        a.icon = icon->second.first;
        a.iconLocked = icon->second.second;
      }
      auto earned = unlocked.find(id);
      if (earned != unlocked.end()) {
        a.unlocked = true;
        a.unlockedAt = earned->second;
        unlocked.erase(earned);
      }
      rows.push_back(a);
    }
  };
  try {
    auto data = catalog(paths, entry, game);
    if (!data.empty() && data.size() <= maxCatalog)
      take(json5pp::parse(data));
    else
      take(steam);
  } catch (const std::exception &) {
    // A missing or invalid catalog still leaves locally earned IDs visible.
  }
  result.total = rows.size();
  result.earned = result.total ? std::count_if(rows.begin(), rows.end(), [](const Achievement &a) { return a.unlocked; })
                               : unlocked.size();
  result.available = result.total || bool(files);
  for (const auto &item : unlocked) {
    Achievement a;
    a.id = a.name = item.first;
    a.unlocked = true;
    a.unlockedAt = item.second;
    rows.push_back(a);
  }
  // As Steam lists them: the newest unlock first, then what is still locked.
  std::stable_sort(rows.begin(), rows.end(), [](const Achievement &a, const Achievement &b) {
    if (a.unlocked != b.unlocked)
      return a.unlocked;
    return a.unlocked && a.unlockedAt > b.unlockedAt;
  });
  return result;
}

Achievement describeAchievement(const Paths &paths, const std::string &game, const std::string &gameFolder,
                                const std::string &id, std::string &iconPath) {
  Achievement a;
  a.id = a.name = id;
  a.unlocked = true;
  iconPath.clear();
  if (!validGame(game) || !validId(id))
    return a;
  auto steam = steamRows(paths, game);
  auto find = [&](const Json &list) -> const Json * {
    if (list.is_array())
      for (const auto &row : list.as_array())
        if (row.is_object() && field(row, "id") == id)
          return &row;
    return nullptr;
  };
  // The text from the first catalog that has this achievement.
  std::vector<std::string> texts;
  for (auto path : {gameDir(paths, game) + "/catalog.json", gameFolder + "/hardrpg-achievements.json"}) {
    try {
      texts.push_back(readFile(path, maxCatalog));
    } catch (const std::exception &) {
    }
  }
  const Json *row = nullptr;
  Json parsed;
  for (auto &text : texts) {
    try {
      parsed = json5pp::parse(text);
      if ((row = find(parsed)))
        break;
    } catch (const std::exception &) {
    }
  }
  if (!row)
    row = find(steam);
  if (row) {
    auto name = field(*row, "name");
    if (!name.empty())
      a.name = name;
    a.description = field(*row, "description");
  }
  if (const Json *icons = find(steam)) {
    a.icon = field(*icons, "icon");
    if (safeIconName(a.icon))
      iconPath = gameDir(paths, game) + "/icons/" + a.icon;
  }
  return a;
}

bool steamCatalogRecorded(const Paths &paths, const std::string &game) {
  struct stat st;
  return validGame(game) && stat((gameDir(paths, game) + "/steam.json").c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool achievementsUsed(const Paths &paths, const Entry &entry) {
  return directory(gameDir(paths, key(entry.node.identity(paths))));
}

bool steamScriptGame(const Paths &paths, const Entry &entry) {
  if (entry.kind != Kind::Game || entry.node.archive)
    return false;
  auto dir = entry.node.path();
  struct stat st;
  if (stat((dir + "/steam_api.dll").c_str(), &st) != 0)
    return false;
  auto game = key(entry.node.identity(paths));
  auto list = paths.at("achievements/steam-script-checked.txt");
  std::string checked;
  try {
    checked = readFile(list, 256 * 1024);
  } catch (const std::exception &) {
  }
  std::istringstream lines(checked);
  for (std::string line; std::getline(lines, line);)
    if (line == game)
      return false;
  if (gameScriptsMention(dir, "steam_acheivement"))
    return registerAchievements(paths, game);
  if (ensureDir(paths.root) && ensureDir(paths.at("achievements")))
    std::ofstream(list, std::ios::app) << game << "\n";
  return false;
}

bool safeIconName(const std::string &name) {
  if (name.size() < 5 || name.size() > 80 || name.compare(name.size() - 4, 4, ".jpg") != 0)
    return false;
  return name.find_first_not_of(hex, 0) == name.size() - 4;
}

namespace {
// The JSON inside <script ... id="valve-ssr-data">...</script>.
std::string ssrData(const std::string &html) {
  auto id = html.find("id=\"valve-ssr-data\"");
  if (id == std::string::npos)
    return {};
  auto open = html.rfind("<script", id);
  auto start = html.find('>', id);
  auto end = start == std::string::npos ? start : html.find("</script>", start);
  if (open == std::string::npos || end == std::string::npos)
    return {};
  return html.substr(start + 1, end - start - 1);
}
const Json *member(const Json &v, const char *name) {
  if (!v.is_object())
    return nullptr;
  auto it = v.as_object().find(name);
  return it == v.as_object().end() ? nullptr : &it->second;
}
} // namespace

bool parseSteamAchievementPage(const std::string &html, long appid, std::vector<SteamAchievement> &out) {
  out.clear();
  try {
    auto data = ssrData(html);
    if (data.empty())
      return false;
    auto page = json5pp::parse(data);
    auto *context = member(page, "renderContext");
    auto *queryText = context ? member(*context, "queryData") : nullptr;
    if (!queryText || !queryText->is_string())
      return false;
    auto queryData = json5pp::parse(queryText->as_string());
    auto *queries = member(queryData, "queries");
    if (!queries || !queries->is_array())
      return false;
    for (const auto &query : queries->as_array()) {
      // queryKey ["achievements", <appid>, "schema_v2"]
      auto *k = member(query, "queryKey");
      if (!k || !k->is_array() || k->as_array().size() != 3)
        continue;
      auto &parts = k->as_array();
      if (!parts[0].is_string() || parts[0].as_string() != "achievements" || !parts[1].is_number() ||
          long(parts[1].as_number()) != appid || !parts[2].is_string() || parts[2].as_string() != "schema_v2")
        continue;
      auto *state = member(query, "state");
      auto *schema = state ? member(*state, "data") : nullptr;
      auto *groups = schema ? member(*schema, "groups") : nullptr;
      if (!groups || !groups->is_array())
        return true; // an app without achievements
      std::set<std::string> ids;
      for (const auto &group : groups->as_array()) {
        if (!group.is_object() || boolean(group, "archived") || !boolean(group, "ispublic"))
          continue;
        auto *list = member(group, "achievements");
        if (!list || !list->is_array())
          continue;
        for (const auto &a : list->as_array()) {
          if (!a.is_object() || boolean(a, "archived"))
            continue;
          SteamAchievement row;
          row.id = field(a, "api_name");
          if (!validId(row.id) || !ids.insert(row.id).second || out.size() >= maxEntries)
            continue;
          row.name = field(a, "name");
          row.description = field(a, "description");
          row.hidden = boolean(a, "hidden");
          row.icon = field(a, "icon_achieved");
          row.iconLocked = field(a, "icon_unachieved");
          if (!safeIconName(row.icon))
            row.icon.clear();
          if (!safeIconName(row.iconLocked))
            row.iconLocked.clear();
          out.push_back(row);
        }
      }
      return true;
    }
    // The page was understood, but it lists no achievements for this app.
    return true;
  } catch (const std::exception &) {
  }
  return false;
}

std::string steamCatalogJson(long appid, const std::string &language, const std::vector<SteamAchievement> &rows,
                             bool incomplete) {
  auto doc = Json(std::initializer_list<Json::pair_type>{});
  auto &o = doc.as_object();
  o["source"] = "steam";
  o["appid"] = Json::integer_type(appid);
  o["language"] = language;
  o["fetched"] = Json::integer_type(int64_t(std::time(nullptr)));
  if (incomplete)
    o["incomplete"] = true;
  auto list = Json(std::initializer_list<Json>{});
  for (const auto &a : rows) {
    auto row = Json(std::initializer_list<Json::pair_type>{});
    auto &r = row.as_object();
    r["id"] = a.id;
    r["name"] = a.name;
    r["description"] = a.description;
    r["hidden"] = a.hidden;
    r["icon"] = a.icon;
    r["iconLocked"] = a.iconLocked;
    list.as_array().push_back(row);
  }
  o["achievements"] = list;
  return doc.stringify();
}
} // namespace hardrpg
