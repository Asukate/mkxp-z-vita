#include "art_fetch.h"
#include "image.h"
#include "util/json5pp.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <ctime>
#include <curl/curl.h>
#include <dirent.h>
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
// "Lisa the painful" compare equal. Accented Latin letters count as their
// plain ones ("Pokémon" is "pokemon"); other scripts are left out.
std::string plain(const std::string &s) {
  // U+00C0 to U+00FF, without the accent.
  static const char latin[] = "aaaaaaaceeeeiiiidnooooo*ouuuuyts"
                              "aaaaaaaceeeeiiiidnooooo/ouuuuyty";
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    unsigned char c = s[i];
    if (std::isalnum(c))
      out += char(std::tolower(c));
    else if (c == 0xc3 && i + 1 < s.size()) {
      unsigned char d = s[++i];
      if (d >= 0x80 && d <= 0xbf) {
        char base = latin[d - 0x80];
        if (std::isalpha((unsigned char)base))
          out += base;
      }
    }
  }
  return out;
}
// Names to search for, best first. Game folders and zips are often named
// "YumeNikki.zip", "Mogeko Castle - English VX Ace" or "Game_v1.2 [ENG]":
// the extension, bracketed notes, version numbers and tags such as the
// language or the engine are dropped, and joined words are split.
std::vector<std::string> searchTerms(const std::string &name) {
  auto lower = [](std::string s) {
    for (auto &c : s)
      c = char(std::tolower((unsigned char)c));
    return s;
  };
  std::string base = name;
  for (const char *ext : {".zip", ".7z", ".rar", ".exe"}) {
    size_t n = std::strlen(ext);
    if (base.size() > n && lower(base.substr(base.size() - n)) == ext)
      base.resize(base.size() - n);
  }
  // Folder names like "to-the-moon": hyphens separate the words.
  if (base.find(' ') == std::string::npos && base.find('-') != std::string::npos)
    std::replace(base.begin(), base.end(), '-', ' ');
  // Words, without bracketed notes; '_' separates words too.
  std::vector<std::string> words;
  std::string word;
  int depth = 0;
  for (char c : base + " ") {
    if (c == '(' || c == '[' || c == '{')
      ++depth;
    else if ((c == ')' || c == ']' || c == '}') && depth)
      --depth;
    else if (depth)
      continue;
    else if (c == ' ' || c == '_') {
      if (!word.empty())
        words.push_back(word);
      word.clear();
    } else
      word += c;
  }
  // Stop at a version ("v1.2", "Ver. 1.089", "version 3") and leave out tags.
  static const char *const tags[] = {"english", "eng", "translated", "translation", "patched", "uncensored",
                                     "vx", "ace", "vxace", "xp", "mv", "mz", "rpgmaker", "rpgm", "2k", "2k3"};
  std::string out;
  for (size_t i = 0; i < words.size(); ++i) {
    auto w = lower(words[i]);
    bool version = (w.size() > 1 && w[0] == 'v' && std::isdigit((unsigned char)w[1])) || w == "ver" ||
                   w == "ver." || w == "version" || (w.rfind("ver.", 0) == 0 && w.size() > 4);
    if (version && i > 0)
      break;
    std::string bare;
    for (char c : w)
      if (std::isalnum((unsigned char)c))
        bare += c;
    bool tag = false;
    for (auto *t : tags)
      tag = tag || bare == t;
    if (tag && i > 0)
      continue;
    out += (out.empty() ? "" : " ") + words[i];
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '-' || out.back() == ':'))
    out.pop_back();
  if (out.empty())
    out = base;
  std::vector<std::string> terms{out};
  auto add = [&](const std::string &t) {
    if (!t.empty() && std::find(terms.begin(), terms.end(), t) == terms.end())
      terms.push_back(t);
  };
  // "YumeNikki" -> "Yume Nikki".
  std::string split;
  for (size_t i = 0; i < out.size(); ++i) {
    if (i && std::isupper((unsigned char)out[i]) && std::islower((unsigned char)out[i - 1]))
      split += ' ';
    split += out[i];
  }
  add(split);
  // "N.P.C. Dreams" -> "NPC Dreams".
  std::string undotted;
  for (char c : split)
    if (c != '.')
      undotted += c;
  add(undotted);
  return terms;
}
// How alike two plain names are, from 0 to 1 (edit distance).
double likeness(const std::string &a, const std::string &b) {
  if (a.empty() || b.empty())
    return 0;
  std::vector<size_t> row(b.size() + 1);
  for (size_t j = 0; j <= b.size(); ++j)
    row[j] = j;
  for (size_t i = 1; i <= a.size(); ++i) {
    size_t diag = row[0];
    row[0] = i;
    for (size_t j = 1; j <= b.size(); ++j) {
      size_t up = row[j];
      row[j] = std::min({row[j] + 1, row[j - 1] + 1, diag + (a[i - 1] != b[j - 1])});
      diag = up;
    }
  }
  return 1.0 - double(row[b.size()]) / double(std::max(a.size(), b.size()));
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
// The game's own title screen, as file bytes: the fallback picture when no
// source knows the game. RPG Maker keeps it in Graphics/Titles (XP),
// Graphics/System/Title (VX) or Graphics/Titles1 (VX Ace), loose or inside
// the game's encrypted archive. Without the game's data to say which file
// the title uses, the largest picture there is taken. Games with a scripted
// title (VorlorN) keep it in Graphics/Pictures as "Title...": those count
// when there is nothing in the title folders.
std::string lowered(std::string s) {
  for (auto &c : s)
    c = char(std::tolower((unsigned char)c));
  std::replace(s.begin(), s.end(), '\\', '/');
  return s;
}
// 2: a picture in a title folder, 1: a "Title..." picture, 0: neither.
int titlePicture(const std::string &path) {
  auto p = lowered(path);
  auto ext = p.substr(p.find_last_of('.') == std::string::npos ? p.size() : p.find_last_of('.'));
  if (ext != ".png" && ext != ".jpg" && ext != ".jpeg")
    return 0;
  if (p.rfind("graphics/titles/", 0) == 0 || p.rfind("graphics/titles1/", 0) == 0 ||
      p.rfind("graphics/system/title.", 0) == 0)
    return 2;
  return p.rfind("graphics/pictures/title", 0) == 0 ? 1 : 0;
}
std::string readFile(const std::string &path, size_t limit) {
  std::string out;
  if (FILE *f = std::fopen(path.c_str(), "rb")) {
    char buf[65536];
    for (size_t n; out.size() < limit && (n = std::fread(buf, 1, sizeof buf, f)) > 0;)
      out.append(buf, n);
    std::fclose(f);
  }
  return out.size() <= limit ? out : std::string();
}
// The real name of a file or folder under dir, ignoring case; empty if none.
std::string entryNamed(const std::string &dir, const std::string &want) {
  std::string found;
  if (DIR *d = opendir(dir.c_str())) {
    while (auto *e = readdir(d))
      if (lowered(e->d_name) == lowered(want)) {
        found = e->d_name;
        break;
      }
    closedir(d);
  }
  return found;
}
std::string looseTitle(const std::string &game) {
  std::string best;
  size_t bestSize = 0;
  int bestRank = 0;
  auto graphics = entryNamed(game, "Graphics");
  if (graphics.empty())
    return "";
  for (const char *sub : {"Titles1", "Titles", "System", "Pictures"}) {
    auto folder = entryNamed(game + "/" + graphics, sub);
    if (folder.empty())
      continue;
    auto dir = game + "/" + graphics + "/" + folder;
    if (DIR *d = opendir(dir.c_str())) {
      while (auto *e = readdir(d)) {
        int rank = titlePicture(std::string("Graphics/") + sub + "/" + e->d_name);
        if (!rank)
          continue;
        struct stat st;
        auto path = dir + "/" + e->d_name;
        if (stat(path.c_str(), &st) == 0 && size_t(st.st_size) <= maxDownload &&
            (rank > bestRank || (rank == bestRank && size_t(st.st_size) > bestSize)))
          best = path, bestSize = size_t(st.st_size), bestRank = rank;
      }
      closedir(d);
    }
  }
  return best.empty() ? "" : readFile(best, maxDownload);
}
// Encrypted archives: Game.rgssad (XP), Game.rgss2a (VX), Game.rgss3a (VX
// Ace); some games rename them after the game. Both formats XOR names and
// data with a key that changes every four bytes (key * 7 + 3).
std::string archiveTitle(const std::string &path) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f)
    return "";
  std::string out;
  auto u32 = [&](uint32_t &v) {
    unsigned char b[4];
    if (std::fread(b, 1, 4, f) != 4)
      return false;
    v = uint32_t(b[0]) | uint32_t(b[1]) << 8 | uint32_t(b[2]) << 16 | uint32_t(b[3]) << 24;
    return true;
  };
  char magic[8];
  struct Entry {
    long offset;
    uint32_t size, key;
    int rank;
  } best{0, 0, 0, 0};
  auto better = [&](int rank, uint32_t size) {
    return rank && size <= maxDownload && (rank > best.rank || (rank == best.rank && size > best.size));
  };
  if (std::fread(magic, 1, 8, f) == 8 && std::memcmp(magic, "RGSSAD\0", 7) == 0) {
    if (magic[7] == 1) {
      uint32_t key = 0xDEADCAFE, n, size;
      for (int guard = 0; guard < 100000 && u32(n); ++guard) {
        n ^= key, key = key * 7 + 3;
        if (n == 0 || n > 512)
          break;
        std::string name(n, '\0');
        if (std::fread(&name[0], 1, n, f) != n)
          break;
        for (auto &c : name)
          c = char((unsigned char)c ^ (key & 0xff)), key = key * 7 + 3;
        if (!u32(size))
          break;
        size ^= key, key = key * 7 + 3;
        long offset = std::ftell(f);
        if (better(titlePicture(name), size))
          best = {offset, size, key, titlePicture(name)};
        if (std::fseek(f, long(size), SEEK_CUR) != 0)
          break;
      }
    } else if (magic[7] == 3) {
      uint32_t key, offset, size, start, n;
      if (u32(key)) {
        key = key * 9 + 3;
        for (int guard = 0; guard < 100000 && u32(offset); ++guard) {
          offset ^= key;
          if (offset == 0 || !u32(size) || !u32(start) || !u32(n))
            break;
          size ^= key, start ^= key, n ^= key;
          if (n == 0 || n > 512)
            break;
          std::string name(n, '\0');
          if (std::fread(&name[0], 1, n, f) != n)
            break;
          for (size_t i = 0; i < name.size(); ++i)
            name[i] = char((unsigned char)name[i] ^ ((key >> (8 * (i % 4))) & 0xff));
          if (better(titlePicture(name), size))
            best = {long(offset), size, start, titlePicture(name)};
        }
      }
    }
  }
  if (best.size && std::fseek(f, best.offset, SEEK_SET) == 0) {
    out.resize(best.size);
    if (std::fread(&out[0], 1, best.size, f) != best.size)
      out.clear();
    uint32_t key = best.key;
    for (size_t i = 0; i < out.size(); ++i) {
      out[i] = char((unsigned char)out[i] ^ ((key >> (8 * (i % 4))) & 0xff));
      if (i % 4 == 3)
        key = key * 7 + 3;
    }
  }
  std::fclose(f);
  return out;
}
std::string titleScreen(const std::string &game, bool deeper = true) {
  if (game.empty())
    return "";
  auto loose = looseTitle(game);
  if (!loose.empty())
    return loose;
  std::string bytes;
  if (DIR *d = opendir(game.c_str())) {
    while (auto *e = readdir(d)) {
      auto n = lowered(e->d_name);
      auto dot = n.find_last_of('.');
      auto ext = dot == std::string::npos ? "" : n.substr(dot);
      if (ext == ".rgssad" || ext == ".rgss2a" || ext == ".rgss3a") {
        bytes = archiveTitle(game + "/" + e->d_name);
        if (!bytes.empty())
          break;
      }
    }
    closedir(d);
  }
  // A ZIP often holds the game in a folder of its own.
  if (bytes.empty() && deeper)
    if (DIR *d = opendir(game.c_str())) {
      while (auto *e = readdir(d)) {
        if (e->d_name[0] == '.')
          continue;
        auto sub = game + "/" + e->d_name;
        struct stat st;
        if (stat(sub.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
          bytes = titleScreen(sub, false);
          if (!bytes.empty())
            break;
        }
      }
      closedir(d);
    }
  return bytes;
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
  std::string root, gridKey, rawgKey, igdbId, igdbSecret, igdbToken;
  bool igdbFailed = false;
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
    bool rpgMaker = false;    // the source says the game was made with RPG Maker
    std::string title, released, developer, publisher, genres, description, source;
    std::vector<std::string> shots; // screenshots, best first
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
  bool get(const std::string &url, std::string &out, const std::string &bearer = "", const std::string &post = "",
           const char *contentType = "application/json", const std::string &header = "") {
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
    if (!header.empty())
      headers = curl_slist_append(headers, header.c_str());
    if (!post.empty()) {
      headers = curl_slist_append(headers, (std::string("Content-Type: ") + contentType).c_str());
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
    // Close spellings: "Yume Nikki" and "Yume Nikki!", "Ib" remakes aside.
    int best = -1;
    double score = 0.8;
    for (size_t i = 0; i < names.size(); ++i) {
      double l = likeness(w, plain(names[i]));
      if (l >= score)
        best = int(i), score = l;
    }
    return best;
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
                 "\"data_request\":{\"include_assets\":true,\"include_basic_info\":true,\"include_release\":true,"
                 "\"include_screenshots\":true}}";
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
      // Screenshots, in their 600x338 size: "steam/apps/1/ss_x.jpg?t=2".
      if (auto *shots = field(item, "screenshots"))
        if (auto *list = field(*shots, "all_ages_screenshots"))
          if (list->is_array())
            for (auto &shot : list->as_array()) {
              auto file = text(shot, "filename");
              auto dot = file.find(".jpg");
              if (dot == std::string::npos || f.shots.size() >= 4)
                continue;
              file.insert(dot, ".600x338");
              f.shots.push_back("https://shared.akamai.steamstatic.com/store_item_assets/" + file);
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
      // The first "short screenshot" is the background again.
      if (auto *shots = field(g, "short_screenshots"))
        if (shots->is_array())
          for (size_t i = 1; i < shots->as_array().size() && f.shots.size() < 4; ++i)
            f.shots.push_back(text(shots->as_array()[i], "image"));
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
      // A background and screenshots: those that are not explicit.
      if (auto *shots = field(g, "screenshots"))
        if (shots->is_array())
          for (auto &shot : shots->as_array())
            if (!explicitImage(shot) && !text(shot, "url").empty()) {
              take(f.urls[1], [&] { return text(shot, "url"); });
              if (f.shots.size() < 4)
                f.shots.push_back(text(shot, "url"));
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

  // IGDB (Twitch): the widest catalogue of release details, with covers,
  // artwork and screenshots. Needs a Twitch app's client ID and secret.
  // Many games share a name ("Flow", "Ib", "To the Moon"), so among exact
  // matches the one made with RPG Maker wins, then the oldest.
  void igdb(const std::string &term, Found &f) {
    if (igdbId.empty() || igdbSecret.empty() || igdbFailed)
      return;
    std::string body;
    if (igdbToken.empty()) {
      if (!get("https://id.twitch.tv/oauth2/token", body, "",
               "client_id=" + escaped(igdbId) + "&client_secret=" + escaped(igdbSecret) + "&grant_type=client_credentials",
               "application/x-www-form-urlencoded")) {
        igdbFailed = true; // a wrong key: do not ask again for every game
        return;
      }
      try {
        igdbToken = text(json5pp::parse(body), "access_token");
      } catch (...) {
      }
      if (igdbToken.empty()) {
        igdbFailed = true;
        return;
      }
    }
    std::string name;
    for (char c : term)
      if (c != '"' && c != '\\')
        name += c;
    const std::string fields =
        " fields name,first_release_date,summary,cover.image_id,artworks.image_id,screenshots.image_id,"
        "involved_companies.company.name,involved_companies.developer,involved_companies.publisher,genres.name,"
        "themes,game_engines.name; limit 10;";
    // Its search leaves out some short or common names ("OFF", "To the
    // Moon"), so the exact name is asked for as well.
    std::vector<json5pp::value> results;
    std::vector<int64_t> ids;
    for (auto query : {"search \"" + name + "\";" + fields, "where name ~ \"" + name + "\";" + fields}) {
      if (!get("https://api.igdb.com/v4/games", body, igdbToken, query, "text/plain", "Client-ID: " + igdbId))
        continue;
      try {
        auto list = json5pp::parse(body);
        for (auto &g : list.as_array()) {
          auto *id = field(g, "id");
          int64_t n = id && id->is_number() ? int64_t(id->as_number()) : -1;
          if (std::find(ids.begin(), ids.end(), n) == ids.end()) {
            ids.push_back(n);
            results.push_back(g);
          }
        }
      } catch (...) {
      }
    }
    std::vector<std::string> names;
    for (auto &g : results)
      names.push_back(text(g, "name"));
    auto year = [](const json5pp::value &g) {
      auto *when = field(g, "first_release_date");
      if (!when || !when->is_number())
        return 9999;
      std::time_t t = std::time_t(when->as_number());
      return std::gmtime(&t)->tm_year + 1900;
    };
    auto rpgMaker = [](const json5pp::value &g) {
      if (auto *engines = field(g, "game_engines"))
        if (engines->is_array())
          for (auto &e : engines->as_array()) {
            auto n = plain(text(e, "name"));
            if (n.find("rpgmaker") != std::string::npos || n.find("tsukuru") != std::string::npos ||
                n.find("wolfrpg") != std::string::npos)
              return true;
          }
      return false;
    };
    int at = -1, best = 0;
    for (size_t i = 0; i < results.size(); ++i)
      if (plain(names[i]) == plain(term)) {
        int rank = (rpgMaker(results[i]) ? 0 : 10000) + year(results[i]);
        if (at < 0 || rank < best)
          at = int(i), best = rank;
      }
    if (at < 0)
      at = pick(term, names);
    if (at < 0)
      return;
    auto &g = results[at];
    f.rpgMaker = rpgMaker(g);
    auto image = [](const json5pp::value &img, const char *size) {
      auto id = text(img, "image_id");
      return id.empty() ? std::string() : "https://images.igdb.com/igdb/image/upload/t_" + std::string(size) + "/" + id + ".jpg";
    };
    if (auto *cover = field(g, "cover"))
      take(f.urls[0], [&] { return image(*cover, "cover_big_2x"); });
    for (const char *kind : {"artworks", "screenshots"})
      if (auto *list = field(g, kind))
        if (list->is_array() && !list->as_array().empty())
          take(f.urls[1], [&] { return image(list->as_array()[0], "1080p"); });
    if (auto *list = field(g, "screenshots"))
      if (list->is_array())
        for (auto &shot : list->as_array())
          if (f.shots.size() < 4)
            f.shots.push_back(image(shot, "screenshot_big"));
    // Theme 42 is "Erotic".
    if (auto *themes = field(g, "themes"))
      if (themes->is_array())
        for (auto &t : themes->as_array())
          if (t.is_number() && t.as_number() == 42)
            f.adult = true;
    take(f.title, [&] { return names[at]; });
    if (year(g) != 9999)
      take(f.released, [&] { return std::to_string(year(g)); });
    take(f.description, [&] { return plainText(text(g, "summary"), 600); });
    if (auto *companies = field(g, "involved_companies"))
      if (companies->is_array())
        for (auto &c : companies->as_array()) {
          auto *company = field(c, "company");
          auto *dev = field(c, "developer");
          auto *pub = field(c, "publisher");
          if (company && dev && dev->is_boolean() && dev->as_boolean())
            take(f.developer, [&] { return text(*company, "name"); });
          if (company && pub && pub->is_boolean() && pub->as_boolean())
            take(f.publisher, [&] { return text(*company, "name"); });
        }
    if (auto *genres = field(g, "genres"))
      if (genres->is_array()) {
        std::string list;
        for (auto &x : genres->as_array())
          list += (list.empty() ? "" : ", ") + text(x, "name");
        take(f.genres, [&] { return list; });
      }
    take(f.source, [] { return std::string("IGDB"); });
  }

  // DLsite: Japanese doujin games, many of them RPG Maker ones. No API: its
  // search page lists product IDs, and a small JSON reply describes each.
  // Searched on its adult, all-ages and game floors.
  void dlsite(const std::string &term, Found &f) {
    std::string words = escaped(term), page;
    for (size_t at; (at = words.find("%20")) != std::string::npos;)
      words.replace(at, 3, "+"); // DLsite refuses %20
    struct Hit {
      std::string site, id, title;
    };
    std::vector<Hit> hits;
    for (const char *site : {"maniax", "home", "soft"}) {
      if (cancel || !get("https://www.dlsite.com/" + std::string(site) + "/fsr/=/keyword/" + words + "/", page))
        continue;
      // Each result: product_id/RJ123456.html" title="Name"
      const std::string mark = "product_id/";
      for (size_t at = page.find(mark); at != std::string::npos; at = page.find(mark, at + 1)) {
        size_t id = at + mark.size(), end = page.find(".html\" title=\"", id);
        if (end == std::string::npos || end - id < 8 || end - id > 10 || (page[id] != 'R'))
          continue;
        size_t t = end + 14, close = page.find('"', t);
        if (close == std::string::npos || close - t > 300)
          continue;
        Hit h{site, page.substr(id, end - id), page.substr(t, close - t)};
        bool seen = false;
        for (auto &x : hits)
          seen = seen || x.id == h.id;
        if (!seen)
          hits.push_back(h);
      }
    }
    std::vector<std::string> names;
    for (auto &h : hits)
      names.push_back(h.title);
    int at = pick(term, names);
    if (at < 0)
      return;
    auto &h = hits[at];
    if (!get("https://www.dlsite.com/" + h.site + "/product/info/ajax?product_id=" + h.id + "&cdn_cache_min=1", page))
      return;
    try {
      auto all = json5pp::parse(page);
      auto *info = field(all, h.id.c_str());
      if (!info)
        return;
      auto picture = text(*info, "work_image"); // //img.dlsite.jp/.../RJ123456_img_main.jpg
      if (picture.rfind("//", 0) == 0)
        picture = "https:" + picture;
      take(f.urls[0], [&] { return picture; });
      f.heroFallback = picture;
      auto main = picture.find("_img_main");
      for (int i = 1; i <= 4 && main != std::string::npos; ++i)
        f.shots.push_back(picture.substr(0, main) + "_img_smp" + std::to_string(i) + picture.substr(main + 9));
      auto *age = field(*info, "age_category");
      f.adult = age && ((age->is_string() && age->as_string() == "3") || (age->is_number() && age->as_number() == 3));
      take(f.title, [&] { return text(*info, "work_name"); });
      take(f.released, [&] { return text(*info, "regist_date").substr(0, 4); });
      take(f.source, [] { return std::string("DLsite"); });
    } catch (...) {
    }
  }

  // itch.io: free indie games. Its search has no API and is full of fan
  // games and assets with the same names, so only an exact name counts.
  void itch(const std::string &term, Found &f) {
    std::string page;
    if (!get("https://itch.io/search?q=" + escaped(term), page))
      return;
    // Results: a cell per game (data-game_id) holding a lazily loaded cover
    // and a title link.
    const std::string cell = "data-game_id=\"";
    for (size_t at = page.find(cell); at != std::string::npos;) {
      size_t next = page.find(cell, at + 1);
      auto part = page.substr(at, (next == std::string::npos ? page.size() : next) - at);
      at = next;
      auto title = part.find("class=\"title game_link\"");
      if (title == std::string::npos)
        continue;
      auto open = part.rfind("<a ", title), close = part.find('>', title), end = part.find('<', close);
      if (open == std::string::npos || close == std::string::npos || end == std::string::npos)
        continue;
      auto name = part.substr(close + 1, end - close - 1);
      for (auto entity : {std::make_pair("&amp;", "&"), std::make_pair("&#39;", "'"), std::make_pair("&quot;", "\"")})
        for (size_t e; (e = name.find(entity.first)) != std::string::npos;)
          name.replace(e, std::strlen(entity.first), entity.second);
      if (plain(name) != plain(term) || plain(name).empty())
        continue;
      auto img = part.find("data-lazy_src=\"");
      std::string cover;
      if (img != std::string::npos && img < title)
        cover = part.substr(img + 15, part.find('"', img + 15) - img - 15);
      auto small = cover.find("/315x250%23c/");
      if (small != std::string::npos)
        cover.replace(small, 13, "/630x500%23c/");
      take(f.urls[0], [&] { return cover; });
      f.heroFallback = cover;
      take(f.title, [&] { return name; });
      take(f.source, [] { return std::string("itch.io"); });
      return;
    }
  }

  // Asks the sources about one game. Each answers separately so the results
  // can be blended field by field, or offered side by side.
  std::vector<Found> gather(const std::string &name, bool everything) {
    std::vector<Found> all;
    auto terms = searchTerms(name);
    const bool need[3] = {true, true, true};
    // Each source is asked with the first spelling, then the others until it
    // knows the game.
    auto ask = [&](const std::function<void(const std::string &, Found &)> &source) {
      for (auto &term : terms) {
        if (cancel)
          return;
        Found f;
        source(term, f);
        if (!f.source.empty()) {
          all.push_back(std::move(f));
          return;
        }
      }
    };
    ask([&](const std::string &t, Found &f) { steamGrid(t, need, f); });
    ask([&](const std::string &t, Found &f) { steam(t, f); });
    ask([&](const std::string &t, Found &f) { igdb(t, f); });
    ask([&](const std::string &t, Found &f) { dlsite(t, f); });
    ask([&](const std::string &t, Found &f) { rawg(t, f); });
    // VNDB is slow to search and rarely needed when another source knew the
    // game; itch.io is full of fan games with the same names, so it is asked
    // only when nothing else has a cover.
    if (everything || all.empty() || pickUrl(all, 0).empty())
      ask([&](const std::string &t, Found &f) { vndb(t, f); });
    if (all.empty() || pickUrl(all, 0).empty())
      ask([&](const std::string &t, Found &f) { itch(t, f); });
    // IGDB sometimes knows only another game of the same name. When a source
    // that matched the name gives a release year more than a year apart,
    // IGDB found the wrong game, unless IGDB's is an RPG Maker game (Steam
    // sells the 2022 remake of Ib; IGDB found the 2012 original).
    auto igdbAt = std::find_if(all.begin(), all.end(), [](const Found &f) { return f.source == "IGDB"; });
    if (igdbAt != all.end() && !igdbAt->released.empty() && !igdbAt->rpgMaker)
      for (auto &f : all)
        if (&f != &*igdbAt && f.released.size() == 4 && std::abs(std::atoi(f.released.c_str()) - std::atoi(igdbAt->released.c_str())) > 1) {
          all.erase(igdbAt);
          break;
        }
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
    static const char *order[3][7] = {{"SteamGridDB", "Steam", "IGDB", "DLsite", "VNDB", "itch.io", ""},
                                      {"SteamGridDB", "Steam", "IGDB", "RAWG", "VNDB", "", ""},
                                      {"SteamGridDB", "Steam", "", "", "", "", ""}};
    for (auto *name : order[kind])
      if (*name)
        if (auto *f = from(all, name))
          if (!f->urls[kind].empty())
            return f->urls[kind];
    if (kind == 1)
      for (auto *name : {"Steam", "DLsite", "itch.io"})
        if (auto *f = from(all, name))
          if (!f->heroFallback.empty())
            return f->heroFallback;
    return "";
  }
  // Screenshots from the first source that has them.
  static const Found *shotsFrom(const std::vector<Found> &all) {
    for (auto *name : {"IGDB", "Steam", "VNDB", "RAWG", "DLsite"})
      if (auto *f = from(all, name))
        if (!f->shots.empty())
          return f;
    return nullptr;
  }
  // Saves up to four screenshots as shot1..shot4 in dir, replacing old ones.
  bool saveShots(const Found &f, const std::string &dir) {
    int n = 0;
    std::vector<std::string> data;
    for (auto &url : f.shots) {
      std::string bytes;
      if (cancel || data.size() >= 4 || !get(url, bytes))
        continue;
      if (Image::decode(std::vector<uint8_t>(bytes.begin(), bytes.end())).empty())
        continue;
      data.push_back(std::move(bytes));
    }
    if (data.empty())
      return false;
    for (int i = 1; i <= 4; ++i)
      for (const char *ext : {".png", ".jpg", ".webp"})
        std::remove((dir + "shot" + std::to_string(i) + ext).c_str());
    for (auto &bytes : data) {
      const char *ext = uint8_t(bytes[0]) == 0x89 ? ".png" : uint8_t(bytes[0]) == 0xff ? ".jpg" : ".webp";
      n += write(dir, "shot" + std::to_string(n + 1) + ext, bytes);
    }
    return n > 0;
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
    return "{\n  \"title\": " +
           quoted(best(&Found::title, {"Steam", "IGDB", "RAWG", "VNDB", "SteamGridDB", "DLsite", "itch.io"})) +
           ",\n  \"released\": " + quoted(best(&Found::released, {"IGDB", "RAWG", "Steam", "VNDB", "DLsite"})) +
           ",\n  \"developer\": " + quoted(best(&Found::developer, {"IGDB", "RAWG", "Steam", "VNDB"})) +
           ",\n  \"publisher\": " + quoted(best(&Found::publisher, {"IGDB", "RAWG", "Steam"})) +
           ",\n  \"genres\": " + quoted(best(&Found::genres, {"IGDB", "RAWG"})) +
           ",\n  \"description\": " + quoted(best(&Found::description, {"Steam", "IGDB", "RAWG", "VNDB"})) +
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
    bool shots = current(job, "shot1").empty();
    if (!need[0] && !need[1] && !need[2] && !info && !shots)
      return false;
    auto all = gather(job.name, false);
    auto dir = root + job.folder + "/";
    for (int k = 0; k < 3; ++k)
      if (need[k] && !cancel)
        found = save(pickUrl(all, k), dir, kinds[k]) || found;
    // No source knew the game: its own title screen becomes its cover and
    // background.
    if ((need[0] || need[1]) && !cancel && pickUrl(all, 0).empty() && pickUrl(all, 1).empty()) {
      auto title = titleScreen(job.gameDir);
      if (!title.empty() && !Image::decode(std::vector<uint8_t>(title.begin(), title.end())).empty()) {
        const char *ext = uint8_t(title[0]) == 0x89 ? ".png" : ".jpg";
        for (int k = 0; k < 2; ++k)
          if (need[k])
            found = write(dir, std::string(kinds[k]) + ext, title) || found;
      }
    }
    if (shots && !cancel)
      if (auto *f = shotsFrom(all))
        found = saveShots(*f, dir) || found;
    if (info && !cancel && !all.empty())
      found = write(dir, "info.json", infoJson(all)) || found;
    return found;
  }

  // One game: every source's pictures are offered beside the current one.
  bool choose(const Job &job) {
    auto all = gather(job.name, true);
    if (cancel || (all.empty() && titleScreen(job.gameDir).empty()))
      return false;
    Choices offer;
    offer.key = job.key;
    offer.name = job.name;
    std::vector<std::string> bytes[3], paths[3], sources[3];
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
        sources[k].push_back("Current");
      }
      std::vector<std::string> seen;
      // The game's own title screen, as a cover or background.
      if (k < 2 && !cancel) {
        auto title = titleScreen(job.gameDir);
        Image img = title.empty() || title == nowBytes ? Image() : Image::decode(std::vector<uint8_t>(title.begin(), title.end()));
        if (!img.empty()) {
          offer.options[k].push_back({"Title screen", thumbnail(img, k), false, false});
          bytes[k].push_back(std::move(title));
          paths[k].push_back("");
          sources[k].push_back("Title screen");
        }
      }
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
        sources[k].push_back(f.source);
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
    // The sources whose pictures were taken: picking one confirms that source
    // found the right game, so only those supply the details.
    std::vector<std::string> trusted;
    for (int k = 0; k < 3; ++k) {
      if (pick[k] < 0 || pick[k] >= int(bytes[k].size()) || bytes[k][pick[k]].empty())
        continue; // nothing, or the current picture kept
      trusted.push_back(sources[k][pick[k]]);
      // Replace in the folder the game's art is read from.
      auto now = paths[k].empty() ? "" : paths[k][0];
      auto dir = now.empty() ? root + job.folder + "/" : now.substr(0, now.find_last_of('/') + 1);
      for (const char *ext : {".png", ".jpg", ".jpeg", ".webp"})
        std::remove((dir + kinds[k] + ext).c_str());
      auto &data = bytes[k][pick[k]];
      const char *ext = uint8_t(data[0]) == 0x89 ? ".png" : uint8_t(data[0]) == 0xff ? ".jpg" : ".webp";
      found = write(dir, std::string(kinds[k]) + ext, data) || found;
    }
    // Everything kept (or the chooser closed): the lookup found the wrong
    // game or nothing better, so the details stay as they are too.
    if (ask && trusted.empty()) {
      note("Kept the current artwork and details.");
      return false;
    }
    std::vector<Found> facts;
    for (auto &f : all)
      if (!ask || std::find(trusted.begin(), trusted.end(), f.source) != trusted.end())
        facts.push_back(f);
    if (facts.empty())
      return found;
    std::string infoDir = root + job.folder + "/";
    for (auto &dir : job.existing)
      if (exists(root + dir + "/info.json")) {
        infoDir = root + dir + "/";
        break;
      }
    found = write(infoDir, "info.json", infoJson(facts)) || found;
    if (auto *f = shotsFrom(facts))
      found = saveShots(*f, infoDir) || found;
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
  state->igdbId = readKey(dataRoot + "igdb-client-id.txt");
  state->igdbSecret = readKey(dataRoot + "igdb-client-secret.txt");
#ifdef __vita__
  auto holder = new std::shared_ptr<State>(state);
  // Below the screen and the picture loader, on the core the screen does
  // not use: decoding downloaded pictures never holds up scrolling.
  auto thread = sceKernelCreateThread("hardrpg_art_fetch", State::entry, 0x10000100 + 30, 256 * 1024, 0,
                                      SCE_KERNEL_CPU_MASK_USER_1, nullptr);
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
