#include "art_loader.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <dirent.h>
#include <sys/stat.h>
#include <chrono>

namespace hardrpg {
namespace {
// Memory card folder names ignore case.
std::string folded(std::string s) {
  for (auto &c : s)
    c = char(std::tolower(static_cast<unsigned char>(c)));
  return s;
}
void nap() {
#ifdef __vita__
  sceKernelDelayThread(6000);
#else
  std::this_thread::sleep_for(std::chrono::milliseconds(6));
#endif
}
// Requests not wanted for this many frames are dropped from the queue.
constexpr uint64_t staleFrames = 20;
// Bump to throw away cached generated covers when their look changes.
constexpr int generatedLook = 1;
std::string hashed(const std::string &s) {
  uint64_t h = 1469598103934665603ull;
  for (unsigned char c : s)
    h = (h ^ c) * 1099511628211ull;
  char buf[17];
  std::snprintf(buf, sizeof buf, "%016llx", (unsigned long long)h);
  return buf;
}
// The cache name of a picture: its path, size and time, so a changed file
// is decoded again.
bool source(const std::string &path, std::string &stamp) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
    return false;
  stamp = path + "|" + std::to_string((long long)st.st_size) + "|" + std::to_string((long long)st.st_mtime);
  return true;
}
void makeDirs(const std::string &dir) {
  for (size_t at = dir.find('/', 1); at != std::string::npos; at = dir.find('/', at + 1))
    mkdir(dir.substr(0, at).c_str(), 0777);
}
} // namespace

ArtLoader::ArtLoader() {
#ifdef __vita__
  // Core 2 is free while the launcher runs; the UI draws on core 0.
  thread = sceKernelCreateThread("hardrpg_art_loader", entry, 0x10000100, 256 * 1024, 0, SCE_KERNEL_CPU_MASK_USER_2, nullptr);
  auto self = this;
  if (thread >= 0 && sceKernelStartThread(thread, sizeof(self), &self) < 0) {
    sceKernelDeleteThread(thread);
    thread = -1;
  }
#else
  thread = std::thread([this] { run(); });
#endif
}
ArtLoader::~ArtLoader() {
  stop = true;
#ifdef __vita__
  if (thread >= 0) {
    sceKernelWaitThreadEnd(thread, nullptr, nullptr);
    sceKernelDeleteThread(thread);
  }
#else
  thread.join();
#endif
}
#ifdef __vita__
int ArtLoader::entry(SceSize, void *argument) {
  (*static_cast<ArtLoader **>(argument))->run();
  sceKernelExitThread(0);
  return 0;
}
#endif

void ArtLoader::beginFrame() {
  std::lock_guard<std::mutex> hold(lock);
  ++frame;
}
void ArtLoader::want(const Request &r, double distance) {
  std::lock_guard<std::mutex> hold(lock);
  auto id = r.key + (r.shots ? "|shots" : r.hero ? "|hero" : "|cover");
  if (active.count(id))
    return;
  auto found = queue.find(id);
  if (found == queue.end())
    queue.emplace(id, Pending{r, distance, frame});
  else {
    found->second.distance = distance;
    found->second.frame = frame;
  }
}
bool ArtLoader::take(Result &out) {
  std::lock_guard<std::mutex> hold(lock);
  if (done.empty())
    return false;
  out = std::move(done.front());
  done.pop_front();
  active.erase(out.key + (out.shots ? "|shots" : out.hero ? "|hero" : "|cover"));
  return true;
}
bool ArtLoader::busy() {
  std::lock_guard<std::mutex> hold(lock);
  return !queue.empty() || working || !done.empty();
}
void ArtLoader::store(const std::string &path, Image image) {
  if (path.empty())
    return;
  std::lock_guard<std::mutex> hold(lock);
  saves.push_back({path, std::move(image)});
}
void ArtLoader::rescan(const std::string &key) {
  rescanWanted = true;
  std::lock_guard<std::mutex> hold(lock);
  // A request already decoding would bring back the old art.
  queue.erase(key + "|cover");
  queue.erase(key + "|hero");
}

void ArtLoader::run() {
  while (!stop) {
    Request next;
    bool have = false;
    {
      std::lock_guard<std::mutex> hold(lock);
      // Drop what scrolled away, then take the newest, nearest request.
      for (auto it = queue.begin(); it != queue.end();) {
        if (frame - it->second.frame > staleFrames) {
          Result gone;
          gone.key = it->second.request.key;
          gone.hero = it->second.request.hero;
          gone.shots = it->second.request.shots;
          gone.cancelled = true;
          done.push_back(std::move(gone));
          it = queue.erase(it);
        } else
          ++it;
      }
      auto best = queue.end();
      for (auto it = queue.begin(); it != queue.end(); ++it)
        if (best == queue.end() || it->second.frame > best->second.frame ||
            (it->second.frame == best->second.frame && it->second.distance < best->second.distance))
          best = it;
      if (best != queue.end()) {
        next = std::move(best->second.request);
        active.insert(best->first);
        queue.erase(best);
        ++working;
        have = true;
      }
    }
    if (!have) {
      std::pair<std::string, Image> save;
      bool saving = false;
      {
        std::lock_guard<std::mutex> hold(lock);
        if (!saves.empty()) {
          save = std::move(saves.front());
          saves.pop_front();
          saving = true;
        }
      }
      if (saving) {
        makeDirs(save.first.substr(0, save.first.find_last_of('/') + 1));
        save.second.saveJpeg(save.first);
      } else
        nap();
      continue;
    }
    Result r = load(next);
    std::lock_guard<std::mutex> hold(lock);
    done.push_back(std::move(r));
    --working;
  }
}

ArtLoader::Result ArtLoader::load(const Request &q) {
  Result r;
  r.key = q.key;
  r.hero = q.hero;
  r.shots = q.shots;
  if (rescanWanted.exchange(false))
    foldersRead = false;
  if (!foldersRead) {
    foldersRead = true;
    folders.clear();
    foldersOk = false;
    if (DIR *d = opendir(q.root.c_str())) {
      foldersOk = true;
      while (auto *e = readdir(d))
        folders.insert(folded(e->d_name));
      closedir(d);
    }
  }
  // The first art folder holding this picture, in any supported format.
  auto find = [&](const char *base, std::string &path, std::string &stamp) {
    for (auto &name : q.names) {
      if (foldersOk && !folders.count(folded(name)))
        continue;
      for (const char *ext : {".png", ".jpg", ".jpeg", ".webp", ".json"}) {
        path = q.root + name + "/" + base + ext;
        if (source(path, stamp))
          return true;
      }
    }
    return false;
  };
  auto cached = [&](const std::string &file) {
    if (cacheMade != q.cache) {
      makeDirs(q.cache);
      cacheMade = q.cache;
    }
    return q.cache + file;
  };
  std::string path, stamp;
  // Downloaded facts, and whether the game is marked adult.
  if (find("info", path, stamp)) {
    if (FILE *f = std::fopen(path.c_str(), "rb")) {
      char buf[8192];
      r.info.assign(buf, std::fread(buf, 1, sizeof buf, f));
      std::fclose(f);
    }
  }
  bool blur = q.blurAdult && r.info.find("\"adult\": true") != std::string::npos;
  auto size = "|" + std::to_string(q.width) + "x" + std::to_string(q.height) + (blur ? "|blur" : "");
  auto scaled = [&](const std::string &file) {
    Image img = Image::load(file).cover(q.width, q.height);
    return blur ? img.blurred(std::max(6, q.width / 24)) : img;
  };
  if (q.shots) {
    for (int i = 1; i <= 4; ++i) {
      if (!find(("shot" + std::to_string(i)).c_str(), path, stamp))
        break;
      auto file = cached("s-" + hashed(stamp + size) + ".jpg");
      Image img = Image::load(file);
      if (img.empty()) {
        img = scaled(path);
        if (!img.empty())
          img.saveJpeg(file);
      }
      if (!img.empty())
        r.pictures.push_back(std::move(img));
    }
    return r;
  }
  if (q.hero) {
    if (find("hero", path, stamp)) {
      auto file = cached("h-" + hashed(stamp + size) + ".jpg");
      r.background = Image::load(file);
      if (r.background.empty()) {
        r.background = scaled(path);
        r.background.saveJpeg(file);
      }
    }
    return r;
  }
  if (find("cover", path, stamp)) {
    auto file = cached("c-" + hashed(stamp + size) + ".jpg");
    r.cover = Image::load(file);
    if (r.cover.empty()) {
      r.cover = scaled(path);
      r.cover.saveJpeg(file);
    }
  }
  if (r.cover.empty()) {
    r.generatedPath = cached("g-" + hashed(q.title + size + "|" + std::to_string(generatedLook)) + ".jpg");
    r.cover = Image::load(r.generatedPath);
    if (!r.cover.empty())
      r.generatedPath.clear();
  }
  if (find("logo", path, stamp)) {
    auto file = cached("l-" + hashed(stamp) + ".raw");
    r.logo = Image::loadRaw(file);
    if (r.logo.empty()) {
      Image l = Image::load(path);
      if (!l.empty()) {
        double k = std::min({1.0, 380.0 / l.width, 70.0 / l.height});
        r.logo = k < 1 ? l.resized(std::max(1, int(l.width * k)), std::max(1, int(l.height * k))) : std::move(l);
        r.logo.saveRaw(file);
      }
    }
  }
  return r;
}
} // namespace hardrpg
