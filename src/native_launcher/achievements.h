#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hardrpg {
struct Entry;
struct Paths;

struct Achievement {
  std::string id, name, description;
  bool hidden = false;
  bool unlocked = false;
  // Icon file names in GameAchievements::iconDir; empty when not fetched.
  std::string icon, iconLocked;
  int64_t unlockedAt = 0; // Unix time, 0 when unknown
  bool concealed() const { return hidden && !unlocked; }
};

struct GameAchievements {
  // Unlocked first, newest first, then the locked ones in catalog order.
  std::vector<Achievement> entries;
  bool available = false;
  size_t earned = 0, total = 0; // total is known only with a catalog
  std::string game;             // the game's key under achievements/
  std::string iconDir;          // where the icons are, ending in '/'
  size_t percent() const { return total ? earned * 100 / total : 0; }
  // Entries shown in the unlocked section (they come first).
  int unlockedRows() const {
    int n = 0;
    while (n < int(entries.size()) && entries[size_t(n)].unlocked)
      ++n;
    return n;
  }
};

// A launched game uses the same key as its library entry, including ZIPs.
// With a direct boot, derive the key from the game folder.
std::string achievementGameKey(const Paths &, const std::string &configPath,
                               const std::string &gameFolder);
bool registerAchievements(const Paths &, const std::string &game);
bool unlockAchievement(const Paths &, const std::string &game, const std::string &id);
bool achievementUnlocked(const Paths &, const std::string &game, const std::string &id);
GameAchievements gameAchievements(const Paths &, const Entry &);

// Steam catalogs fetched by the launcher (achievement_fetch.h) are kept in
// achievements/<game>/steam.json, with the icons in achievements/<game>/icons/.
// The record also marks a game Steam has no achievements for, so it is looked
// up once. Returns true when the game has such a record.
bool steamCatalogRecorded(const Paths &, const std::string &game);
// Whether the game uses achievements: it has an achievements folder, made when
// the game starts with the achievement bridge (or by hand, for a catalog).
bool achievementsUsed(const Paths &, const Entry &);

// Reads a Steam Community achievements page (the HTML of
// steamcommunity.com/achievements/<appid>): false when the page is not one
// (Steam changed it, an error page); true with no rows for an app without
// achievements. The rows keep Steam's order.
struct SteamAchievement {
  std::string id, name, description, icon, iconLocked;
  bool hidden = false;
};
bool parseSteamAchievementPage(const std::string &html, long appid, std::vector<SteamAchievement> &out);
// The steam.json record. incomplete: some icons are still to download.
std::string steamCatalogJson(long appid, const std::string &language, const std::vector<SteamAchievement> &rows,
                             bool incomplete = false);
// Icon file names must be plain names Steam gives (hex digits and .jpg).
bool safeIconName(const std::string &);
} // namespace hardrpg
