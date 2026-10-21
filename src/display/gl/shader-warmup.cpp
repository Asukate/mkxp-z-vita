#include "shader-warmup.h"

#include "gl-fun.h"
#include "shader.h"
#include "shader-cache-build-id.h"

#include <cstdio>
#include <cstring>
#include <exception>
#include <sys/stat.h>
#include <unistd.h>

namespace ShaderWarmup {
namespace {
const char marker[] = "ux0:/data/mkxpz-cache/programs-v1/prepared.txt";

template <class T> bool make() {
  T shader; // compiling it fills the cache; it is deleted right away
  (void)shader;
  return true;
}

struct Step {
  const char *label;
  bool (*run)();
};

// The shaders the games we measured use in their first minutes, most common
// first. Rare ones (KGL effects, the Bicubic and Lanczos filters) still
// compile on first use.
const Step steps[] = {
    {"pictures", make<SimpleShader>},
    {"sprites", make<SimpleSpriteShader>},
    {"sprite effects", make<SpriteShader>},
    {"sprites with opacity", make<AlphaSpriteShader>},
    {"fades", make<SimpleAlphaShader>},
    {"solid colors", make<FlatColorShader>},
    {"tinted pictures", make<SimpleColorShader>},
    {"screen fades", make<SimpleTransShader>},
    {"image transitions", make<TransShader>},
    {"backgrounds and fogs", make<PlaneShader>},
    {"VX map tiles", make<TilemapVXShader>},
    {"XP map tiles", make<TilemapShader>},
    {"map flashes", make<FlashMapShader>},
    {"Sharp filter", make<SharpShader>},
    {"blur", make<BlurShader>},
    {"color shifts", make<HueShader>},
    {"gray effects", make<GrayShader>},
    {"picture copies", make<BltShader>},
    {"rotated pictures", make<SimpleMatrixShader>},
};

bool glReady = false;
} // namespace

bool needed() {
  if (access("app0:/shader-cache.off", F_OK) == 0)
    return false; // nothing would be kept
  FILE *f = std::fopen(marker, "rb");
  if (!f)
    return true;
  char id[128] = {};
  size_t n = std::fread(id, 1, sizeof id - 1, f);
  std::fclose(f);
  id[n] = 0;
  return std::strcmp(id, MKXPZ_SHADER_CACHE_BUILD) != 0;
}

int count() { return int(sizeof steps / sizeof steps[0]); }

const char *label(int i) { return i >= 0 && i < count() ? steps[i].label : ""; }

bool run(int i) {
  if (i < 0 || i >= count())
    return false;
  try {
    if (!glReady) {
      initGLFunctions();
      glReady = true;
    }
    return steps[i].run();
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Shader warm-up: %s failed: %s\n", steps[i].label, e.what());
  } catch (...) {
    std::fprintf(stderr, "Shader warm-up: %s failed\n", steps[i].label);
  }
  return false;
}

void finish() {
  mkdir("ux0:/data/mkxpz-cache", 0777);
  mkdir("ux0:/data/mkxpz-cache/programs-v1", 0777);
  if (FILE *f = std::fopen(marker, "wb")) {
    std::fputs(MKXPZ_SHADER_CACHE_BUILD, f);
    std::fclose(f);
  }
}
} // namespace ShaderWarmup
