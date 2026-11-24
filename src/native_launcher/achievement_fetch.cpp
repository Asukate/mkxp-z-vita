#include "achievement_fetch.h"
#include "achievements.h"
#include "art_fetch.h"
#include "i18n.h"
#include "model.h"
#include "net_session.h"

#include <cerrno>
#include <cstdio>
#include <curl/curl.h>
#include <sys/stat.h>
#include <thread>

namespace hardrpg {
namespace {
constexpr size_t maxIcon = 256 * 1024;
constexpr size_t maxRecord = 256 * 1024;

bool fileExists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}
void makeDir(const std::string &path) { mkdir(path.c_str(), 0777); }

// A record already on the card: its app ID, rows and whether it is complete.
struct Record {
  bool present = false, incomplete = false;
  long appid = 0;
  std::string language;
  std::vector<SteamAchievement> rows;
};
Record readRecord(const std::string &dir) {
  Record r;
  try {
    auto doc = json5pp::parse(readFile(dir + "steam.json", maxRecord));
    if (!doc.is_object())
      return r;
    auto &o = doc.as_object();
    r.present = true;
    auto text = [](const Json &v, const char *name) {
      auto it = v.as_object().find(name);
      return it != v.as_object().end() && it->second.is_string() ? it->second.as_string() : std::string();
    };
    auto flag = [](const Json &v, const char *name) {
      auto it = v.as_object().find(name);
      return it != v.as_object().end() && it->second.is_boolean() && it->second.as_boolean();
    };
    auto app = o.find("appid");
    if (app != o.end() && app->second.is_number())
      r.appid = long(app->second.as_number());
    r.incomplete = flag(doc, "incomplete");
    r.language = text(doc, "language");
    auto list = o.find("achievements");
    if (list != o.end() && list->second.is_array())
      for (auto &row : list->second.as_array())
        if (row.is_object())
          r.rows.push_back({text(row, "id"), text(row, "name"), text(row, "description"), text(row, "icon"),
                            text(row, "iconLocked"), flag(row, "hidden")});
  } catch (const std::exception &) {
  }
  return r;
}
} // namespace

const char *steamLanguage(int language) {
  switch (language) {
  case Spanish:
    return "spanish";
  case Portuguese:
    return "brazilian";
  case Japanese:
    return "japanese";
  case Chinese:
    return "schinese";
  default:
    return "english";
  }
}

bool steamCatalogComplete(const std::string &gameDir) {
  auto r = readRecord(gameDir + "/");
  return r.present && !r.incomplete;
}

struct AchievementFetch::State {
  std::vector<Job> jobs;
  std::string root, language;
  std::atomic<bool> cancel{false}, done{false};
  mutable std::mutex lock;
  Progress progress;
  CURL *curl = nullptr;

  bool get(const std::string &url, std::string &out) { return httpsRequest(curl, cancel, url, out); }
  std::string escaped(const std::string &s) {
    char *e = curl_easy_escape(curl, s.c_str(), int(s.size()));
    std::string out = e ? e : "";
    curl_free(e);
    return out;
  }

  // The app whose store name is the game's name, 0 when Steam sells none;
  // -1 when the store could not be asked.
  long search(const std::string &name) {
    std::string body;
    if (!get("https://store.steampowered.com/api/storesearch/?l=english&cc=US&term=" + escaped(name), body))
      return -1;
    try {
      auto found = json5pp::parse(body);
      auto want = plainTitle(name);
      for (auto &g : found.as_object().at("items").as_array()) {
        auto it = g.as_object().find("name");
        if (it != g.as_object().end() && it->second.is_string() && plainTitle(it->second.as_string()) == want &&
            !want.empty())
          return long(g.as_object().at("id").as_number());
      }
      return 0;
    } catch (const std::exception &) {
      return -1;
    }
  }

  bool icon(long appid, const std::string &dir, const std::string &file) {
    if (file.empty() || fileExists(dir + file))
      return true;
    std::string bytes;
    if (!get("https://cdn.akamai.steamstatic.com/steamcommunity/public/images/apps/" + std::to_string(appid) + "/" + file,
             bytes))
      return false;
    // JPEG only, as Steam serves them; anything else is not kept.
    if (bytes.size() < 4 || bytes.size() > maxIcon || uint8_t(bytes[0]) != 0xff || uint8_t(bytes[1]) != 0xd8)
      return false;
    try {
      atomicWrite(dir + file, bytes);
      return true;
    } catch (const std::exception &) {
      return false;
    }
  }

  // True when the game's record changed.
  bool run(const Job &job) {
    const auto dir = root + job.game + "/";
    auto record = readRecord(dir);
    long appid = record.appid;
    std::vector<SteamAchievement> rows = record.rows;
    std::string lang = record.language.empty() ? language : record.language;
    bool fresh = !record.present || job.force;
    if (fresh) {
      appid = job.appid > 0 ? job.appid : search(job.name);
      if (appid < 0 || cancel)
        return false; // offline: try again next time
      rows.clear();
      lang = language;
      if (appid > 0) {
        std::string page;
        if (!get("https://steamcommunity.com/achievements/" + std::to_string(appid) + "?l=" + lang, page) ||
            !parseSteamAchievementPage(page, appid, rows))
          return false;
      }
    } else if (!record.incomplete || appid <= 0)
      return false;
    makeDir(root.substr(0, root.size() - 1));
    makeDir(dir.substr(0, dir.size() - 1));
    bool complete = true;
    if (!rows.empty()) {
      makeDir(dir + "icons");
      for (auto &a : rows) {
        if (cancel)
          return false;
        complete = icon(appid, dir + "icons/", a.icon) && complete;
        complete = icon(appid, dir + "icons/", a.iconLocked) && complete;
      }
    }
    try {
      atomicWrite(dir + "steam.json", steamCatalogJson(appid, lang, rows, !complete));
    } catch (const std::exception &) {
      return false;
    }
    return true;
  }

  void run() {
    // Brings the network up itself: the artwork lookup before it takes its
    // own down when it finishes.
    NetSession session;
    curl = session.online() ? curl_easy_init() : nullptr;
    for (size_t i = 0; i < jobs.size() && !cancel; ++i) {
      bool changed = curl && run(jobs[i]);
      std::lock_guard<std::mutex> hold(lock);
      ++progress.done;
      if (changed) {
        ++progress.found;
        progress.updated.push_back(jobs[i].game);
      }
    }
    if (curl)
      curl_easy_cleanup(curl);
    curl = nullptr;
    {
      std::lock_guard<std::mutex> hold(lock);
      progress.finished = true;
    }
    done = true;
  }
};

AchievementFetch::~AchievementFetch() { cancel(); }

void AchievementFetch::start(std::vector<Job> jobs, const std::string &root, const std::string &language) {
  cancel();
  state = std::make_shared<State>();
  state->jobs = std::move(jobs);
  state->root = root;
  state->language = language;
  state->progress.total = int(state->jobs.size());
  auto owned = state;
  // The thread keeps the state alive when this object goes first.
  std::thread([owned] { owned->run(); }).detach();
}

bool AchievementFetch::running() const { return state && !state->done; }

AchievementFetch::Progress AchievementFetch::poll() {
  if (!state)
    return {};
  std::lock_guard<std::mutex> hold(state->lock);
  auto p = state->progress;
  state->progress.updated.clear();
  return p;
}

void AchievementFetch::cancel() {
  if (state)
    state->cancel = true;
}
} // namespace hardrpg
