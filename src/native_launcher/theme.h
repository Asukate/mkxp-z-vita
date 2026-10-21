#pragma once
#include <string>

namespace hardrpg {
// A game's title music, found in the game's own files.
//
// RPG Maker XP/VX/VX Ace: the title BGM named in Data/System.rxdata,
// .rvdata or .rvdata2 (loose or inside the encrypted archive), played from
// Audio/BGM. RPG Maker 2000/2003: the title music in RPG_RT.ldb, from Music/.
// A "hardrpg-theme.ogg" (or .mp3/.wav) in the game folder overrides both.
// When the named track is missing (it may be in the RTP) or is MIDI, a
// track with "title" in its name is used instead, if there is one.
struct Theme {
  std::string file;  // a loose file, streamed from disk, or
  std::string bytes; // the whole track, read out of an encrypted archive
  std::string ext;   // ".ogg", ".mp3" or ".wav"
  std::string name;  // the track the game names, for logs
  int volume = 100;  // the game's own volume for it, in percent
};
// gameDir is the game's folder (for a ZIP game, its unpacked copy).
bool findTheme(const std::string &gameDir, Theme &out);

// The pieces, exposed for tests. Both return the track name, or "".
std::string marshalTitleBgm(const std::string &systemData, int &volume);
std::string lcfTitleMusic(const std::string &database, int &volume);
} // namespace hardrpg
