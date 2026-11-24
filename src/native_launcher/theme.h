#pragma once
#include <string>
#include <vector>

namespace hardrpg {
// A game's title music, found in the game's own files. Nothing is downloaded.
//
// RPG Maker XP/VX/VX Ace: the title BGM named in Data/System.rxdata,
// .rvdata or .rvdata2 (loose or inside the encrypted archive), played from
// Audio/BGM. RPG Maker 2000/2003: the title music in RPG_RT.ldb, from Music/.
// A "hardrpg-theme.ogg" (or .mp3/.wav) in the game folder overrides both.
// A track the game lacks is looked for in the RTP folders; when it is still
// missing or is MIDI, a track with "title" in its name stands in, if any.
struct Theme {
  std::string file;  // a file on disk to play, or
  std::string bytes; // the whole track, when no cache folder was given
  std::string ext;   // ".ogg", ".mp3" or ".wav"
  std::string name;  // the track the game names
  int volume = 100;  // the game's own volume for it, in percent
  int pitch = 100;   // and its pitch (speed), in percent
  std::string note;  // without a track: why, in a few words (for logs)
};
// source is a game folder, an unpacked ZIP game's folder, or a .zip file.
//
// With a cache folder, the answer is kept there (keyed by source, checked
// against the game files' sizes and times), so asking again reads one small
// file; a track inside an encrypted archive or a ZIP is written there once
// and played from disk afterwards. rtps are RTP folders to look in.
bool findTheme(const std::string &source, Theme &out, const std::string &cache = "",
               const std::vector<std::string> &rtps = {});
// Call once on the main thread before findTheme reads ZIPs on another.
void themeInit();

// The pieces, exposed for tests. Both return the track name, or "".
std::string marshalTitleBgm(const std::string &systemData, int &volume, int *pitch = nullptr);
std::string lcfTitleMusic(const std::string &database, int &volume, int *pitch = nullptr);
} // namespace hardrpg
