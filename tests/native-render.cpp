#include "native_launcher/view.h"
#include <cassert>
#include <fstream>

// argv[2] is an isolated fixture directory, never a real game library.
int main(int argc, char **argv) {
  assert(argc == 3);
  hardrpg::Paths paths;
  paths.root = argv[2];
  hardrpg::UI ui(paths);
  auto font = hardrpg::readFile(argv[1], 4194304);
  auto copy = paths.at("font.ttf");
  hardrpg::atomicWrite(copy, font);
  hardrpg::View actual(copy), reference(argv[1]);
  // Any later font-file access would now read invalid font bytes. Labels must
  // still render correctly: the complete face must be resident before drawing.
  std::ofstream(copy, std::ios::binary | std::ios::trunc) << "invalid font";
  ui.model.list = {{"Pokémon Infinite Fusion", hardrpg::Kind::Game, {}, 1},
                   {"The Witch’s House ver. 1.09a with a very long game label "
                    "and more extra text than the available row width",
                    hardrpg::Kind::Game,
                    {},
                    2}};
  ui.focus = hardrpg::Focus::Games;
  for (int frame = 0; frame < 20; ++frame) {
    ui.selected = frame % 2;
    actual.draw(ui);
    reference.draw(ui);
    assert(actual.pixels == reference.pixels);
  }
}
