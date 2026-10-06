#include "text_input.h"
#include <stdexcept>

namespace hardrpg {
std::vector<uint16_t> keyboardText(const std::string &text, size_t maxUnits) {
  std::vector<uint16_t> out;
  for (size_t i = 0; i < text.size();) {
    auto first = uint8_t(text[i++]);
    int count = first < 0x80                     ? 0
                : first >= 0xc2 && first <= 0xdf ? 1
                : first >= 0xe0 && first <= 0xef ? 2
                : first >= 0xf0 && first <= 0xf4 ? 3
                                                 : -1;
    if (count < 0)
      throw std::runtime_error("Invalid UTF-8 game name");
    uint32_t cp = count ? first & ((1u << (6 - count)) - 1) : first;
    for (int n = 0; n < count; ++n) {
      if (i >= text.size() || (uint8_t(text[i]) & 0xc0) != 0x80)
        throw std::runtime_error("Invalid UTF-8 game name");
      cp = (cp << 6) | (uint8_t(text[i++]) & 63);
    }
    if (!cp ||
        (count && cp < (count == 1   ? 0x80u
                        : count == 2 ? 0x800u
                                     : 0x10000u)) ||
        cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
      throw std::runtime_error("Invalid Unicode game name");
    size_t units = cp > 0xffff ? 2 : 1;
    if (out.size() + units > maxUnits)
      break; // Bound the initial label without splitting a surrogate pair.
    if (units == 2) {
      cp -= 0x10000;
      out.push_back(0xd800 | (cp >> 10));
      out.push_back(0xdc00 | (cp & 1023));
    } else
      out.push_back(cp);
  }
  out.push_back(0);
  return out;
}
std::string keyboardResult(const uint16_t *text, size_t capacity) {
  std::string out;
  for (size_t i = 0; i < capacity; ++i) {
    uint32_t cp = text[i];
    if (!cp)
      return out;
    if (cp >= 0xd800 && cp <= 0xdbff) {
      if (++i >= capacity || text[i] < 0xdc00 || text[i] > 0xdfff)
        throw std::runtime_error("Invalid keyboard result");
      cp = 0x10000 + ((cp - 0xd800) << 10) + (text[i] - 0xdc00);
    } else if (cp >= 0xdc00 && cp <= 0xdfff)
      throw std::runtime_error("Invalid keyboard result");
    if (cp < 0x80)
      out += char(cp);
    else if (cp < 0x800) {
      out += char(0xc0 | (cp >> 6));
      out += char(0x80 | (cp & 63));
    } else if (cp < 0x10000) {
      out += char(0xe0 | (cp >> 12));
      out += char(0x80 | ((cp >> 6) & 63));
      out += char(0x80 | (cp & 63));
    } else {
      out += char(0xf0 | (cp >> 18));
      out += char(0x80 | ((cp >> 12) & 63));
      out += char(0x80 | ((cp >> 6) & 63));
      out += char(0x80 | (cp & 63));
    }
  }
  throw std::runtime_error("Unterminated keyboard result");
}
} // namespace hardrpg
