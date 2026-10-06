#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace hardrpg {
// The Vita IME exchanges UTF-16, while game labels and JSON use UTF-8.
std::vector<uint16_t> keyboardText(const std::string &, size_t maxUnits = 128);
std::string keyboardResult(const uint16_t *, size_t capacity);
} // namespace hardrpg
