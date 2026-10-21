#pragma once
#include "sounds.h"
#include <memory>
#include <string>

namespace hardrpg {
// The launcher's own sound output: interface sounds and a game's title
// music. It runs a mixing thread and never touches OpenAL, which games
// start on their own after the launcher has handed over.
//
// On the Vita it writes to a BGM audio port; on the desktop to SDL audio.
class LauncherAudio {
public:
  LauncherAudio();
  ~LauncherAudio();
  // folder holds move.ogg, select.ogg ... launch.ogg. Returns false when
  // no audio output could be opened; every call is then harmless.
  bool start(const std::string &folder);
  // Stops the threads and releases the output. Called before a game starts.
  void stop();
  void play(Sound);
  // Plays this game's title music (see theme.h), fading out what is playing.
  // An empty gameDir fades the music out. The same gameDir keeps playing.
  void theme(const std::string &gameDir);
  // Levels, 0 to 100.
  void setLevels(int effects, int music);
  // Fades the music out over ms milliseconds, leaving effects playing.
  void fadeMusic(int ms);
  // What theme() last found: the track name, or "" while none plays.
  std::string playingTheme() const;

  struct Impl;

private:
  std::unique_ptr<Impl> impl;
};
} // namespace hardrpg
