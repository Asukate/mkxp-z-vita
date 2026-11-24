#pragma once

#include <string>
#include <vector>

namespace hardrpg {
struct Entry;
struct Paths;

struct Achievement {
  std::string id, name, description;
  bool hidden = false;
  bool unlocked = false;
  bool concealed() const { return hidden && !unlocked; }
};

struct GameAchievements {
  std::vector<Achievement> entries;
  bool available = false;
  size_t earned = 0, total = 0; // total is known only with a catalog
  size_t percent() const { return total ? earned * 100 / total : 0; }
};

// A launched game uses the same key as its library entry, including ZIPs.
// With a direct boot, derive the key from the game folder.
std::string achievementGameKey(const Paths &, const std::string &configPath,
                               const std::string &gameFolder);
bool registerAchievements(const Paths &, const std::string &game);
bool unlockAchievement(const Paths &, const std::string &game, const std::string &id);
bool achievementUnlocked(const Paths &, const std::string &game, const std::string &id);
GameAchievements gameAchievements(const Paths &, const Entry &);
} // namespace hardrpg
