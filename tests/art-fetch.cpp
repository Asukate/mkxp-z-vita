// The artwork downloader against canned Steam, RAWG and VNDB replies: which
// source wins each picture and fact, the adult flag, VNDB's RPG Maker check,
// and the per-game chooser. Usage: art-fetch WORK_DIR
#include "native_launcher/art_fetch.h"
#include "native_launcher/image.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
using namespace hardrpg;

namespace {
std::string root;
std::string read(const std::string &path) {
  std::ifstream in(path, std::ios::binary);
  std::stringstream s;
  s << in.rdbuf();
  return s.str();
}
bool exists(const std::string &path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}
// A small JPEG of one colour, as a picture download.
std::string picture(int shade) {
  Image img = Image::blank(60, 90);
  for (size_t i = 0; i < img.rgba.size(); i += 4)
    img.rgba[i] = img.rgba[i + 1] = img.rgba[i + 2] = uint8_t(shade), img.rgba[i + 3] = 255;
  auto path = root + "/picture.jpg";
  img.saveJpeg(path);
  return read(path);
}
ArtFetch::Progress run(ArtFetch &fetch, const std::string &name, const std::string &folder, bool strict) {
  fetch.start({{"key-" + folder, name, folder, {folder}}}, root + "/art/", root + "/", strict);
  ArtFetch::Progress p;
  for (int i = 0; i < 500; ++i) {
    p = fetch.poll();
    if (p.finished || p.choosing)
      break;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return p;
}
void finish(ArtFetch &fetch) {
  for (int i = 0; i < 500 && fetch.running(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}
} // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  root = argv[1];
  std::ofstream(root + "/rawg-key.txt") << "testkey\n";
  std::string cover = picture(40), hero = picture(80), other = picture(200);
  std::string steamSearch, rawgSearch = "{\"results\":[]}", vndbTitles = "{\"results\":[]}", vndbReleases = "{\"results\":[]}";
  std::string steamItem, steamCover = cover;
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    auto has = [&](const char *part) { return url.find(part) != std::string::npos; };
    if (has("storesearch"))
      out = steamSearch;
    else if (has("GetItems"))
      out = steamItem;
    else if (has("api.rawg.io/api/games/5"))
      out = "{\"description_raw\":\"Rawg text\",\"developers\":[{\"name\":\"RawgDev\"}],\"publishers\":[{\"name\":\"RawgPub\"}]}";
    else if (has("api.rawg.io/api/games?"))
      out = rawgSearch;
    else if (has("kana/vn"))
      out = vndbTitles;
    else if (has("kana/release"))
      out = vndbReleases;
    else if (has("img/cover"))
      out = steamCover;
    else if (has("img/hero") || has("img/bg") || has("img/shot"))
      out = hero;
    else if (has("img/vncover"))
      out = cover;
    else
      return false; // everything else (logos here) is missing
    return true;
  });

  // 1. A whole-library run blends Steam and RAWG: Steam's pictures and store
  // text, RAWG's year, credits and genres; Steam rates it adults only.
  steamSearch = "{\"items\":[{\"name\":\"Blend Game\",\"id\":10}]}";
  steamItem = "{\"response\":{\"store_items\":[{\"assets\":{\"asset_url_format\":\"steam/apps/10/${FILENAME}\","
              "\"library_capsule\":\"img/cover.jpg\",\"library_hero\":\"img/hero.jpg\"},"
              "\"basic_info\":{\"short_description\":\"Steam text\",\"developers\":[{\"name\":\"SteamDev\"}],"
              "\"publishers\":[{\"name\":\"SteamPub\"}]},\"release\":{\"steam_release_date\":1500000000},"
              "\"content_descriptorids\":[3]}]}}";
  rawgSearch = "{\"results\":[{\"id\":5,\"name\":\"Blend Game\",\"released\":\"2016-05-01\","
               "\"background_image\":\"https://media.rawg.io/img/bg.jpg\",\"genres\":[{\"name\":\"RPG\"},{\"name\":\"Horror\"}]}]}";
  {
    ArtFetch fetch;
    auto p = run(fetch, "Blend Game", "blend-game", true);
    assert(p.finished && p.found == 1 && p.updated == std::vector<std::string>{"key-blend-game"});
  }
  auto dir = root + "/art/blend-game/";
  assert(read(dir + "cover.jpg") == cover && read(dir + "hero.jpg") == hero && !exists(dir + "logo.png"));
  auto info = read(dir + "info.json");
  for (const char *expected : {"\"released\": \"2016\"", "\"developer\": \"RawgDev\"", "\"publisher\": \"RawgPub\"",
                               "\"genres\": \"RPG, Horror\"", "\"description\": \"Steam text\"", "\"adult\": true",
                               "\"sources\": \"Steam, RAWG\""})
    assert(info.find(expected) != std::string::npos);
  // Nothing is missing any more: a second run asks no source and changes nothing.
  {
    ArtFetch fetch;
    steamCover = other;
    assert(run(fetch, "Blend Game", "blend-game", true).found == 0);
    assert(read(dir + "cover.jpg") == cover);
  }

  // 2. VNDB only knows an adult visual novel of the same name, made with
  // another engine: nothing is taken from it.
  steamSearch = "{\"items\":[]}";
  rawgSearch = "{\"results\":[]}";
  vndbTitles = "{\"results\":[{\"id\":\"v1\",\"title\":\"Misao\",\"image\":{\"url\":\"https://t.vndb.org/img/vncover.jpg\",\"sexual\":2}}]}";
  vndbReleases = "{\"results\":[{\"engine\":\"KiriKiri\",\"minage\":18}]}";
  {
    ArtFetch fetch;
    assert(run(fetch, "Misao", "misao", true).found == 0);
  }
  assert(!exists(root + "/art/misao"));

  // 3. One made with RPG Maker is taken, and marked adult by its age rating.
  vndbTitles = "{\"results\":[{\"id\":\"v2\",\"title\":\"Mad Clock\",\"released\":\"2009-01-01\","
               "\"image\":{\"url\":\"https://t.vndb.org/img/vncover.jpg\",\"sexual\":0},"
               "\"screenshots\":[{\"url\":\"https://t.vndb.org/img/shot.jpg\",\"sexual\":0}]}]}";
  vndbReleases = "{\"results\":[{\"engine\":null,\"minage\":18},{\"engine\":\"RPG Maker 2000\",\"minage\":18}]}";
  {
    ArtFetch fetch;
    assert(run(fetch, "Mad Clock", "mad-clock", true).found == 1);
  }
  info = read(root + "/art/mad-clock/info.json");
  assert(exists(root + "/art/mad-clock/cover.jpg") && exists(root + "/art/mad-clock/hero.jpg"));
  assert(info.find("\"adult\": true") != std::string::npos && info.find("\"released\": \"2009\"") != std::string::npos);

  // 4. Looking up one game offers each source's picture beside the current
  // one and waits; the pick replaces the file.
  steamSearch = "{\"items\":[{\"name\":\"Blend Game\",\"id\":10}]}";
  rawgSearch = "{\"results\":[]}";
  vndbTitles = "{\"results\":[]}";
  steamCover = other;
  {
    ArtFetch fetch;
    auto p = run(fetch, "Blend Game", "blend-game", false);
    assert(p.choosing);
    ArtFetch::Choices offer;
    assert(fetch.takeChoices(offer) && offer.key == "key-blend-game");
    assert(offer.options[0].size() == 2 && offer.options[0][0].current && offer.options[0][1].source == "Steam");
    assert(offer.options[0][1].adult && !offer.options[0][1].thumb.empty());
    int pick[3] = {1, -1, -1};
    fetch.choose(pick);
    finish(fetch);
  }
  assert(read(dir + "cover.jpg") == other && read(dir + "hero.jpg") == hero);

  // 5. Keeping every current picture (or closing the chooser) leaves the
  // details alone: the lookup may have found another game.
  auto before = read(dir + "info.json");
  steamCover = picture(120);
  steamItem.replace(steamItem.find("Steam text"), 10, "Wrong text");
  {
    ArtFetch fetch;
    auto p = run(fetch, "Blend Game", "blend-game", false);
    assert(p.choosing);
    ArtFetch::Choices offer;
    assert(fetch.takeChoices(offer));
    int keep[3] = {-1, -1, -1};
    fetch.choose(keep);
    finish(fetch);
    p = fetch.poll();
    assert(p.found == 0 && p.note.find("Kept") != std::string::npos);
  }
  assert(read(dir + "info.json") == before && read(dir + "cover.jpg") == other);

  // 6. Zip and folder names are cleaned before searching: "YumeNikki.zip"
  // is looked up as "YumeNikki", then "Yume Nikki".
  std::string asked;
  steamSearch = "{\"items\":[{\"name\":\"YUMENIKKI -DREAM DIARY-\",\"id\":11},{\"name\":\"Yume Nikki\",\"id\":10}]}";
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    if (url.find("storesearch") != std::string::npos) {
      asked += url.substr(url.find("term=") + 5) + ";";
      out = steamSearch;
    } else if (url.find("GetItems") != std::string::npos)
      out = steamItem;
    else if (url.find("img/cover") != std::string::npos)
      out = cover;
    else
      return false;
    return true;
  });
  {
    ArtFetch fetch;
    assert(run(fetch, "YumeNikki.zip", "yume", true).found == 1);
  }
  assert(asked == "YumeNikki;" && exists(root + "/art/yume/cover.jpg"));
  // "Mogeko Castle - English VX Ace" is looked up without its tags.
  asked.clear();
  steamSearch = "{\"items\":[]}";
  {
    ArtFetch fetch;
    run(fetch, "Mogeko Castle - English VX Ace", "mogeko", true);
    finish(fetch);
  }
  assert(asked == "Mogeko%20Castle;");
  ArtFetch::setTransport(nullptr);
  std::cout << "art-fetch: all checks passed\n";
}
