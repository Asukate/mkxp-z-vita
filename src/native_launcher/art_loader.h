#pragma once
#include "image.h"
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#else
#include <thread>
#endif

namespace hardrpg {
// Decodes and scales launcher art on its own thread (its own CPU core on the
// Vita), so scrolling never waits for a PNG. The UI asks for art every frame
// it is on screen; the nearest and most recently wanted pictures go first,
// and requests nobody has asked for lately are dropped.
class ArtLoader {
public:
  struct Request {
    std::string key;
    bool hero = false;
    bool shots = false;             // the screenshots instead (width x height each)
    std::string root;               // the art folder, ending in '/'
    std::vector<std::string> names; // folders under root to try, in order
    int width = 0, height = 0;      // cover or background size
    // Scaled copies are kept here, so each picture is decoded in full once.
    std::string cache;   // ending in '/'
    std::string title;   // names the generated cover when there is no art
    bool blurAdult = true; // blur the pictures of games marked adult
  };
  struct Result {
    std::string key;
    bool hero = false, cancelled = false, shots = false;
    Image cover, logo, background;
    std::vector<Image> pictures; // screenshots, when asked for
    // No cover art: draw one and give it to store() under this path.
    std::string generatedPath;
    std::string info; // the game's info.json, if it has one
  };
  ArtLoader();
  ~ArtLoader();
  ArtLoader(const ArtLoader &) = delete;
  ArtLoader &operator=(const ArtLoader &) = delete;
  // Call once per drawn frame before asking for art.
  void beginFrame();
  // Queues r, or only marks it wanted again when it is already queued.
  // Lower distance loads first.
  void want(const Request &r, double distance);
  bool take(Result &out);
  // Saves a generated cover in the cache, on the loader's thread.
  void store(const std::string &path, Image image);
  // Art folders changed (artwork was downloaded): list them again.
  void rescan(const std::string &key);
  // Anything still queued, decoding or not yet taken.
  bool busy();

private:
  struct Pending {
    Request request;
    double distance = 0;
    uint64_t frame = 0;
  };
  std::mutex lock;
  std::map<std::string, Pending> queue;
  std::deque<Result> done;
  // Decoding, or decoded and not yet taken: asking again changes nothing.
  std::set<std::string> active;
  int working = 0;
  uint64_t frame = 0;
  std::atomic<bool> stop{false};
  // Folder names under the art root, listed once so missing art costs no file opens.
  std::set<std::string> folders;
  bool foldersRead = false, foldersOk = false;
  std::atomic<bool> rescanWanted{false};
  std::string cacheMade; // the cache folder already created
  std::deque<std::pair<std::string, Image>> saves;
  void run();
  Result load(const Request &);
#ifdef __vita__
  SceUID thread = -1;
  static int entry(SceSize, void *);
#else
  std::thread thread;
#endif
};
} // namespace hardrpg
