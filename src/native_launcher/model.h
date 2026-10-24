#pragma once

#include "engines.h"
#include "util/json5pp.hpp"
#include "input/vita_controls.h"
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace hardrpg {
// Game filtering ("smoothScaling"), in the order the launcher offers them:
// Nearest, Sharp (even pixels at any scale, the default) and Bilinear.
constexpr int filterCount = 3, filterDefault = 5;
constexpr int filterValues[filterCount] = {0, 5, 1};
constexpr const char *filterNames[filterCount] = {"Nearest", "Sharp", "Bilinear"};
// Position of a stored value in filterValues; -1 when it is not one of them.
inline int filterIndex(const json5pp::value &v) {
  if (!v.is_integer())
    return -1;
  for (int i = 0; i < filterCount; ++i)
    if (v.as_integer() == filterValues[i])
      return i;
  return -1;
}
using Json = json5pp::value;
using Progress = std::function<void(uint64_t, uint64_t)>;
using PreparationStatus = std::function<void(const std::string &)>;
using ScanProgress = std::function<void(size_t, const std::string &)>;
struct Cancelled : std::runtime_error {
  using std::runtime_error::runtime_error;
};
struct MissingRtp : std::runtime_error {
  std::string pack;
  explicit MissingRtp(const std::string &name);
};
struct Paths {
  std::string root = "ux0:/data/hardrpg";
  std::vector<std::string> storage = {"ux0:/", "uma0:/", "imc0:/"};
  std::string at(const std::string &name) const { return root + "/" + name; }
  bool external(const std::string &path) const;
  void setup() const;
};
bool relative(const std::string &path, bool empty = false);
std::string key(const std::string &identity);
std::string lower(std::string value);
std::string readFile(const std::string &path, size_t limit);
void atomicWrite(const std::string &path, const std::string &data);
bool directory(const std::string &path);
struct File {
  std::string name;
  bool directory;
  int64_t size;
};
struct Node {
  std::string relative, inner, root;
  bool archive = false;
  // An unpacked ZIP game keeps the identity of the ZIP game it came from, so
  // its settings, controls, name and saves follow it after the ZIP is gone.
  std::string alias;
  static Node fromPath(const Paths &, const std::string &, bool zip = false,
                       const std::string &inner = "");
  std::string path() const;
  std::string identity(const Paths &) const;
  std::string label(const Paths &) const;
  Node child(const std::string &, bool zip = false) const;
  std::vector<File> files() const;
  std::string read(const std::string &) const;
  std::string iniName() const;
  Json ini() const;
  std::vector<std::string> rtps() const;
};
enum class Kind { Folder, Zip, Game };
struct Entry {
  std::string name;
  Kind kind;
  Node node;
  int version = 0;
};
bool game(const Node &, Entry &);
bool configurableGame(const Entry &, Entry &);
std::vector<Entry> entries(const Node &);
struct ErrorRecord {
  std::string path, game, timestamp, summary;
};
// When a game was last started and how long it has been played, in seconds.
struct Activity {
  int64_t last = 0, seconds = 0;
};
struct Mount {
  std::string path, root;
};
class Model {
public:
  // scan = false leaves the game list empty until refresh().
  explicit Model(Paths paths = {}, bool scan = true);
  Paths paths;
  std::vector<Node> stack;
  std::vector<Entry> list;
  std::string error;
  std::function<bool(const Engine &)> backendAvailable;
  // Unpack a ZIP game on its first start. Off on the Vita for 0.4.0 (minutes
  // even for a small game); the desktop test host turns it on.
  bool unpackZips = false;
  const Node &current() const { return stack.back(); }
  void refresh();
  void setDisplayName(const Entry &, const std::string &displayName);
  bool back();
  bool enter(const Entry &, Entry &selected);
  std::vector<Node> discover(const std::string &, ScanProgress = {}) const;
  size_t addFolder(const std::string &, ScanProgress = {});
  size_t addSearchFolder(const std::string &, ScanProgress = {});
  void removeSearchFolder(const std::string &);
  std::vector<std::string> searchFolders() const;
  std::vector<Node> references() const;
  // Unpacked ZIP games whose ZIP is gone; they launch from cache/.
  std::vector<Node> unpackedGames() const;
  // What "Remove from library" can do for a game.
  struct Removal {
    std::string folder, zip, unpacked; // existing paths, or empty
  };
  Removal removal(const Entry &) const;
  void hide(const Entry &);
  // Takes a game off the list, deleting its folder or ZIP and/or the
  // unpacked copy as asked. While any of its files remain it stays hidden.
  void remove(const Entry &, bool files, bool unpacked);
  // Deletes an unpacked ZIP game's ZIP; the game stays, played from its copy.
  void deleteZip(const Entry &);
  Json launch(const Entry &, Progress = {}, PreparationStatus = {});
  // The game started last (active-game.json), when its settings key is
  // config: the 16 hex digits the engine was given.
  bool activeGame(Entry &, const std::string &config) const;
  // A ZIP's unpacked copy in the cache. 0.4.0 no longer unpacks: a ZIP with
  // no copy yet is reported (rtpName names the RTP for an RTP ZIP).
  std::string prepareZip(const Node &, Progress = {}, const std::string &rtpName = "");
  ControlProfile controls(const Entry &) const;
  void saveControls(const Entry &, const ControlProfile &);
  Json display() const;
  void changeDisplay(const std::string &, const Json &);
  Json gameSettings(const Entry &) const;
  void changeGameSetting(const Entry &, const std::string &, const Json &,
                         bool reset = false);
  Json rtpPaths() const;
  void setRtp(const std::string &, const std::string &path = "");
  void clearRtpCache() const { rtpCache.clear(); }
  bool inspectRtp(const std::string &, const std::string &, Mount &,
                  int depth = 0) const;
  std::vector<Mount> resolveRtp(const Node &) const;
  std::string rtpStatus(const std::string &) const;
  std::vector<std::string> takeError(bool previous = false);
  std::vector<ErrorRecord> errorHistory() const;
  std::vector<std::string> readError(const std::string &) const;
  std::vector<std::string> libraryRoots() const;
  std::string activeErrorPath;
  std::string exportError() const;
  // Play history and favourites, keyed by key(node.identity(paths)).
  std::map<std::string, Activity> activity() const;
  // Marks a game as started now; the time counts once the launcher returns.
  void recordLaunch(const std::string &id);
  // On a launcher boot after a game, adds the elapsed time to that game.
  void settleActivity(bool returning);
  std::set<std::string> favourites() const;
  void setFavourite(const std::string &id, bool on);

private:
  struct CachedRtp {
    Mount mount;
    int64_t size, modified;
    bool found;
  };
  mutable std::map<std::string, CachedRtp> rtpCache;
  bool inspectRtpRaw(const std::string &, const std::string &, Mount &,
                     int) const;
  void saveSearchFolders(const std::vector<std::string> &);
  std::set<std::string> hidden() const;
  void saveHidden(const std::set<std::string> &);
  void forget(const Entry &);
  void applyNames();
};
class Browser {
public:
  Browser(const Paths &, bool zips = false);
  Paths paths;
  std::string path, error;
  std::vector<File> list;
  bool zips;
  void refresh();
  void enter(const std::string &);
  bool back();
  std::string selectedPath(size_t) const;
};
extern const char *const packs[rtpPackCount];
std::string packName(const std::string &);
std::string packLabel(const std::string &);
} // namespace hardrpg
