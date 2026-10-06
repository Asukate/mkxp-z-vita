#pragma once

#include "util/json5pp.hpp"
#include <cstdint>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace hardrpg {
using Json = json5pp::value;
using Progress = std::function<void(uint64_t, uint64_t)>;
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
  static Node fromPath(const Paths &, const std::string &, bool zip = false,
                       const std::string &inner = "");
  std::string path() const;
  std::string identity(const Paths &) const;
  std::string label(const Paths &) const;
  Node child(const std::string &, bool zip = false) const;
  std::vector<File> files() const;
  std::string read(const std::string &) const;
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
std::vector<Entry> entries(const Node &);
struct ErrorRecord {
  std::string path, game, timestamp, summary;
};
struct Mount {
  std::string path, root;
};
class Model {
public:
  explicit Model(Paths paths = {});
  Paths paths;
  std::vector<Node> stack;
  std::vector<Entry> list;
  std::string error;
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
  Json launch(const Entry &, Progress = {});
  std::string prepareZip(const Node &, Progress = {});
  Json display() const;
  void changeDisplay(const std::string &, const Json &);
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
extern const char *const packs[3];
std::string packName(const std::string &);
std::string packLabel(const std::string &);
} // namespace hardrpg
