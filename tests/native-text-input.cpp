#include "native_launcher/text_input.h"
#include <cassert>
#include <stdexcept>

int main() {
  for (auto s : {std::string(""), std::string("The Witch’s House"),
                 std::string("Pokémon 日本語 \xf0\x9f\x8e\xae")}) {
    auto text = hardrpg::keyboardText(s);
    assert(hardrpg::keyboardResult(text.data(), text.size()) == s);
  }
  auto capped =
      hardrpg::keyboardText(std::string(127, 'a') + "\xf0\x9f\x8e\xae");
  assert(capped.size() == 128 && capped.back() == 0);
  auto empty = hardrpg::keyboardText("hello", 0);
  assert(empty.size() == 1 && empty[0] == 0);
  for (auto s : {std::string("\xff"), std::string("\xc0\x80"),
                 std::string("\xed\xa0\x80"), std::string("\0", 1)}) {
    bool rejected = false;
    try {
      hardrpg::keyboardText(s);
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    assert(rejected);
  }
  for (auto text :
       {std::vector<uint16_t>{0xd800, 0}, std::vector<uint16_t>{0xdc00, 0},
        std::vector<uint16_t>{'a'}}) {
    bool rejected = false;
    try {
      hardrpg::keyboardResult(text.data(), text.size());
    } catch (const std::runtime_error &) {
      rejected = true;
    }
    assert(rejected);
  }
}
