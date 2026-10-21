// Finding a game's title music (src/native_launcher/theme.cpp) in fixtures
// written here: loose VX Ace data, XP and VX Ace encrypted archives, an
// RPG Maker 2000 database, the "title" fallback, MIDI, the override file and
// a ZIP's inner folder. Usage: theme WORK_DIR
#include "native_launcher/theme.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <sys/stat.h>
#include <vector>
using namespace hardrpg;

namespace {
std::string root;
void makeDirs(const std::string &path) {
  for (size_t at = path.find('/', 1); at != std::string::npos; at = path.find('/', at + 1))
    mkdir(path.substr(0, at).c_str(), 0777);
  mkdir(path.c_str(), 0777);
}
void write(const std::string &path, const std::string &data) {
  makeDirs(path.substr(0, path.find_last_of('/')));
  std::ofstream(path, std::ios::binary) << data;
}

// ---- Ruby Marshal 4.8, just what RPG::System needs
std::string fix(long n) { // small fixnums only
  assert(n >= 0 && n < 123);
  return n == 0 ? std::string(1, '\0') : std::string(1, char(n + 5));
}
std::string sym(const std::string &s) { return ":" + fix(long(s.size())) + s; }
std::string link(long i) { return ";" + fix(i); }
std::string str(const std::string &s) { return "I\"" + fix(long(s.size())) + s + fix(1) + sym("E") + "T"; }
// An RPG::System whose @title_bgm names track at volume, after a Table (user
// data) and before a second BGM that reuses the symbols by link.
std::string systemData(const std::string &track, int volume) {
  std::string table(40, '\x01');
  return std::string("\x04\x08", 2) + "o" + sym("RPG::System") + fix(3) +
         sym("@data") + "u" + sym("Table") + fix(long(table.size())) + table +
         sym("@title_bgm") + "o" + sym("RPG::BGM") + fix(3) + sym("@name") + str(track) + sym("@volume") + "i" +
         fix(volume) + sym("@pitch") + "i" + fix(100) +
         // symbols so far: 0 RPG::System 1 @data 2 Table 3 @title_bgm 4 RPG::BGM 5 @name 6 E 7 @volume 8 @pitch
         sym("@battle_bgm") + "o" + link(4) + fix(1) + link(5) + "I\"" + fix(6) + "Battle" + fix(1) + link(6) + "T";
}

// ---- encrypted archives
void put32(std::string &out, uint32_t v) {
  for (int i = 0; i < 4; ++i)
    out.push_back(char((v >> (8 * i)) & 0xff));
}
std::string crypt(std::string data, uint32_t key) {
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = char((unsigned char)data[i] ^ ((key >> (8 * (i % 4))) & 0xff));
    if (i % 4 == 3)
      key = key * 7 + 3;
  }
  return data;
}
using Files = std::vector<std::pair<std::string, std::string>>;
std::string rgssad(const Files &files) { // XP and VX
  std::string out("RGSSAD\0\x01", 8);
  uint32_t key = 0xDEADCAFE;
  for (auto &f : files) {
    put32(out, uint32_t(f.first.size()) ^ key);
    key = key * 7 + 3;
    for (char c : f.first) {
      out.push_back(char((unsigned char)c ^ (key & 0xff)));
      key = key * 7 + 3;
    }
    put32(out, uint32_t(f.second.size()) ^ key);
    key = key * 7 + 3;
    out += crypt(f.second, key);
  }
  return out;
}
std::string rgss3a(const Files &files) { // VX Ace
  std::string out("RGSSAD\0\x03", 8);
  uint32_t seed = 0x1234, key = seed * 9 + 3;
  put32(out, seed);
  uint32_t offset = 12 + 4;
  for (auto &f : files)
    offset += 16 + uint32_t(f.first.size());
  std::string data;
  uint32_t start = 0x0BADF00D;
  for (auto &f : files) {
    put32(out, (offset + uint32_t(data.size())) ^ key);
    put32(out, uint32_t(f.second.size()) ^ key);
    put32(out, start ^ key);
    put32(out, uint32_t(f.first.size()) ^ key);
    for (size_t i = 0; i < f.first.size(); ++i)
      out.push_back(char((unsigned char)f.first[i] ^ ((key >> (8 * (i % 4))) & 0xff)));
    data += crypt(f.second, start);
    start += 77;
  }
  put32(out, 0 ^ key);
  return out + data;
}

// ---- LCF (RPG Maker 2000/2003)
std::string ber(uint32_t v) {
  std::string out(1, char(v & 0x7f));
  while (v >>= 7)
    out.insert(out.begin(), char(0x80 | (v & 0x7f)));
  return out;
}
std::string chunk(uint32_t id, const std::string &data) { return ber(id) + ber(uint32_t(data.size())) + data; }
std::string database(const std::string &track, int volume) {
  std::string music = chunk(0x01, track) + chunk(0x03, ber(uint32_t(volume))) + std::string(1, '\0');
  std::string system = chunk(0x0A, std::string(300, 'x')) + chunk(0x1F, music) + std::string(1, '\0');
  return ber(11) + "LcfDataBase" + chunk(0x0B, std::string(200, 'y')) + chunk(0x16, system) + std::string(1, '\0');
}

Theme find(const std::string &dir, bool expect = true) {
  Theme t;
  bool found = findTheme(root + "/" + dir, t);
  if (found != expect) {
    std::cerr << dir << ": expected " << (expect ? "a theme" : "none") << "\n";
    std::exit(1);
  }
  return t;
}
} // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  root = argv[1];
  int volume = 0;

  // The parsers on their own.
  volume = 0;
  assert(marshalTitleBgm(systemData("Theme of Ace", 80), volume) == "Theme of Ace" && volume == 80);
  volume = 100;
  assert(marshalTitleBgm("not marshal", volume).empty() && volume == 100);
  assert(lcfTitleMusic(database("Opening", 90), volume) == "Opening" && volume == 90);
  assert(lcfTitleMusic(database("(OFF)", 90), volume).empty());

  // VX Ace, loose files, matched without regard to case.
  write(root + "/ace/Game.ini", "[Game]\n");
  write(root + "/ace/Data/System.rvdata2", systemData("Theme of Ace", 80));
  write(root + "/ace/audio/bgm/theme of ace.OGG", "OggS loose");
  auto t = find("ace");
  assert(t.name == "Theme of Ace" && t.volume == 80 && t.ext == ".ogg" && t.bytes.empty());
  assert(t.file == root + "/ace/audio/bgm/theme of ace.OGG");

  // XP: data and music inside Game.rgssad, an MP3 track.
  write(root + "/xp/Game.ini", "[Game]\n");
  write(root + "/xp/Game.rgssad", rgssad({{"Graphics\\Titles\\t.png", "png"},
                                          {"Data\\System.rxdata", systemData("Opening", 60)},
                                          {"Audio\\BGM\\Opening.mp3", "ID3 xp music bytes"}}));
  t = find("xp");
  assert(t.name == "Opening" && t.volume == 60 && t.ext == ".mp3" && t.bytes == "ID3 xp music bytes");

  // VX Ace: Game.rgss3a; a loose track still wins over the archived one.
  write(root + "/ace3/Game.rgss3a", rgss3a({{"Data\\System.rvdata2", systemData("Sea", 100)},
                                            {"Audio\\BGM\\Sea.ogg", "OggS archived sea"},
                                            {"Audio\\BGM\\Other.ogg", "OggS other"}}));
  t = find("ace3");
  assert(t.name == "Sea" && t.bytes == "OggS archived sea");
  write(root + "/ace3/Audio/BGM/Sea.ogg", "OggS loose sea");
  t = find("ace3");
  assert(t.file == root + "/ace3/Audio/BGM/Sea.ogg" && t.bytes.empty());

  // RPG Maker 2000: RPG_RT.ldb and Music/.
  write(root + "/2k/RPG_RT.ldb", database("Opening1", 90));
  write(root + "/2k/Music/Opening1.wav", "RIFF wav");
  t = find("2k");
  assert(t.name == "Opening1" && t.volume == 90 && t.ext == ".wav");

  // The named track is missing (it is in the RTP): a "title" track stands in.
  write(root + "/rtp/Data/System.rvdata2", systemData("RTP Theme", 100));
  write(root + "/rtp/Audio/BGM/Field.ogg", "OggS field");
  write(root + "/rtp/Audio/BGM/MyTitle_Loop.ogg", "OggS title");
  t = find("rtp");
  assert(t.name == "mytitle_loop" && t.ext == ".ogg");

  // MIDI only: nothing to play.
  write(root + "/midi/Data/System.rvdata2", systemData("Song", 100));
  write(root + "/midi/Audio/BGM/Song.mid", "MThd");
  find("midi", false);

  // hardrpg-theme.* chooses the music.
  write(root + "/override/Data/System.rvdata2", systemData("Theme", 100));
  write(root + "/override/Audio/BGM/Theme.ogg", "OggS game");
  write(root + "/override/hardrpg-theme.mp3", "ID3 chosen");
  t = find("override");
  assert(t.name == "hardrpg-theme" && t.ext == ".mp3");

  // An unpacked ZIP with the game in a folder of its own.
  write(root + "/zip/Inner Game/Data/System.rvdata2", systemData("Zip Theme", 100));
  write(root + "/zip/Inner Game/Audio/BGM/Zip Theme.ogg", "OggS zip");
  t = find("zip");
  assert(t.name == "Zip Theme");

  // Not a game, and not there.
  makeDirs(root + "/empty");
  find("empty", false);
  find("missing", false);
  std::cout << "theme: all checks passed\n";
}
