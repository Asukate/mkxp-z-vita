#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hardrpg {
// Downloads achievement names, descriptions and icons from Steam Community,
// on its own thread, the way ArtFetch downloads artwork (and through the same
// HTTPS helper, so its test transport answers here too). No key or account.
//
// For each game: the Steam app ID (from the artwork lookup's info.json, else
// a Steam store search for the exact name), then the public page
// steamcommunity.com/achievements/<appid>?l=<language>, whose embedded data
// lists every achievement, then both icons of each from Steam's CDN.
//
// Written under the game's achievements folder: steam.json (see
// achievements.h) and icons/<file>.jpg. A game with no Steam match or no
// achievements gets a record too, so it is not looked up again. Nothing is
// written when the network fails; the game is tried again next time. A record
// whose icons did not all download is marked incomplete and its missing icons
// are tried again.
class AchievementFetch {
public:
  struct Job {
    std::string game; // the key under achievements/
    std::string name; // the game's name, searched for when appid is 0
    long appid = 0;
    bool force = false; // look up again even when the game has a record
  };
  struct Progress {
    int done = 0, total = 0, found = 0;
    std::vector<std::string> updated; // games whose record changed since last poll
    bool finished = false;
  };
  ~AchievementFetch();
  // root is the achievements folder, ending in '/'; language a Steam one.
  void start(std::vector<Job> jobs, const std::string &root, const std::string &language);
  bool running() const;
  Progress poll();
  void cancel();

private:
  struct State;
  std::shared_ptr<State> state;
};
// Steam's name for a launcher language ("english", "brazilian", ...).
const char *steamLanguage(int language);
// Whether the game's Steam record exists and has every icon.
bool steamCatalogComplete(const std::string &gameDir);
} // namespace hardrpg
