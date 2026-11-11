#pragma once
// Loads images and sound effects on another core before the game asks for
// them, so a menu, battle or effect does not stop the game while a file is
// found and decoded (on the Vita a 1 MB PNG takes ~400 ms, a short sound
// effect 30-100 ms, much of it locating the file).
//
// What to load comes from the game's own history: every image or sound
// effect whose first load was slow is noted, in the order it was first
// needed, in a small file per game. On the next boot a worker thread lists
// the asset folders, then decodes those files in that order, keeping at most
// a fixed amount of decoded data waiting. The game thread takes a prepared
// file instead of loading it; if the worker is busy with exactly that file,
// it waits for it rather than decoding it twice.
#include <SDL_surface.h>
#include <cstdint>
#include <string>
#include <vector>

class FileSystem;

namespace AssetPrefetch {
// After the game's folders are mounted. gameKey identifies the game (its
// folder); history lives in historyDir.
void start(FileSystem &fs, const std::string &gameKey, const std::string &historyDir);
void stop();

// Where a prepared image was read from, so the game thread need not look
// the file up again (size -1: not known).
struct FileInfo {
  std::string path;
  int64_t size = -1, mtime = -1;
};

// A prepared image for this name, already in ABGR8888, or null. The caller
// owns the surface. file (if given) receives where it was read from.
SDL_Surface *takeImage(const std::string &name, FileInfo *file = nullptr);

struct Sound {
  std::vector<uint8_t> pcm;
  uint32_t format = 0; // SDL audio format
  uint8_t channels = 0;
  uint32_t rate = 0;
};
// A decoded sound effect for this name; false when none is prepared.
bool takeSound(const std::string &name, Sound &out);

// After the game thread decoded an image itself (ABGR8888, before it is
// uploaded): keeps a copy when it was expensive, for the next load.
void keepImage(const std::string &name, SDL_Surface *surface, double ms, const FileInfo &file = FileInfo());

// The game will soon load this image ('I') or sound effect ('S'): prepare it
// on the worker if it is not ready already. An urgent one goes first (the
// game is about to use it). Any thread; cheap.
void request(char kind, const std::string &name, bool urgent = false);

// Called by the game thread after a load it had to do itself.
void noteImage(const std::string &name, double ms);
void noteSound(const std::string &name, double ms);

// Memory is short: free everything prepared or kept (it can all be loaded
// again) and prepare nothing more this session. Any thread.
void releaseMemory();

// Held by the game thread while it loads a file itself. The worker reads its
// files in pieces and waits between them while one is held: the memory card
// serves one read at a time, and a 27 KB fog waited 230 ms behind the
// worker's reads of a large tileset.
struct ForegroundLoad {
  ForegroundLoad();
  ~ForegroundLoad();
  ForegroundLoad(const ForegroundLoad &) = delete;
  ForegroundLoad &operator=(const ForegroundLoad &) = delete;
};
// The game thread is busy with work of its own that the worker slows down
// (load_data: on the Vita, decoding on another core made BLACK SOULS II's
// database load 3.5 s slower, Classes 100 ms -> 840 ms). While held, and for
// a moment after, the worker starts no new file. Calls nest; any thread.
void holdWorker();
void releaseWorker();
// Vita: the user cores other than the one start() pinned the game thread to
// (0 before start). Busy helper threads move there so they never hold the
// game's core.
int otherCoresMask();
// Loads under this cost are not worth preparing.
constexpr double worthMs = 15.0;
} // namespace AssetPrefetch
