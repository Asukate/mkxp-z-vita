// SPDX-License-Identifier: GPL-3.0-or-later
// Launch request for the Dante 98 backend (OpenDT). Written by the launcher to
// ux0:/data/hardrpg/selection.json and checked again by app0:/opendt.self.
// A game is either a folder holding the game's files, or a disk image with
// the game inside it ("disk", plus "folder" inside the image).
#pragma once
#include "easyrpg_handoff.h" // safeRelative, storagePath, readEasyRpgRequest
#include "util/json5pp.hpp"
#include <stdexcept>
#include <string>
#include <vector>

namespace hardrpg {
struct OpenDtRequest {
  // config: the game's 16-hex key (its settings file config/<key>.json).
  std::string project, disk, folder, save, config;
};

inline OpenDtRequest openDtRequest(
    const std::string &encoded, const std::string &root = "ux0:/data/hardrpg",
    const std::vector<std::string> &storage = {"ux0:/", "uma0:/", "imc0:/"}) {
  if (encoded.empty() || encoded.size() > 4096)
    throw std::runtime_error("Invalid Dante 98 launch request size");
  auto request = json5pp::parse5(encoded);
  const auto &v = request.as_object();
  auto text = [&](const char *name, bool optional = false) -> std::string {
    auto i = v.find(name);
    if (i == v.end() && optional)
      return "";
    if (i == v.end() || !i->second.is_string())
      throw std::runtime_error("Invalid Dante 98 launch request field");
    return i->second.as_string();
  };
  auto schema = v.find("schema");
  if (text("backend") != "opendt" || schema == v.end() ||
      !schema->second.is_integer() || schema->second.as_integer() != 1)
    throw std::runtime_error("Unsupported Dante 98 launch request");
  auto id = text("config");
  if (id.size() != 16 || id.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("Invalid Dante 98 save identity");
  OpenDtRequest r;
  r.project = text("project");
  r.save = text("save");
  r.config = id;
  if (!storagePath(r.project, storage) || r.save != root + "/saves/opendt/" + id)
    throw std::runtime_error("Invalid Dante 98 launch path");
  auto disk = text("disk", true), folder = text("folder", true);
  if (!disk.empty()) {
    if (!safeRelative(disk))
      throw std::runtime_error("Invalid Dante 98 disk path");
    r.disk = r.project + "/" + disk;
    if (!folder.empty() && !safeRelative(folder))
      throw std::runtime_error("Invalid Dante 98 folder in the disk image");
    r.folder = folder;
  } else if (!folder.empty())
    throw std::runtime_error("Invalid Dante 98 launch request");
  return r;
}
} // namespace hardrpg
