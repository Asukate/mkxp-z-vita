#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hardrpg {
class View;
// Shows the launcher's texts in the language chosen in the launcher.
void useLauncherLanguage();
// The menu over a running game, opened with L+R+Start or the icon in the
// screen's empty border. It is the launcher's page for this game, cut down
// to Resume, Controls, Display, Performance and speed and Back to library,
// drawn by the launcher's own view over the paused game. The game's script
// waits inside Graphics.update while it is open.
//
// This class holds no GL state: the engine draws frame()'s quads itself on
// the GPU, as the launcher does, so moving the focus costs little.
class GameMenu {
public:
  GameMenu();
  ~GameMenu();
  // configPath is the game's settings file the engine was started with,
  // ux0:/data/hardrpg/config/<key>.json. False when that game is not the
  // one the launcher started last (then there is nothing to edit).
  bool open(const std::string &configPath);
  bool isOpen() const;
  // Reads the buttons and the front screen once and acts on them.
  void step();
  enum Exit { Staying, Resume, BackToLibrary };
  Exit exit() const;
  // A setting changed since the last call: the engine applies it.
  bool takeChanged();
  // This game's settings as the launcher sees them (its own over the
  // defaults): fixedAspectRatio, integerScalingActive, smoothScaling...
  std::string valuesJson() const;
  // What to draw over the game: the launcher view's quads (960x544 space,
  // premultiplied images), rebuilt when something changed. Null while closed.
  // The images belong to the view and stay valid until the next call.
  const View *frame();
  // Frees the view, its font and the picture.
  void close();

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace hardrpg
