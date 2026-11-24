#pragma once
#include <string>

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
      {"easyrpg", "2000/2003", "2K/2K3", "app0:/easyrpg.self", 4},
      {"np2kai", "Dante 98", "DANTE 98", "app0:/np2kai.self", 7}};
  for (const auto &engine : engines)
    if (engine.format == format)
      return &engine;
  return nullptr;
}
constexpr int engineFilterCount = 5; // All + the four format families.
// RPG Maker MV and MZ (HTML5 games): found and listed so the player learns
// that HardRPG cannot run them yet, instead of the folder looking empty or
// broken. They have no engine (engineFor returns null) and no tab.
constexpr int formatMV = 5, formatMZ = 6;
inline bool comingSoon(int format) { return format == formatMV || format == formatMZ; }
// PC-98 disk images (RPG Maker Dante 98 / Dante 98 II games), run by NP2kai.
constexpr int formatPC98 = 7, formatLast = formatPC98;
// The Filter panel's engine rows: XP, VX, VX Ace, 2000/2003, Dante 98.
constexpr int filterEngineCount = 5;
inline int filterEngineFormat(int row) { return row < 4 ? row + 1 : formatPC98; }
// Dante 98 games (a kind of PC-98 game) run natively by OpenDT when the
// package has it.
inline const Engine *openDtEngine() {
  static const Engine engine{"opendt", "Dante 98", "DANTE 98", "app0:/opendt.self", formatPC98};
  return &engine;
}
// The program that runs a launch request: a PC-98 game goes to OpenDT or
// NP2kai depending on what the launcher found on its disk.
inline const char *launchExecutable(int format, const std::string &backend) {
  if (backend == "opendt")
    return openDtEngine()->executable;
  const Engine *engine = engineFor(format);
  return engine ? engine->executable : nullptr;
}
// Formats with an engine, in tab order.
constexpr int runnableFormats[] = {1, 2, 3, 4, formatPC98};
constexpr int rtpPackCount = 5;
} // namespace hardrpg
