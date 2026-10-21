#pragma once
#include "image.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hardrpg {
// Downloads cover, background and logo art for games, on its own thread.
//
// Sources, asked in turn for whatever is still missing:
//   SteamGridDB  covers, backgrounds, logos for most games; needs a key in
//                steamgriddb-key.txt in the data folder
//   Steam        library art and descriptions for games sold on Steam; no key
//   RAWG         release year, genres, developer, a background; needs a key
//                in rawg-key.txt
//   VNDB         Japanese and doujin games (many RPG Maker ones); no key
//
// Art is written to art/<folder>/cover|hero|logo.<png|jpg|webp>, facts to
// art/<folder>/info.json. Pictures a game already has are never replaced.
class ArtFetch {
public:
  struct Job {
    std::string key;     // the game's activity key, reported back when done
    std::string name;    // what to search for
    std::string folder;  // where to write, under the art root
    std::vector<std::string> existing; // folders that may already hold art
  };
  struct Progress {
    int done = 0, total = 0, found = 0;
    std::string current, note;
    std::vector<std::string> updated; // keys whose art changed since last poll
    bool finished = false;
    bool choosing = false; // waiting for choose(); takeChoices() has the options
  };
  // A single-game lookup offers each source's pictures beside the current
  // ones and waits for the player to pick.
  struct Candidate {
    std::string source; // "Current", "Steam", "SteamGridDB", "RAWG", "VNDB"
    Image thumb;
    bool current = false;
    bool adult = false; // shown blurred while adult artwork is blurred
  };
  struct Choices {
    std::string key, name;
    std::vector<Candidate> options[3]; // cover, background, logo
  };
  ~ArtFetch();
  // strict: a whole-library run. Only exact name matches are taken, sources
  // are blended without asking, and only missing pictures are filled in.
  // dataRoot ends in '/' and holds the key files.
  void start(std::vector<Job> jobs, const std::string &artRoot, const std::string &dataRoot, bool strict);
  bool running() const;
  // Copies the progress and hands over the updated keys.
  Progress poll();
  void cancel();
  bool takeChoices(Choices &out);
  // pick[k] indexes options[k]; -1 keeps what the game has.
  void choose(const int pick[3]);
  // For tests: answers every request instead of the network. Arguments: the
  // URL, the JSON body of a POST (empty for a GET), and the reply to fill.
  using Transport = std::function<bool(const std::string &, const std::string &, std::string &)>;
  static void setTransport(Transport transport);

private:
  struct State;
  std::shared_ptr<State> state;
};
} // namespace hardrpg
