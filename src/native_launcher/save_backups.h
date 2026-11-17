#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hardrpg {
// Save backups: copies of each game's saves kept outside the game, in
// <root>/save-backups/<settings key>/ (see launcher/save_backup.rb, which
// keeps the save a game replaces, for the folder layout). Here the launcher
// records the game (game.json), copies in saves that changed outside the
// game's own save code, lists them, restores them and prunes them.
//
// Every call works on the files alone, so a worker thread may run sync()
// while the screen runs, as long as nothing else touches that game.
struct SaveVersion {
  std::string file; // the kept copy (full path)
  std::string why;  // save, library, manual, restore, removed
  std::string mapName, leader;
  int level = 0;
  int64_t saved = 0, kept = 0, size = 0, playtime = 0;
};
struct SaveSlot {
  std::string id;     // the folder inside the game's backups
  std::string name;   // the save's file name ("Save03.rvdata2")
  int number = -1;    // 3 for Save03; -1 when the name has no number
  std::string target; // where the game keeps it (full path), "" if unknown
  bool present = false; // the game has the file now
  SaveVersion now;      // the file in the game, when present; after a restore
                        // its saved is the restored version's own time
  std::vector<SaveVersion> versions; // newest first
  // The version that holds what the game had last: what "Restore missing
  // saves" puts back (the newest by save time is not it after a restore).
  int last = 0;
};
struct BackupGame {
  std::string key, name, folder;
  int engine = 0; // RGSS 1-3, 4 for EasyRPG
  bool installed = false;
  int slots = 0;
  int64_t bytes = 0, newest = 0;
};
class SaveBackups {
public:
  explicit SaveBackups(const std::string &root = "ux0:/data/hardrpg");
  std::string root;
  std::string dir(const std::string &key) const;
  // Records the game: its name, engine and folder (the folder the game runs
  // in) and, for EasyRPG, its save folder. enabled and keep go to
  // save_backup.rb.
  void record(const std::string &key, const std::string &name, int engine, const std::string &folder,
              const std::string &saveFolder, bool enabled, int keep) const;
  // Copies each save file that differs from its newest kept version: the
  // files save_backup.rb has seen, and save files in the game folder (or
  // the EasyRPG save folder). Returns how many were copied.
  int sync(const std::string &key, const std::string &why = "library", int keep = 0) const;
  std::vector<SaveSlot> slots(const std::string &key) const;
  // Puts a kept version back in place of the game's file. The file it
  // replaces is kept first, so a restore can be undone.
  void restore(const std::string &key, const SaveSlot &, const SaveVersion &) const;
  void drop(const SaveVersion &) const;
  std::vector<BackupGame> games() const;
  BackupGame game(const std::string &key) const;
  // Deletes every backup of a game.
  void forget(const std::string &key) const;
  // Moves a removed game's backups to the key of the same game added again
  // from elsewhere. Slots the new key already has stay as they are.
  void adopt(const std::string &from, const std::string &to) const;
  // The game started last, so its saves are copied in once the launcher is
  // back (also after a crash or a closed app).
  void markRunning(const std::string &key) const;
  std::string takeRunning() const;
  int keep(const std::string &key) const;
  static const int keepChoices[4];
  static constexpr int defaultKeep = 5;
  static constexpr int gap = 600; // seconds; see save_backup.rb
};
// "Today 21:05", "Yesterday 23:50", "Nov 12 18:04" (local time).
std::string backupTime(int64_t when);
std::string backupSize(int64_t bytes);
} // namespace hardrpg
