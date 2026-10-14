#pragma once

namespace hardrpg {
struct Engine {
  const char *id, *label, *badge, *executable;
  int format;
};
// Format 4 covers LCF projects. EasyRPG determines 2000/2003 from the database.
inline const Engine *engineFor(int format) {
  static const Engine engines[] = {
      {"mkxp-z", "XP", "XP", "app0:/eboot.bin", 1},
      {"mkxp-z", "VX", "VX", "app0:/eboot.bin", 2},
      {"mkxp-z", "VX Ace", "ACE", "app0:/eboot.bin", 3},
      {"easyrpg", "2000/2003", "2K/2K3", "app0:/easyrpg.self", 4}};
  for (const auto &engine : engines)
    if (engine.format == format)
      return &engine;
  return nullptr;
}
constexpr int engineFilterCount = 5; // All + the four format families.
constexpr int rtpPackCount = 5;
} // namespace hardrpg
