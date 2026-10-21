#include "art_fetch.h"
#include "image.h"
#include "util/json5pp.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <ctime>
#include <curl/curl.h>
#include <sys/stat.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#else
#include <thread>
#endif

namespace hardrpg {
namespace {
constexpr size_t maxDownload = 8 * 1024 * 1024;
const char *const kinds[] = {"cover", "hero", "logo"};

// Lower case letters and digits only, so "LISA: The Painful" and
// "Lisa the painful" compare equal.
std::string plain(const std::string &s) {
  std::string out;
  for (unsigned char c : s)
    if (std::isalnum(c))
      out += char(std::tolower(c));
  return out;
}
// The name to search for: without bracketed notes or version numbers.
std::string searchTerm(const std::string &name) {
  std::string out;
  int depth = 0;
  for (char c : name) {
    if (c == '(' || c == '[')
      ++depth;
    else if ((c == ')' || c == ']') && depth)
      --depth;
    else if (!depth)
      out += c;
  }
  for (const char *marker : {" v1", " v2", " v0", " ver", " Ver", " V1", " V2"}) {
    auto at = out.find(marker);
    if (at != std::string::npos && at > 2)
      out.resize(at);
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '-' || out.back() == '_'))
    out.pop_back();
  return out.empty() ? name : out;
}
bool exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}
void makeDirs(const std::string &dir) {
  for (size_t at = dir.find('/', 1); at != std::string::npos; at = dir.find('/', at + 1))
    mkdir(dir.substr(0, at).c_str(), 0777);
}
// Text for JSON: quotes, backslashes and control characters escaped.
std::string quoted(const std::string &s) {
  std::string out = "\"";
  for (unsigned char c : s) {
    if (c == '"' || c == '\\')
      out += '\\', out += char(c);
    else if (c == '\n')
      out += "\\n";
    else if (c >= 32)
      out += char(c);
  }
  return out + "\"";
}
// VNDB descriptions use [url=...]text[/url] style markup; keep the text.
std::string plainText(const std::string &s, size_t limit) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '[') {
      auto end = s.find(']', i);
      if (end != std::string::npos && end - i < 200) {
        i = end;
        continue;
      }
    }
    out += s[i];
  }
  while (!out.empty() && (out.back() == '\n' || out.back() == ' '))
    out.pop_back();
  if (out.size() > limit) {
    out.resize(limit);
    // Do not cut a UTF-8 character in half.
    while (!out.empty() && (static_cast<unsigned char>(out.back()) & 0xc0) == 0x80)
      out.pop_back();
    if (!out.empty() && static_cast<unsigned char>(out.back()) >= 0xc0)
      out.pop_back();
    out += "...";
  }
  return out;
}
const json5pp::value *field(const json5pp::value &v, const char *name) {
  if (!v.is_object())
    return nullptr;
  auto &o = v.as_object();
  auto it = o.find(name);
  return it == o.end() ? nullptr : &it->second;
}
std::string text(const json5pp::value &v, const char *name) {
  auto *f = field(v, name);
  return f && f->is_string() ? f->as_string() : "";
}
size_t collect(char *data, size_t size, size_t count, void *out) {
  auto &bytes = *static_cast<std::string *>(out);
  size_t n = size * count;
  if (n > maxDownload - std::min(maxDownload, bytes.size()))
    return 0;
  bytes.append(data, n);
  return n;
}
} // namespace

namespace {
ArtFetch::Transport transport;
} // namespace

struct ArtFetch::State {
  std::vector<Job> jobs;
  std::string root, gridKey, rawgKey;
  bool strict = false;
  std::atomic<bool> cancel{false}, done{false};
  mutable std::mutex lock;
  Progress progress;
  std::unique_ptr<Choices> choices; // waiting to be taken by the UI
  bool chosen = false;
  int picked[3] = {-1, -1, -1};
  CURL *curl = nullptr;

  // What the sources found for one game: picture URLs and a few facts.
  struct Found {
    std::string urls[3];
    std::string heroFallback; // a small store banner, if nothing better turns up
    bool adult = false;       // the source rates the game (or picture) adults only
    std::string title, released, developer, publisher, genres, description, source;
    bool complete(const bool need[3]) const {
      for (int k = 0; k < 3; ++k)
        if (need[k] && urls[k].empty())
          return false;
      return !description.empty() || !released.empty();
    }
  };

  static int abort(void *self, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return static_cast<State *>(self)->cancel ? 1 : 0;
  }
  // GET (or POST, with a JSON body) url; false on any failure or a status
  // other than 200.
  bool get(const std::string &url, std::string &out, const std::string &bearer = "", const std::string &post = "") {
    out.clear();
    if (transport)
      return transport(url, post, out) && !cancel;
    curl_easy_reset(curl);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "HardRPG launcher (github.com/Asukate/mkxp-z-vita)");
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 5000L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 20000L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef __vita__
    curl_easy_setopt(curl, CURLOPT_CAINFO, "app0:/ca-certificates.crt");
#endif
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, abort);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_slist *headers = nullptr;
    if (!bearer.empty())
      headers = curl_slist_append(headers, ("Authorization: Bearer " + bearer).c_str());
    if (!post.empty()) {
      headers = curl_slist_append(headers, "Content-Type: application/json");
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post.c_str());
    }
    if (headers)
      curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    long status = 0;
    auto rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    return rc == CURLE_OK && status == 200 && !cancel;
  }
  std::string escaped(const std::string &s) {
    char *e = curl_easy_escape(curl, s.c_str(), int(s.size()));
    std::string out = e ? e : "";
    curl_free(e);
    return out;
  }
  // Picks the search result whose name matches; -1 when none does.
  int pick(const std::string &wanted, const std::vector<std::string> &names) {
    auto w = plain(wanted);
    for (size_t i = 0; i < names.size(); ++i)
      if (plain(names[i]) == w)
        return int(i);
    if (strict || w.size() < 4)
      return -1;
    for (size_t i = 0; i < names.size(); ++i) {
      auto n = plain(names[i]);
      if (n.size() >= 4 && (n.find(w) != std::string::npos || w.find(n) != std::string::npos))
        return int(i);
    }
    return -1;
  }
  // Downloads url and saves it as dir/kind.<ext> if it is a picture we can read.
  bool save(const std::string &url, const std::string &dir, const char *kind) {
    std::string bytes;
    if (url.empty() || !get(url, bytes))
      return false;
    std::vector<uint8_t> data(bytes.begin(), bytes.end());
    if (Image::decode(data).empty())
      return false;
    const char *ext = data[0] == 0x89 ? ".png" : data[0] == 0xff ? ".jpg" : ".webp";
    return write(dir, std::string(kind) + ext, bytes);
  }
  bool write(const std::string &dir, const std::string &name, const std::string &bytes) {
    makeDirs(dir);
    auto path = dir + name, temp = path + ".part";
    FILE *f = std::fopen(temp.c_str(), "wb");
    bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f)
      ok = std::fclose(f) == 0 && ok;
    if (ok)
      std::remove(path.c_str()); // the Vita will not rename over a file
    ok = ok && std::rename(temp.c_str(), path.c_str()) == 0;
    if (!ok)
      std::remove(temp.c_str());
    return ok;
  }
  template <class F> void take(std::string &slot, F &&value) {
    if (slot.empty())
      slot = value();
  }

  // SteamGridDB: covers, backgrounds and logos for almost any game. Needs a key.
  void steamGrid(const std::string &term, const bool need[3], Found &f) {
    std::string body;
    if (gridKey.empty() || !get("https://www.steamgriddb.com/api/v2/search/autocomplete/" + escaped(term), body, gridKey))
      return;
    std::vector<std::string> names;
    std::vector<int> ids;
    try {
      auto found = json5pp::parse(body);
      for (auto &g : found.as_object().at("data").as_array()) {
        names.push_back(text(g, "name"));
        ids.push_back(g.as_object().at("id").as_integer());
      }
    } catch (...) {
      return;
    }
    int at = pick(term, names);
    if (at < 0)
      return;
    auto first = [&](const std::string &url) -> std::string {
      std::string list;
      if (!get(url, list, gridKey))
        return "";
      try {
        auto v = json5pp::parse(list);
        for (auto &item : v.as_object().at("data").as_array())
          if (!text(item, "url").empty()) {
            if (auto *nsfw = field(item, "nsfw"))
              f.adult = f.adult || (nsfw->is_boolean() && nsfw->as_boolean());
            return text(item, "url");
          }
      } catch (...) {
      }
      return "";
    };
    const std::string api = "https://www.steamgriddb.com/api/v2/", id = std::to_string(ids[at]);
    if (need[0])
      take(f.urls[0], [&] { return first(api + "grids/game/" + id + "?dimensions=600x900,342x482,660x930&types=static&nsfw=any"); });
    if (need[1])
      take(f.urls[1], [&] { return first(api + "heroes/game/" + id + "?types=static&nsfw=false"); });
    if (need[2])
      take(f.urls[2], [&] { return first(api + "logos/game/" + id + "?types=static&mimes=image/png&nsfw=false"); });
    take(f.title, [&] { return names[at]; });
    take(f.source, [] { return std::string("SteamGridDB"); });
  }

  // Steam: library art and a short description for games sold on Steam.
  void steam(const std::string &term, Found &f) {
    std::string body;
    if (!get("https://store.steampowered.com/api/storesearch/?l=english&cc=US&term=" + escaped(term), body))
      return;
    std::vector<std::string> names;
    std::vector<int> ids;
    try {
      auto found = json5pp::parse(body);
      for (auto &g : found.as_object().at("items").as_array()) {
        names.push_back(text(g, "name"));
        ids.push_back(g.as_object().at("id").as_integer());
      }
    } catch (...) {
      return;
    }
    int at = pick(term, names);
    if (at < 0)
      return;
    // Steam's own list of the app's pictures: newer apps keep them under
    // hashed folders, so the file names cannot be guessed.
    auto app = std::to_string(ids[at]);
    auto query = "{\"ids\":[{\"appid\":" + app +
                 "}],\"context\":{\"language\":\"english\",\"country_code\":\"US\"},"
                 "\"data_request\":{\"include_assets\":true,\"include_basic_info\":true,\"include_release\":true}}";
    if (!get("https://api.steampowered.com/IStoreBrowseService/GetItems/v1/?input_json=" + escaped(query), body))
      return;
    try {
      auto found = json5pp::parse(body);
      auto &items = found.as_object().at("response").as_object().at("store_items").as_array();
      if (items.empty())
        return;
      auto &item = items[0];
      if (auto *assets = field(item, "assets")) {
        auto format = text(*assets, "asset_url_format");
        auto url = [&](std::initializer_list<const char *> names) -> std::string {
          for (auto *name : names) {
            auto file = text(*assets, name);
            auto at = format.find("${FILENAME}");
            if (file.empty() || at == std::string::npos)
              continue;
            auto path = format;
            path.replace(at, 11, file);
            return "https://shared.akamai.steamstatic.com/store_item_assets/" + path;
          }
          return "";
        };
        take(f.urls[0], [&] { return url({"library_capsule_2x", "library_capsule"}); });
        take(f.urls[1], [&] { return url({"library_hero", "library_hero_2x"}); });
        f.heroFallback = url({"header_2x", "header"});
        // The list rarely names the logo; most apps keep it at a fixed path.
        take(f.urls[2], [&] {
          auto u = url({"library_logo", "library_logo_2x"});
          return u.empty() ? "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps/" + app + "/logo.png" : u;
        });
      }
      take(f.title, [&] { return names[at]; });
      // Content descriptors 3 and 4: adult-only or frequent sexual content.
      if (auto *rating = field(item, "content_descriptorids"))
        if (rating->is_array())
          for (auto &d : rating->as_array())
            if (d.is_number() && (d.as_number() == 3 || d.as_number() == 4))
              f.adult = true;
      if (auto *info = field(item, "basic_info")) {
        take(f.description, [&] { return plainText(text(*info, "short_description"), 600); });
        if (auto *devs = field(*info, "developers"))
          if (devs->is_array() && !devs->as_array().empty())
            take(f.developer, [&] { return text(devs->as_array()[0], "name"); });
        if (auto *pubs = field(*info, "publishers"))
          if (pubs->is_array() && !pubs->as_array().empty())
            take(f.publisher, [&] { return text(pubs->as_array()[0], "name"); });
      }
      if (auto *release = field(item, "release"))
        if (auto *when = field(*release, "steam_release_date"))
          if (when->is_number())
            take(f.released, [&] {
              std::time_t t = std::time_t(when->as_number());
              char buf[8];
              std::strftime(buf, sizeof buf, "%Y", std::gmtime(&t));
              return std::string(buf);
            });
      take(f.source, [] { return std::string("Steam"); });
    } catch (...) {
    }
  }

  // RAWG: release dates, genres, developers and a wide background picture.
  // Needs a key.
  void rawg(const std::string &term, Found &f) {
    std::string body;
    if (rawgKey.empty() ||
        !get("https://api.rawg.io/api/games?page_size=5&key=" + escaped(rawgKey) + "&search=" + escaped(term), body))
      return;
    try {
      auto found = json5pp::parse(body);
      auto &results = found.as_object().at("results").as_array();
      std::vector<std::string> names;
      for (auto &g : results)
        names.push_back(text(g, "name"));
      int at = pick(term, names);
      if (at < 0)
        return;
      auto &g = results[at];
      if (auto *esrb = field(g, "esrb_rating"))
        f.adult = text(*esrb, "slug") == "adults-only";
      take(f.urls[1], [&] { return text(g, "background_image"); });
      take(f.title, [&] { return names[at]; });
      take(f.released, [&] { return text(g, "released").substr(0, 4); });
      if (auto *genres = field(g, "genres"))
        if (genres->is_array()) {
          std::string list;
          for (auto &x : genres->as_array())
            list += (list.empty() ? "" : ", ") + text(x, "name");
          take(f.genres, [&] { return list; });
        }
      auto *id = field(g, "id");
      if (id && id->is_number() &&
          get("https://api.rawg.io/api/games/" + std::to_string(int(id->as_number())) + "?key=" + escaped(rawgKey), body)) {
        auto more = json5pp::parse(body);
        take(f.description, [&] { return plainText(text(more, "description_raw"), 600); });
        if (auto *devs = field(more, "developers"))
          if (devs->is_array() && !devs->as_array().empty())
            take(f.developer, [&] { return text(devs->as_array()[0], "name"); });
        if (auto *pubs = field(more, "publishers"))
          if (pubs->is_array() && !pubs->as_array().empty())
            take(f.publisher, [&] { return text(pubs->as_array()[0], "name"); });
      }
      take(f.source, [] { return std::string("RAWG"); });
    } catch (...) {
    }
  }

  // VNDB: free and keyless; knows many Japanese and doujin games, including
  // RPG Maker horror and adventure games. Its search is loose and its
  // catalogue is mostly visual novels on other engines, so a title is only
  // taken when its name matches exactly and its releases do not name an
  // engine other than RPG Maker.
  void vndb(const std::string &term, Found &f) {
    std::string body;
    auto query = "{\"filters\":[\"search\",\"=\"," + quoted(term) +
                 "],\"fields\":\"id,title,alttitle,released,description,developers.name,image.url,image.sexual,"
                 "screenshots.url,screenshots.sexual\",\"sort\":\"searchrank\",\"results\":5}";
    if (!get("https://api.vndb.org/kana/vn", body, "", query))
      return;
    try {
      auto found = json5pp::parse(body);
      auto &results = found.as_object().at("results").as_array();
      std::vector<std::string> names;
      std::vector<size_t> owner;
      for (size_t i = 0; i < results.size(); ++i)
        for (const char *key : {"title", "alttitle"})
          if (!text(results[i], key).empty()) {
            names.push_back(text(results[i], key));
            owner.push_back(i);
          }
      bool loose = strict;
      strict = true;
      int at = pick(term, names);
      strict = loose;
      if (at < 0)
        return;
      auto &g = results[owner[at]];
      // Which engines its releases were made with, and their age ratings.
      std::string releases;
      auto rq = "{\"filters\":[\"vn\",\"=\",[\"id\",\"=\"," + quoted(text(g, "id")) +
                "]],\"fields\":\"engine,minage\",\"results\":50}";
      if (!get("https://api.vndb.org/kana/release", releases, "", rq))
        return;
      bool rpgMaker = false, otherEngine = false;
      auto list = json5pp::parse(releases);
      for (auto &r : list.as_object().at("results").as_array()) {
        auto engine = plain(text(r, "engine"));
        if (engine.find("rpg") != std::string::npos || engine.find("tsukuru") != std::string::npos)
          rpgMaker = true;
        else if (!engine.empty())
          otherEngine = true;
        if (auto *age = field(r, "minage"))
          if (age->is_number() && age->as_number() >= 18)
            f.adult = true;
      }
      if (otherEngine && !rpgMaker)
        return;
      auto explicitImage = [](const json5pp::value &img) {
        auto *sexual = field(img, "sexual");
        return sexual && sexual->is_number() && sexual->as_number() >= 1;
      };
      if (auto *image = field(g, "image")) {
        take(f.urls[0], [&] { return text(*image, "url"); });
        f.adult = f.adult || explicitImage(*image);
      }
      // A background: the first screenshot that is not explicit.
      if (auto *shots = field(g, "screenshots"))
        if (shots->is_array())
          for (auto &shot : shots->as_array())
            if (!explicitImage(shot)) {
              take(f.urls[1], [&] { return text(shot, "url"); });
              break;
            }
      take(f.title, [&] { return text(g, "title"); });
      take(f.released, [&] { return text(g, "released").substr(0, 4); });
      take(f.description, [&] { return plainText(text(g, "description"), 600); });
      if (auto *devs = field(g, "developers"))
        if (devs->is_array() && !devs->as_array().empty())
          take(f.developer, [&] { return text(devs->as_array()[0], "name"); });
      take(f.source, [] { return std::string("VNDB"); });
    } catch (...) {
    }
  }

  // Asks the sources about one game. Each answers separately so the results
  // can be blended field by field, or offered side by side.
  std::vector<Found> gather(const std::string &name, bool everything) {
    std::vector<Found> all;
    auto term = searchTerm(name);
    const bool need[3] = {true, true, true};
    auto ask = [&](const std::function<void(Found &)> &source) {
      if (cancel)
        return;
      Found f;
      source(f);
      if (!f.source.empty())
        all.push_back(std::move(f));
    };
    ask([&](Found &f) { steamGrid(term, need, f); });
    ask([&](Found &f) { steam(term, f); });
    ask([&](Found &f) { rawg(term, f); });
    // VNDB is slow to search and rarely needed when another source knew the game.
    if (everything || all.empty() || pickUrl(all, 0).empty())
      ask([&](Found &f) { vndb(term, f); });
    return all;
  }
  static const Found *from(const std::vector<Found> &all, const char *source) {
    for (auto &f : all)
      if (f.source == source)
        return &f;
    return nullptr;
  }
  // The preferred source for each picture.
  static std::string pickUrl(const std::vector<Found> &all, int kind) {
    static const char *order[3][4] = {{"SteamGridDB", "Steam", "VNDB", ""},
                                      {"SteamGridDB", "Steam", "RAWG", "VNDB"},
                                      {"SteamGridDB", "Steam", "", ""}};
    for (auto *name : order[kind])
      if (auto *f = from(all, name))
        if (!f->urls[kind].empty())
          return f->urls[kind];
    if (kind == 1)
      if (auto *f = from(all, "Steam"))
        return f->heroFallback;
    return "";
  }
  // Facts, each from the source that knows it best: RAWG for dates and
  // credits, Steam for the store description.
  static std::string infoJson(const std::vector<Found> &all) {
    auto best = [&](std::string Found::*member, std::initializer_list<const char *> order) {
      for (auto *name : order)
        if (auto *f = from(all, name))
          if (!(f->*member).empty())
            return f->*member;
      return std::string();
    };
    std::string sources;
    bool adult = false;
    for (auto &f : all) {
      sources += (sources.empty() ? "" : ", ") + f.source;
      adult = adult || f.adult;
    }
    return "{\n  \"title\": " + quoted(best(&Found::title, {"Steam", "RAWG", "VNDB", "SteamGridDB"})) +
           ",\n  \"released\": " + quoted(best(&Found::released, {"RAWG", "Steam", "VNDB"})) +
           ",\n  \"developer\": " + quoted(best(&Found::developer, {"RAWG", "Steam", "VNDB"})) +
           ",\n  \"publisher\": " + quoted(best(&Found::publisher, {"RAWG", "Steam"})) +
           ",\n  \"genres\": " + quoted(best(&Found::genres, {"RAWG"})) +
           ",\n  \"description\": " + quoted(best(&Found::description, {"Steam", "RAWG", "VNDB"})) +
           ",\n  \"adult\": " + (adult ? "true" : "false") + ",\n  \"sources\": " + quoted(sources) + "\n}\n";
  }
  // Where a game's picture of this kind is now, if it has one.
  std::string current(const Job &job, const char *kind) {
    for (auto &dir : job.existing)
      for (const char *ext : {".png", ".jpg", ".jpeg", ".webp"})
        if (exists(root + dir + "/" + kind + ext))
          return root + dir + "/" + kind + ext;
    return "";
  }
  static Image thumbnail(const Image &img, int kind) {
    if (img.empty())
      return img;
    if (kind == 0)
      return img.cover(96, 144);
    if (kind == 1)
      return img.cover(192, 108);
    double k = std::min(192.0 / img.width, 72.0 / img.height);
    return img.resized(std::max(1, int(img.width * k)), std::max(1, int(img.height * k)));
  }

  // The whole library: blend the sources and fill only what is missing.
  bool fillIn(const Job &job) {
    bool need[3], info = true, found = false;
    for (int k = 0; k < 3; ++k)
      need[k] = current(job, kinds[k]).empty();
    for (auto &dir : job.existing)
      if (exists(root + dir + "/info.json"))
        info = false;
    if (!need[0] && !need[1] && !need[2] && !info)
      return false;
    auto all = gather(job.name, false);
    auto dir = root + job.folder + "/";
    for (int k = 0; k < 3; ++k)
      if (need[k] && !cancel)
        found = save(pickUrl(all, k), dir, kinds[k]) || found;
    if (info && !cancel && !all.empty())
      found = write(dir, "info.json", infoJson(all)) || found;
    return found;
  }

  // One game: every source's pictures are offered beside the current one.
  bool choose(const Job &job) {
    auto all = gather(job.name, true);
    if (all.empty() || cancel)
      return false;
    Choices offer;
    offer.key = job.key;
    offer.name = job.name;
    std::vector<std::string> bytes[3], paths[3];
    for (int k = 0; k < 3; ++k) {
      auto now = current(job, kinds[k]);
      std::string nowBytes;
      if (!now.empty())
        if (FILE *in = std::fopen(now.c_str(), "rb")) {
          char buf[65536];
          for (size_t n; (n = std::fread(buf, 1, sizeof buf, in)) > 0;)
            nowBytes.append(buf, n);
          std::fclose(in);
        }
      if (!now.empty()) {
        offer.options[k].push_back({"Current", thumbnail(Image::load(now), k), true});
        bytes[k].push_back("");
        paths[k].push_back(now);
      }
      std::vector<std::string> seen;
      for (auto &f : all) {
        auto url = f.urls[k].empty() && k == 1 && f.source == "Steam" ? f.heroFallback : f.urls[k];
        std::string data;
        if (url.empty() || std::find(seen.begin(), seen.end(), url) != seen.end() || cancel || !get(url, data))
          continue;
        seen.push_back(url);
        if (data == nowBytes)
          continue; // the picture the game already has
        Image img = Image::decode(std::vector<uint8_t>(data.begin(), data.end()));
        if (img.empty())
          continue;
        offer.options[k].push_back({f.source, thumbnail(img, k), false, f.adult});
        bytes[k].push_back(std::move(data));
        paths[k].push_back("");
      }
    }
    // Ask only when there is a real choice; otherwise take what was found.
    bool ask = false;
    int pick[3];
    for (int k = 0; k < 3; ++k) {
      pick[k] = offer.options[k].empty() ? -1 : 0;
      ask = ask || offer.options[k].size() > 1;
    }
    if (ask) {
      {
        std::lock_guard<std::mutex> hold(lock);
        choices.reset(new Choices(std::move(offer)));
        progress.choosing = true;
      }
      while (!cancel) {
        {
          std::lock_guard<std::mutex> hold(lock);
          if (chosen) {
            for (int k = 0; k < 3; ++k)
              pick[k] = picked[k];
            break;
          }
        }
#ifdef __vita__
        sceKernelDelayThread(50000);
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
      }
      std::lock_guard<std::mutex> hold(lock);
      progress.choosing = false;
      if (cancel)
        return false;
    }
    bool found = false;
    for (int k = 0; k < 3; ++k) {
      if (pick[k] < 0 || pick[k] >= int(bytes[k].size()) || bytes[k][pick[k]].empty())
        continue; // nothing, or the current picture kept
      // Replace in the folder the game's art is read from.
      auto now = paths[k].empty() ? "" : paths[k][0];
      auto dir = now.empty() ? root + job.folder + "/" : now.substr(0, now.find_last_of('/') + 1);
      for (const char *ext : {".png", ".jpg", ".jpeg", ".webp"})
        std::remove((dir + kinds[k] + ext).c_str());
      auto &data = bytes[k][pick[k]];
      const char *ext = uint8_t(data[0]) == 0x89 ? ".png" : uint8_t(data[0]) == 0xff ? ".jpg" : ".webp";
      found = write(dir, std::string(kinds[k]) + ext, data) || found;
    }
    // The facts are refreshed whenever a single game is looked up.
    std::string infoDir = root + job.folder + "/";
    for (auto &dir : job.existing)
      if (exists(root + dir + "/info.json")) {
        infoDir = root + dir + "/";
        break;
      }
    found = write(infoDir, "info.json", infoJson(all)) || found;
    return found;
  }

  void run() {
#ifdef __vita__
    bool module = sceSysmoduleLoadModule(SCE_SYSMODULE_NET) == 0;
    void *pool = std::malloc(1024 * 1024);
    SceNetInitParam param{};
    param.memory = pool;
    param.size = 1024 * 1024;
    // Another part of the launcher may already have the network up.
    bool net = pool && sceNetInit(&param) == 0;
    bool ctl = sceNetCtlInit() == 0;
    int connection = 0;
    bool online = sceNetCtlInetGetState(&connection) == 0 && connection == SCE_NETCTL_STATE_CONNECTED;
#else
    bool online = true;
#endif
    if (!online)
      note("Not connected to the internet.");
    else if (curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK) {
      curl = curl_easy_init();
      if (curl) {
        for (auto &job : jobs) {
          if (cancel)
            break;
          {
            std::lock_guard<std::mutex> hold(lock);
            progress.current = job.name;
          }
          bool found = strict ? fillIn(job) : choose(job);
          std::lock_guard<std::mutex> hold(lock);
          ++progress.done;
          if (found) {
            ++progress.found;
            progress.updated.push_back(job.key);
          }
        }
        curl_easy_cleanup(curl);
      }
      curl_global_cleanup();
    }
#ifdef __vita__
    if (ctl)
      sceNetCtlTerm();
    if (net)
      sceNetTerm();
    std::free(pool);
    if (module && net)
      sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
#endif
    std::lock_guard<std::mutex> hold(lock);
    progress.finished = true;
    done = true;
  }
  void note(const std::string &text) {
    std::lock_guard<std::mutex> hold(lock);
    progress.note = text;
  }
#ifdef __vita__
  static int entry(SceSize, void *argument) {
    auto holder = *static_cast<std::shared_ptr<State> **>(argument);
    auto self = *holder;
    delete holder;
    self->run();
    self.reset();
    sceKernelExitDeleteThread(0);
    return 0;
  }
#endif
};

namespace {
// A key file holds one line; anything but letters and digits is dropped.
std::string readKey(const std::string &path) {
  std::string key;
  if (FILE *f = std::fopen(path.c_str(), "rb")) {
    char buf[128] = {};
    std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    for (char *c = buf; *c; ++c)
      if (std::isalnum(static_cast<unsigned char>(*c)))
        key += *c;
  }
  return key;
}
} // namespace

ArtFetch::~ArtFetch() { cancel(); }
void ArtFetch::start(std::vector<Job> jobs, const std::string &artRoot, const std::string &dataRoot, bool strict) {
  if (running())
    return;
  state = std::make_shared<State>();
  state->jobs = std::move(jobs);
  state->root = artRoot;
  state->strict = strict;
  state->progress.total = int(state->jobs.size());
  state->gridKey = readKey(dataRoot + "steamgriddb-key.txt");
  state->rawgKey = readKey(dataRoot + "rawg-key.txt");
#ifdef __vita__
  auto holder = new std::shared_ptr<State>(state);
  auto thread = sceKernelCreateThread("hardrpg_art_fetch", State::entry, 0x10000100, 256 * 1024, 0, 0, nullptr);
  if (thread < 0 || sceKernelStartThread(thread, sizeof(holder), &holder) < 0) {
    if (thread >= 0)
      sceKernelDeleteThread(thread);
    delete holder;
    state->progress.finished = true;
    state->progress.note = "Could not start the download.";
    state->done = true;
  }
#else
  auto owned = state;
  std::thread([owned] { owned->run(); }).detach();
#endif
}
bool ArtFetch::running() const { return state && !state->done; }
ArtFetch::Progress ArtFetch::poll() {
  if (!state)
    return Progress{};
  std::lock_guard<std::mutex> hold(state->lock);
  Progress p = state->progress;
  state->progress.updated.clear();
  return p;
}
void ArtFetch::setTransport(Transport t) { transport = std::move(t); }
bool ArtFetch::takeChoices(Choices &out) {
  if (!state)
    return false;
  std::lock_guard<std::mutex> hold(state->lock);
  if (!state->choices)
    return false;
  out = std::move(*state->choices);
  state->choices.reset();
  return true;
}
void ArtFetch::choose(const int pick[3]) {
  if (!state)
    return;
  std::lock_guard<std::mutex> hold(state->lock);
  for (int k = 0; k < 3; ++k)
    state->picked[k] = pick[k];
  state->chosen = true;
}
void ArtFetch::cancel() {
  if (state)
    state->cancel = true;
}
} // namespace hardrpg
