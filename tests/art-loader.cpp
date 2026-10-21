// The art loader's disk cache: a picture is scaled once and read back from
// the cache, a changed picture is scaled again, adult art is blurred under its
// own cache name, and a generated cover is kept once drawn.
// Usage: art-loader WORK_DIR
#include "native_launcher/art_loader.h"
#include <cassert>
#include <chrono>
#include <dirent.h>
#include <fstream>
#include <iostream>
#include <sys/stat.h>
#include <thread>
using namespace hardrpg;

namespace {
std::string root;
int cached(char kind) {
  int n = 0;
  if (DIR *d = opendir((root + "/cache/").c_str())) {
    while (auto *e = readdir(d))
      n += e->d_name[0] == kind && e->d_name[1] == '-';
    closedir(d);
  }
  return n;
}
Image solid(int w, int h, int shade) {
  Image img = Image::blank(w, h);
  for (size_t i = 0; i < img.rgba.size(); i += 4)
    img.rgba[i] = uint8_t(shade), img.rgba[i + 1] = uint8_t(255 - shade), img.rgba[i + 2] = 0, img.rgba[i + 3] = 255;
  // A stripe, so blurring changes something.
  for (int y = 0; y < h; ++y)
    for (int x = w / 2; x < w / 2 + 4; ++x)
      img.rgba[(size_t(y) * w + x) * 4 + 2] = 255;
  return img;
}
ArtLoader::Result load(ArtLoader &loader, const std::string &key, bool hero = false, bool blur = true) {
  ArtLoader::Request r;
  r.key = key;
  r.hero = hero;
  r.root = root + "/art/";
  r.names = {key};
  r.cache = root + "/cache/";
  r.title = "Title of " + key;
  r.width = hero ? 96 : 40;
  r.height = hero ? 54 : 60;
  r.blurAdult = blur;
  ArtLoader::Result out;
  for (int i = 0; i < 500; ++i) {
    loader.beginFrame();
    loader.want(r, 0);
    if (loader.take(out) && !out.cancelled)
      return out;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  assert(!"the loader never answered");
  return out;
}
} // namespace

int main(int argc, char **argv) {
  assert(argc == 2);
  root = argv[1];
  mkdir((root + "/art").c_str(), 0777);
  mkdir((root + "/art/game").c_str(), 0777);
  solid(120, 180, 30).saveJpeg(root + "/art/game/cover.jpg");
  solid(320, 180, 90).saveJpeg(root + "/art/game/hero.jpg");
  {
    ArtLoader loader;
    auto r = load(loader, "game");
    assert(r.cover.width == 40 && r.cover.height == 60 && r.generatedPath.empty());
    assert(cached('c') == 1);
    auto h = load(loader, "game", true);
    assert(h.background.width == 96 && h.background.height == 54 && cached('h') == 1);
  }
  // A new loader (the next boot) reads the scaled copy; nothing new is cached.
  {
    ArtLoader loader;
    assert(load(loader, "game").cover.width == 40 && cached('c') == 1);
  }
  // The picture changes: it is scaled again under a new name.
  solid(150, 150, 200).saveJpeg(root + "/art/game/cover.jpg");
  {
    ArtLoader loader;
    load(loader, "game");
    assert(cached('c') == 2);
  }
  // Marked adult: blurred, cached apart from the sharp copy; not when the
  // blur is switched off.
  std::ofstream(root + "/art/game/info.json") << "{\n  \"adult\": true\n}\n";
  Image sharp, blurred;
  {
    ArtLoader loader;
    blurred = load(loader, "game").cover;
    assert(cached('c') == 3);
  }
  {
    ArtLoader loader;
    sharp = load(loader, "game", false, false).cover;
    assert(cached('c') == 3 && sharp.rgba != blurred.rgba);
  }
  // No cover at all: the UI draws one and the loader keeps it.
  {
    ArtLoader loader;
    auto r = load(loader, "plain");
    assert(r.cover.empty() && !r.generatedPath.empty() && r.info.empty());
    loader.store(r.generatedPath, solid(40, 60, 120));
    for (int i = 0; i < 300 && cached('g') == 0; ++i)
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    assert(cached('g') == 1);
  }
  {
    ArtLoader loader;
    auto r = load(loader, "plain");
    assert(r.cover.width == 40 && r.generatedPath.empty());
  }
  std::cout << "art-loader: all checks passed\n";
}
