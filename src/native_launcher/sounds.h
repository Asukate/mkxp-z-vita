#pragma once

namespace hardrpg {
// Interface sounds. Files: app0:/sfx/<name>.ogg (launcher/sfx in the source).
enum class Sound { Move, Select, Back, Open, Close, Toggle, Error, Launch, Count };
inline const char *soundName(Sound s) {
  static const char *names[] = {"move", "select", "back", "open", "close", "toggle", "error", "launch"};
  return names[int(s)];
}
} // namespace hardrpg
