// SPDX-License-Identifier: GPL-3.0-or-later
// Launch request for the PC-98 (NP2kai) backend. Written by the launcher to
// ux0:/data/hardrpg/selection.json and checked again by app0:/np2kai.self.
#pragma once
#include "easyrpg_handoff.h" // safeRelative, storagePath, readEasyRpgRequest
#include "util/json5pp.hpp"
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace hardrpg {
struct Np2kaiRequest {
  std::string project, disk, font, save;
  std::vector<std::pair<std::string, std::string>> options;
};

inline bool np2kaiOptionSafe(const std::string &key, const std::string &value) {
  if (key.size() > 48 || key.compare(0, 7, "np2kai_") || value.empty() ||
      value.size() > 48)
    return false;
  for (unsigned char c : key)
    if (!(c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9')))
      return false;
  for (unsigned char c : value)
    if (c < 32 || c == 127 || c == '"' || c == '\\')
      return false;
  return true;
}

// A file named by a path relative to the game folder, or "" when absent.
inline std::string np2kaiInside(const std::string &project, const std::string &relative) {
  if (relative.empty())
    return "";
  if (!safeRelative(relative))
    throw std::runtime_error("Invalid PC-98 file path");
  return project + "/" + relative;
}

inline Np2kaiRequest np2kaiRequest(
    const std::string &encoded, const std::string &root = "ux0:/data/hardrpg",
    const std::vector<std::string> &storage = {"ux0:/", "uma0:/", "imc0:/"}) {
  if (encoded.empty() || encoded.size() > 4096)
    throw std::runtime_error("Invalid PC-98 launch request size");
  auto request = json5pp::parse5(encoded);
  const auto &v = request.as_object();
  auto text = [&](const char *name, bool optional = false) -> std::string {
    auto i = v.find(name);
    if (i == v.end() && optional)
      return "";
    if (i == v.end() || !i->second.is_string())
      throw std::runtime_error("Invalid PC-98 launch request field");
    return i->second.as_string();
  };
  auto schema = v.find("schema");
  if (text("backend") != "np2kai" || schema == v.end() ||
      !schema->second.is_integer() || schema->second.as_integer() != 1)
    throw std::runtime_error("Unsupported PC-98 launch request");
  auto id = text("config");
  if (id.size() != 16 ||
      id.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("Invalid PC-98 save identity");
  Np2kaiRequest r;
  r.project = text("project");
  r.save = text("save");
  if (!storagePath(r.project, storage) || r.save != root + "/saves/np2kai/" + id)
    throw std::runtime_error("Invalid PC-98 launch path");
  r.disk = np2kaiInside(r.project, text("disk"));
  if (r.disk.empty())
    throw std::runtime_error("The PC-98 game has no disk image");
  r.font = np2kaiInside(r.project, text("font", true));
  auto options = v.find("options");
  if (options != v.end()) {
    if (!options->second.is_object() || options->second.as_object().size() > 32)
      throw std::runtime_error("Invalid PC-98 options");
    for (const auto &o : options->second.as_object()) {
      if (!o.second.is_string() || !np2kaiOptionSafe(o.first, o.second.as_string()))
        throw std::runtime_error("Invalid PC-98 option");
      r.options.emplace_back(o.first, o.second.as_string());
    }
  }
  return r;
}
} // namespace hardrpg
