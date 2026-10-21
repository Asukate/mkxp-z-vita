// The artwork downloader against canned Steam, RAWG, VNDB, IGDB, DLsite and itch.io replies: which
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

  // 7. IGDB: the login comes first; its search may miss a short name
  // ("OFF"), so the exact name is asked too; among same-name games the one
  // made with RPG Maker wins over a newer one; screenshots are saved.
  std::ofstream(root + "/igdb-client-id.txt") << "clientid\n";
  std::ofstream(root + "/igdb-client-secret.txt") << "secret\n";
  std::vector<std::string> igdbBodies;
  int logins = 0;
  ArtFetch::setTransport([&](const std::string &url, const std::string &post, std::string &out) {
    auto has = [&](const char *part) { return url.find(part) != std::string::npos; };
    if (has("id.twitch.tv/oauth2/token")) {
      ++logins;
      out = "{\"access_token\":\"tok\"}";
    } else if (has("api.igdb.com/v4/games")) {
      igdbBodies.push_back(post);
      if (post.rfind("search", 0) == 0)
        out = "[]";
      else
        out = "[{\"id\":1,\"name\":\"Off\",\"first_release_date\":1700000000,\"cover\":{\"image_id\":\"newcover\"}},"
              "{\"id\":2,\"name\":\"OFF\",\"first_release_date\":1215000000,\"cover\":{\"image_id\":\"oldcover\"},"
              "\"game_engines\":[{\"name\":\"RPG Maker 2003\"}],\"summary\":\"A batter purifies zones.\","
              "\"screenshots\":[{\"image_id\":\"s1\"},{\"image_id\":\"s2\"}],"
              "\"involved_companies\":[{\"company\":{\"name\":\"Mortis Ghost\"},\"developer\":true,\"publisher\":false}]}]";
    } else if (has("images.igdb.com") && has("oldcover"))
      out = cover;
    else if (has("images.igdb.com") && (has("/s1.") || has("/s2.")))
      out = hero;
    else if (has("images.igdb.com"))
      out = other;
    else if (has("storesearch"))
      out = "{\"items\":[]}";
    else if (has("kana/vn") || has("kana/release"))
      out = "{\"results\":[]}";
    else if (has("api.rawg.io"))
      out = "{\"results\":[]}";
    else
      return false; // DLsite, itch.io: nothing
    return true;
  });
  {
    ArtFetch fetch;
    assert(run(fetch, "OFF", "off", true).found == 1);
    finish(fetch);
  }
  assert(logins == 1 && igdbBodies.size() == 2 && igdbBodies[1].rfind("where name ~ \"OFF\";", 0) == 0);
  assert(read(root + "/art/off/cover.jpg") == cover);
  assert(read(root + "/art/off/shot1.jpg") == hero && exists(root + "/art/off/shot2.jpg") && !exists(root + "/art/off/shot3.jpg"));
  info = read(root + "/art/off/info.json");
  assert(info.find("\"released\": \"2008\"") != std::string::npos && info.find("\"developer\": \"Mortis Ghost\"") != std::string::npos);
  std::remove((root + "/igdb-client-id.txt").c_str());

  // 8. DLsite: its search page names the product; a small JSON reply gives
  // the cover, the year and the adult rating. Spaces are sent as '+'.
  std::string dlsiteAsked;
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    auto has = [&](const char *part) { return url.find(part) != std::string::npos; };
    if (has("dlsite.com/maniax/fsr")) {
      dlsiteAsked = url;
      out = "<a href=\"https://www.dlsite.com/maniax/work/=/product_id/RJ01000001.html\" title=\"Other Game\">"
            "<a href=\"https://www.dlsite.com/maniax/work/=/product_id/RJ203687.html\" title=\"Dream Diary -夢-\">";
    } else if (has("product/info/ajax?product_id=RJ203687"))
      out = "{\"RJ203687\":{\"work_name\":\"Dream Diary -夢-\",\"age_category\":3,\"regist_date\":\"2017-08-29 00:00:00\","
            "\"work_image\":\"//img.dlsite.jp/x/RJ203687_img_main.jpg\"}}";
    else if (has("img.dlsite.jp/x/RJ203687_img_main"))
      out = cover;
    else if (has("RJ203687_img_smp1"))
      out = hero;
    else if (has("storesearch"))
      out = "{\"items\":[]}";
    else if (has("kana/"))
      out = "{\"results\":[]}";
    else
      return false;
    return true;
  });
  {
    ArtFetch fetch;
    assert(run(fetch, "Dream Diary", "dream", true).found == 1);
    finish(fetch);
  }
  assert(dlsiteAsked.find("keyword/Dream+Diary/") != std::string::npos);
  assert(read(root + "/art/dream/cover.jpg") == cover && read(root + "/art/dream/shot1.jpg") == hero);
  info = read(root + "/art/dream/info.json");
  assert(info.find("\"adult\": true") != std::string::npos && info.find("\"released\": \"2017\"") != std::string::npos);

  // 9. itch.io is asked last and only an exact name counts: a fan game
  // whose name merely contains the game's is ignored. Folder names like
  // "to-the-moon" are searched as words.
  std::string itchAsked;
  std::string itchPage =
      "<div data-game_id=\"5\"><img data-lazy_src=\"https://img.itch.zone/a/315x250%23c/fan.png\">"
      "<a class=\"title game_link\" href=\"https://x.itch.io/fan\">To the Moon Fan Remake</a></div>"
      "<div data-game_id=\"6\"><img data-lazy_src=\"https://img.itch.zone/b/315x250%23c/real.png\">"
      "<a class=\"title game_link\" href=\"https://y.itch.io/real\" data-label=\"game:6:title\">To the Moon</a></div>";
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    auto has = [&](const char *part) { return url.find(part) != std::string::npos; };
    if (has("itch.io/search")) {
      itchAsked = url;
      out = itchPage;
    } else if (has("img.itch.zone/b/630x500%23c/real.png"))
      out = cover;
    else if (has("storesearch"))
      out = "{\"items\":[]}";
    else if (has("kana/"))
      out = "{\"results\":[]}";
    else
      return false;
    return true;
  });
  {
    ArtFetch fetch;
    assert(run(fetch, "to-the-moon", "moon", true).found == 1);
    finish(fetch);
  }
  assert(itchAsked.find("q=to%20the%20moon") != std::string::npos && read(root + "/art/moon/cover.jpg") == cover);

  // Accented letters match their plain ones in a library run.
  steamSearch = "{\"items\":[{\"name\":\"Pok\xc3\xa9mon Fusion\",\"id\":10}]}";
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    if (url.find("storesearch") != std::string::npos)
      out = steamSearch;
    else if (url.find("GetItems") != std::string::npos)
      out = steamItem;
    else if (url.find("img/cover") != std::string::npos)
      out = cover;
    else if (url.find("kana/") != std::string::npos)
      out = "{\"results\":[]}";
    else
      return false;
    return true;
  });
  {
    ArtFetch fetch;
    assert(run(fetch, "Pokemon Fusion", "fusion", true).found == 1);
    finish(fetch);
  }

  // 10. Nothing found anywhere: the game's own title screen becomes its
  // cover and background, from loose files or the encrypted archive.
  ArtFetch::setTransport([&](const std::string &url, const std::string &, std::string &out) {
    if (url.find("storesearch") != std::string::npos)
      out = "{\"items\":[]}";
    else if (url.find("kana/") != std::string::npos)
      out = "{\"results\":[]}";
    else
      return false;
    return true;
  });
  auto title = picture(222), small = picture(10);
  auto game = root + "/games/loose";
  mkdir((root + "/games").c_str(), 0777);
  mkdir(game.c_str(), 0777);
  mkdir((game + "/Graphics").c_str(), 0777);
  mkdir((game + "/Graphics/Titles1").c_str(), 0777);
  std::ofstream(game + "/Graphics/Titles1/Small.jpg", std::ios::binary) << small.substr(0, small.size() / 2);
  std::ofstream(game + "/Graphics/Titles1/Castle.jpg", std::ios::binary) << title;
  auto job = [&](const std::string &name, const std::string &folder, const std::string &dir) {
    ArtFetch fetch;
    ArtFetch::Job j{"key-" + folder, name, folder, {folder}, dir};
    fetch.start({j}, root + "/art/", root + "/", true);
    finish(fetch);
    return fetch.poll().found;
  };
  assert(job("Unknown Loose Game", "loose", game) == 1);
  assert(read(root + "/art/loose/cover.jpg") == title && read(root + "/art/loose/hero.jpg") == title);
  // XP and VX archives (version 1) and VX Ace archives (version 3).
  auto archive = [&](int version, const std::string &path, const std::vector<std::pair<std::string, std::string>> &files) {
    std::string out = std::string("RGSSAD\0", 7) + char(version);
    auto put = [&](uint32_t v) {
      for (int i = 0; i < 4; ++i)
        out += char((v >> (8 * i)) & 0xff);
    };
    auto crypt = [](std::string data, uint32_t key) {
      for (size_t i = 0; i < data.size(); ++i) {
        data[i] = char((unsigned char)data[i] ^ ((key >> (8 * (i % 4))) & 0xff));
        if (i % 4 == 3)
          key = key * 7 + 3;
      }
      return data;
    };
    if (version == 1) {
      uint32_t key = 0xDEADCAFE;
      for (auto &f : files) {
        put(uint32_t(f.first.size()) ^ key), key = key * 7 + 3;
        for (char c : f.first)
          out += char((unsigned char)c ^ (key & 0xff)), key = key * 7 + 3;
        put(uint32_t(f.second.size()) ^ key), key = key * 7 + 3;
        out += crypt(f.second, key);
      }
    } else {
      uint32_t seed = 7, key = seed * 9 + 3;
      put(seed);
      size_t table = 0; // the entry table, after the 12 header bytes already written
      for (auto &f : files)
        table += 16 + f.first.size();
      table += 4;
      uint32_t offset = uint32_t(out.size() + table);
      std::string data;
      for (auto &f : files) {
        uint32_t fileKey = 0x1234567u + uint32_t(f.second.size());
        put(offset ^ key), put(uint32_t(f.second.size()) ^ key), put(fileKey ^ key), put(uint32_t(f.first.size()) ^ key);
        for (size_t i = 0; i < f.first.size(); ++i)
          out += char((unsigned char)f.first[i] ^ ((key >> (8 * (i % 4))) & 0xff));
        data += crypt(f.second, fileKey);
        offset += uint32_t(f.second.size());
      }
      put(0 ^ key);
      out += data;
    }
    std::ofstream(path, std::ios::binary) << out;
  };
  for (int version : {1, 3}) {
    auto dir = root + "/games/packed" + std::to_string(version);
    mkdir(dir.c_str(), 0777);
    archive(version, dir + (version == 1 ? "/Packed.rgssad" : "/Game.rgss3a"),
            {{"Data\\Map001.rxdata", std::string(5000, 'x')},
             {version == 1 ? "Graphics\\Titles\\Title.jpg" : "Graphics\\Titles1\\Night.jpg", title},
             {"Graphics\\Pictures\\Big.jpg", other + other}});
    auto folder = "packed" + std::to_string(version);
    assert(job("Unknown Packed Game " + std::to_string(version), folder, dir) == 1);
    assert(read(root + "/art/" + folder + "/cover.jpg") == title);
  }
  ArtFetch::setTransport(nullptr);
  std::cout << "art-fetch: all checks passed\n";
}
