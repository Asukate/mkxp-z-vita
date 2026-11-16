// Host test: finds the Dante game in a PC-98 disk image and checks every
// file it lists against a folder extracted another way.
#include "native_launcher/pc98_fat.h"
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: pc98-fat IMAGE [EXTRACTED_DIR]\n");
    return 2;
  }
  hardrpg::Pc98Fat fat;
  if (!fat.open(argv[1])) {
    std::fprintf(stderr, "no FAT file system found\n");
    return 1;
  }
  hardrpg::DanteInImage game;
  if (!hardrpg::findDanteGame(fat, game)) {
    std::fprintf(stderr, "no Dante game found\n");
    return 1;
  }
  std::printf("engine D%d in '%s'\n", game.engine, game.folder.c_str());
  if (argc < 3)
    return 0;
  int files = 0, bad = 0;
  std::function<void(uint32_t, std::string)> walk = [&](uint32_t cluster, std::string rel) {
    for (auto &e : fat.list(cluster)) {
      std::string path = rel.empty() ? e.name : rel + "/" + e.name;
      if (e.dir) {
        walk(e.cluster, path);
        continue;
      }
      std::vector<uint8_t> data;
      ++files;
      std::FILE *f = std::fopen((std::string(argv[2]) + "/" + path).c_str(), "rb");
      std::vector<uint8_t> want;
      if (f) {
        uint8_t buf[65536];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
          want.insert(want.end(), buf, buf + n);
        std::fclose(f);
      }
      if (!fat.read(e, data) || !f || data != want) {
        ++bad;
        std::printf("MISMATCH %s\n", path.c_str());
      }
    }
  };
  walk(0, "");
  std::printf("%d files, %d mismatches\n", files, bad);
  return bad ? 1 : 0;
}
