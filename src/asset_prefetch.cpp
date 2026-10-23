#include "asset_prefetch.h"
#include "filesystem/filesystem.h"

#include <SDL_image.h>
#include <SDL_mutex.h>
#include <SDL_sound.h>
#include <SDL_thread.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#endif
#include <SDL_timer.h>
#include <physfs.h>
#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <deque>
#include <cstring>
#include <iterator>
#include <list>
#include <exception>
#include <set>
#include <unordered_map>

namespace AssetPrefetch {
namespace {
constexpr size_t imageBudget = 24u << 20, soundBudget = 16u << 20;
// Decoded copies of expensive images the game already loaded: games (and
// their cache scripts) often dispose an image and load it again later.
// RPG Maker VX Ace's own Window_Base#draw_face disposes the face sheet after
// every draw, so status windows decode faces (about 20 ms each on the Vita)
// on every redraw; those must be kept too.
constexpr size_t keepBudget = 24u << 20;
constexpr double keepMs = 8.0;
constexpr size_t historyMax = 400;

struct Entry {
  char kind; // 'I' image, 'S' sound
  double ms;
  std::string name;
};

// SDL's mutex and condition: the ones the engine already relies on on the Vita.
struct Lock {
  SDL_mutex *m;
  explicit Lock(SDL_mutex *m) : m(m) { SDL_LockMutex(m); }
  ~Lock() { SDL_UnlockMutex(m); }
};

struct State {
  SDL_mutex *lock = SDL_CreateMutex();
  SDL_cond *changed = SDL_CreateCond();
  FileSystem *fs = nullptr;
  SDL_Thread *thread = nullptr;
  bool stopping = false;
  int workerMask = 0;
  std::string historyPath;
  std::vector<Entry> history;        // last saved, best first
  std::vector<Entry> session;        // this session, in first-use order
  std::set<std::string> seen;        // keys used or loaded this session
  bool dirty = false;
  std::string busy;                  // key the worker is decoding
  struct Prepared {
    SDL_Surface *surface;
    double ms;
  };
  std::unordered_map<std::string, Prepared> images;
  std::unordered_map<std::string, Sound> sounds;
  size_t imageBytes = 0, soundBytes = 0;
  // Kept copies, most recently used first.
  struct Kept {
    std::string key;
    SDL_Surface *surface;
    double ms;
  };
  std::list<Kept> kept;
  size_t keptBytes = 0;
};

size_t bytesOf(const SDL_Surface *s) { return size_t(s->h) * s->pitch; }

SDL_Surface *copyOf(SDL_Surface *src) {
  SDL_Surface *out = SDL_CreateRGBSurface(0, src->w, src->h, 32, src->format->Rmask, src->format->Gmask,
                                          src->format->Bmask, src->format->Amask);
  if (!out)
    return nullptr;
  for (int y = 0; y < src->h; ++y)
    std::memcpy(static_cast<uint8_t *>(out->pixels) + size_t(y) * out->pitch,
                static_cast<uint8_t *>(src->pixels) + size_t(y) * src->pitch, size_t(src->w) * 4);
  return out;
}

// Under the lock. Takes ownership of surface; drops the least recently used
// copies to stay within the budget.
// Under the lock. Takes ownership of surface. Over the budget, the copies
// that are cheapest to load again go first (oldest first among equals), so
// a 400 ms icon sheet outlives a stream of small pictures.
void keepLocked(State &s, const std::string &k, SDL_Surface *surface, double ms) {
  for (auto it = s.kept.begin(); it != s.kept.end(); ++it)
    if (it->key == k) {
      s.keptBytes -= bytesOf(it->surface);
      SDL_FreeSurface(it->surface);
      s.kept.erase(it);
      break;
    }
  s.kept.push_front({k, surface, ms});
  s.keptBytes += bytesOf(surface);
  while (s.keptBytes > keepBudget && s.kept.size() > 1) {
    auto victim = std::prev(s.kept.end());
    for (auto it = std::prev(s.kept.end()); it != s.kept.begin(); --it)
      if (it->ms < victim->ms)
        victim = it;
    s.keptBytes -= bytesOf(victim->surface);
    SDL_FreeSurface(victim->surface);
    s.kept.erase(victim);
  }
}
State *state = nullptr;

// "Graphics/System/IconSet.png" and "graphics\system\iconset" are one key.
std::string key(const std::string &name) {
  std::string k = name;
  for (auto &c : k)
    c = c == '\\' ? '/' : char(std::tolower((unsigned char)c));
  while (k.compare(0, 2, "./") == 0)
    k.erase(0, 2);
  auto slash = k.rfind('/'), dot = k.rfind('.');
  if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
    k.resize(dot);
  return k;
}

double now() { return double(SDL_GetPerformanceCounter()) * 1000.0 / double(SDL_GetPerformanceFrequency()); }

struct ImageHandler : FileSystem::OpenHandler {
  SDL_Surface *surface = nullptr;
  bool tryRead(SDL_RWops &ops, const char *ext) override {
    if (IMG_isGIF(&ops)) { // animated GIFs go through libnsgif on the game thread
      SDL_RWclose(&ops);
      return true;
    }
    surface = IMG_LoadTyped_RW(&ops, 1, ext);
    return surface != nullptr;
  }
};

struct SoundHandler : FileSystem::OpenHandler {
  Sound *out;
  bool ok = false;
  explicit SoundHandler(Sound *out) : out(out) {}
  bool tryRead(SDL_RWops &ops, const char *ext) override {
    Sound_Sample *sample = Sound_NewSample(&ops, ext, 0, 32768);
    if (!sample) {
      SDL_RWclose(&ops);
      return false;
    }
    uint32_t bytes = Sound_DecodeAll(sample);
    if (bytes && sample->buffer) {
      out->pcm.assign(static_cast<uint8_t *>(sample->buffer), static_cast<uint8_t *>(sample->buffer) + bytes);
      out->format = sample->actual.format;
      out->channels = sample->actual.channels;
      out->rate = sample->actual.rate;
      ok = true;
    }
    Sound_FreeSample(sample);
    return ok;
  }
};

void loadHistory(State &s) {
  FILE *f = std::fopen(s.historyPath.c_str(), "rb");
  if (!f)
    return;
  char line[600];
  while (std::fgets(line, sizeof line, f)) {
    char kind;
    double ms;
    int used = 0;
    if (std::sscanf(line, "%c\t%lf\t%n", &kind, &ms, &used) < 2 || !used || (kind != 'I' && kind != 'S'))
      continue;
    std::string name = line + used;
    while (!name.empty() && (name.back() == '\n' || name.back() == '\r'))
      name.pop_back();
    if (!name.empty())
      s.history.push_back({kind, ms, name});
  }
  std::fclose(f);
}

// This session's loads first, in the order they were needed, then older
// ones this session has not reached.
void saveHistory(State &s) {
  std::vector<Entry> merged;
  std::set<std::string> have;
  {
    Lock hold(s.lock);
    if (!s.dirty)
      return;
    s.dirty = false;
    for (auto &e : s.session)
      if (have.insert(std::string(1, e.kind) + key(e.name)).second)
        merged.push_back(e);
    for (auto &e : s.history)
      if (merged.size() < historyMax && have.insert(std::string(1, e.kind) + key(e.name)).second)
        merged.push_back(e);
  }
  std::string temp = s.historyPath + ".part";
  FILE *f = std::fopen(temp.c_str(), "wb");
  if (!f)
    return;
  bool ok = true;
  for (auto &e : merged)
    ok = std::fprintf(f, "%c\t%.1f\t%s\n", e.kind, e.ms, e.name.c_str()) > 0 && ok;
  ok = std::fclose(f) == 0 && ok;
  std::remove(s.historyPath.c_str()); // the Vita will not rename over a file
  if (!ok || std::rename(temp.c_str(), s.historyPath.c_str()) != 0)
    std::remove(temp.c_str());
}

void listAssetFolders(FileSystem &fs) {
  for (const char *top : {"Graphics", "Audio"}) {
    if (!PHYSFS_isDirectory(top))
      continue;
    char **names = PHYSFS_enumerateFiles(top);
    for (char **n = names; n && *n; ++n) {
      std::string dir = std::string(top) + "/" + *n;
      if (PHYSFS_isDirectory(dir.c_str()))
        fs.warmListing(dir.c_str());
    }
    PHYSFS_freeList(names);
  }
}

int runPlan(State &s);

int run(void *) {
#ifdef __vita__
  // Off the game thread's core, below its priority (see start()).
  sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), state->workerMask);
  sceKernelChangeThreadPriority(sceKernelGetThreadId(), 0x10000100 + 16);
  sceKernelDelayThread(1000);
#endif
  try {
    return runPlan(*state);
  } catch (const std::exception &e) {
    std::fprintf(stderr, "Prefetch stopped: %s\n", e.what());
  } catch (...) {
    std::fprintf(stderr, "Prefetch stopped by an unknown error\n");
  }
  return 0;
}

int runPlan(State &s) {
  listAssetFolders(*s.fs);
  std::vector<Entry> plan;
  {
    Lock hold(s.lock);
    plan = s.history;
  }
  for (auto &e : plan) {
    if (e.ms < worthMs)
      continue;
    std::string k = key(e.name);
    {
      Lock hold(s.lock);
      // Wait while too much prepared data is waiting to be taken.
      while (!s.stopping && (e.kind == 'I' ? s.imageBytes >= imageBudget : s.soundBytes >= soundBudget))
        SDL_CondWait(s.changed, s.lock);
      if (s.stopping)
        break;
      if (s.seen.count(k) || s.images.count(k) || s.sounds.count(k))
        continue; // the game got there first
      s.busy = k;
    }
    SDL_Surface *surface = nullptr;
    Sound sound;
    bool ok = false;
    try {
      if (e.kind == 'I') {
        ImageHandler h;
        s.fs->openRead(h, e.name.c_str());
        surface = h.surface;
        if (surface && surface->format->format != SDL_PIXELFORMAT_ABGR8888) {
          SDL_Surface *converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_ABGR8888, 0);
          SDL_FreeSurface(surface);
          surface = converted;
        }
        ok = surface != nullptr;
      } else {
        SoundHandler h(&sound);
        s.fs->openRead(h, e.name.c_str());
        ok = h.ok;
      }
    } catch (...) {
      ok = false; // a file that no longer exists: the game will report it itself
    }
    Lock hold(s.lock);
    s.busy.clear();
    if (ok && !s.stopping && !s.seen.count(k)) {
      if (e.kind == 'I') {
        s.imageBytes += size_t(surface->w) * surface->h * 4;
        s.images[k] = {surface, e.ms};
        surface = nullptr;
      } else {
        s.soundBytes += sound.pcm.size();
        s.sounds[k] = std::move(sound);
      }
    }
    if (surface)
      SDL_FreeSurface(surface);
    SDL_CondBroadcast(s.changed);
  }
  // Keep the history current while the game runs.
  for (;;) {
    {
      Lock hold(s.lock);
      if (!s.stopping)
        SDL_CondWaitTimeout(s.changed, s.lock, 3000);
      if (s.stopping)
        break;
    }
    saveHistory(s);
  }
  saveHistory(s);
  return 0;
}

// Every load marks the file as used (the worker then leaves it alone); only
// slow ones go into the history. A taken file keeps its saved cost (ms < 0).
void note(char kind, const std::string &name, double ms) {
  if (!state)
    return;
  std::string k = key(name);
  Lock hold(state->lock);
  if (!state->seen.insert(k).second)
    return;
  if (ms < 0)
    for (auto &e : state->history)
      if (e.kind == kind && key(e.name) == k) {
        ms = e.ms;
        break;
      }
  if (ms < worthMs)
    return;
  state->session.push_back({kind, ms, name});
  state->dirty = true;
}
} // namespace

void start(FileSystem &fs, const std::string &gameKey, const std::string &historyDir) {
  if (state)
    return;
  state = new State;
  state->fs = &fs;
  char file[32];
  std::snprintf(file, sizeof file, "%08lx.txt", (unsigned long)crc32(0, (const Bytef *)gameKey.data(), gameKey.size()));
  state->historyPath = historyDir + file;
  loadHistory(*state);
#ifdef __vita__
  // The game thread lands on any of the three user cores (it differs from
  // boot to boot). Keep it on the one it is on, and give the worker the
  // other two, so preparing files never takes time from the game.
  {
    SceKernelThreadInfo info{};
    info.size = sizeof(info);
    int core = 0;
    if (sceKernelGetThreadInfo(sceKernelGetThreadId(), &info) >= 0 && info.currentCpuId >= 0 && info.currentCpuId <= 2)
      core = info.currentCpuId;
    const int all = SCE_KERNEL_CPU_MASK_USER_0 | SCE_KERNEL_CPU_MASK_USER_1 | SCE_KERNEL_CPU_MASK_USER_2;
    const int mine = SCE_KERNEL_CPU_MASK_USER_0 << core;
    sceKernelChangeThreadCpuAffinityMask(sceKernelGetThreadId(), mine);
    state->workerMask = all & ~mine;
  }
#endif
  // A pthread (SDL's locks need one); it moves itself to core 2 in run().
  state->thread = SDL_CreateThreadWithStackSize(run, "prefetch", 256 * 1024, nullptr);
  if (!state->thread) {
    std::fprintf(stderr, "Prefetch: no thread\n");
    delete state;
    state = nullptr;
  }
}

void stop() {
  if (!state)
    return;
  {
    Lock hold(state->lock);
    state->stopping = true;
  }
  SDL_CondBroadcast(state->changed);
  SDL_WaitThread(state->thread, nullptr);
  for (auto &i : state->images)
    SDL_FreeSurface(i.second.surface);
  for (auto &i : state->kept)
    SDL_FreeSurface(i.surface);
  delete state;
  state = nullptr;
}

SDL_Surface *takeImage(const std::string &name) {
  if (!state)
    return nullptr;
  std::string k = key(name);
  SDL_Surface *surface = nullptr;
  {
    Lock hold(state->lock);
    while (state->busy == k)
      SDL_CondWait(state->changed, state->lock);
    auto found = state->images.find(k);
    if (found != state->images.end()) {
      surface = found->second.surface;
      const double ms = found->second.ms;
      state->images.erase(found);
      state->imageBytes -= size_t(surface->w) * surface->h * 4;
      // Keep the original of an expensive one for the next load; the game
      // gets a copy.
      if (ms >= keepMs)
        if (SDL_Surface *copy = copyOf(surface)) {
          keepLocked(*state, k, surface, ms);
          surface = copy;
        }
    } else {
      for (auto it = state->kept.begin(); it != state->kept.end(); ++it)
        if (it->key == k) {
          state->kept.splice(state->kept.begin(), state->kept, it);
          surface = copyOf(it->surface);
          break;
        }
    }
  }
  if (!surface)
    return nullptr;
  SDL_CondBroadcast(state->changed);
  note('I', name, -1);
  return surface;
}

bool takeSound(const std::string &name, Sound &out) {
  if (!state)
    return false;
  std::string k = key(name);
  {
    Lock hold(state->lock);
    while (state->busy == k)
      SDL_CondWait(state->changed, state->lock);
    auto found = state->sounds.find(k);
    if (found == state->sounds.end())
      return false;
    out = std::move(found->second);
    state->sounds.erase(found);
    state->soundBytes -= out.pcm.size();
  }
  SDL_CondBroadcast(state->changed);
  note('S', name, -1);
  return true;
}

void keepImage(const std::string &name, SDL_Surface *surface, double ms) {
  if (!state || ms < keepMs || !surface || surface->format->format != SDL_PIXELFORMAT_ABGR8888)
    return;
  if (SDL_Surface *copy = copyOf(surface)) {
    Lock hold(state->lock);
    keepLocked(*state, key(name), copy, ms);
  }
}

void noteImage(const std::string &name, double ms) {
  note('I', name, ms);
}
void noteSound(const std::string &name, double ms) {
  note('S', name, ms);
}
} // namespace AssetPrefetch
