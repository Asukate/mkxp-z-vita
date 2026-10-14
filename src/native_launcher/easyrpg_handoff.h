// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "util/json5pp.hpp"
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace hardrpg {
// Shared by the Vita entry point and its host protocol tests.
inline bool safeRelative(const std::string &s) {
  if (s.empty())
    return false;
  size_t begin = 0;
  for (unsigned char c : s)
    if (c < 32 || c == 127 || c == '\\' || c == ':')
      return false;
  for (;;) {
    auto end = s.find('/', begin);
    auto part = s.substr(begin, end == std::string::npos ? end : end - begin);
    if (part.empty() || part == "." || part == "..")
      return false;
    if (end == std::string::npos)
      return true;
    begin = end + 1;
  }
}
inline bool storagePath(const std::string &path,
                        const std::vector<std::string> &storage) {
  for (const auto &root : storage)
    if (path.compare(0, root.size(), root) == 0 &&
        safeRelative(path.substr(root.size())))
      return true;
  return false;
}
inline std::vector<std::string> easyRpgArguments(
    const std::string &encoded, const std::string &root = "ux0:/data/hardrpg",
    const std::vector<std::string> &storage = {"ux0:/", "uma0:/", "imc0:/"}) {
  using Json = json5pp::value;
  if (encoded.empty() || encoded.size() > 4096)
    throw std::runtime_error("Invalid EasyRPG launch request size");
  auto request = json5pp::parse5(encoded);
  auto text = [&](const char *name) -> std::string {
    auto i = request.as_object().find(name);
    if (i == request.as_object().end() || !i->second.is_string())
      throw std::runtime_error("Invalid EasyRPG launch request field");
    return i->second.as_string();
  };
  const auto &v = request.as_object();
  auto schema = v.find("schema");
  if (text("backend") != "easyrpg" || schema == v.end() ||
      !schema->second.is_integer() || schema->second.as_integer() != 1)
    throw std::runtime_error("Unsupported EasyRPG launch request");
  auto id = text("config");
  if (id.size() != 16 ||
      id.find_first_not_of("0123456789abcdef") != std::string::npos)
    throw std::runtime_error("Invalid EasyRPG save identity");
  auto project = text("project"), save = text("save");
  if (!storagePath(project, storage) || save != root + "/saves/easyrpg/" + id)
    throw std::runtime_error("Invalid EasyRPG launch path");
  auto source = text("source"), path = text("path");
  if ((source == "games" || source == "cache") && safeRelative(path)) {
    if (project != root + "/" + source + "/" + path)
      throw std::runtime_error("Mismatched EasyRPG project path");
  } else if (source != "external" || path != project)
    throw std::runtime_error("Invalid EasyRPG source");
  std::vector<std::string> args = {"easyrpg-player", "--project-path", project,
                                   "--save-path", save};
  auto rtp = v.find("rtp");
  if (rtp == v.end() || !rtp->second.is_array() ||
      rtp->second.as_array().size() > 2)
    throw std::runtime_error("Invalid EasyRPG RTP list");
  std::vector<std::string> engines;
  for (const auto &p : rtp->second.as_array()) {
    if (!p.is_object())
      throw std::runtime_error("Invalid EasyRPG RTP entry");
    const auto &pack = p.as_object();
    auto engine = pack.find("engine"), path = pack.find("path");
    if (engine == pack.end() || path == pack.end() ||
        !engine->second.is_string() || !path->second.is_string() ||
        (engine->second.as_string() != "2000" && engine->second.as_string() != "2003") ||
        !storagePath(path->second.as_string(), storage))
      throw std::runtime_error("Invalid EasyRPG RTP path");
    auto family = engine->second.as_string();
    for (const auto &seen : engines)
      if (seen == family)
        throw std::runtime_error("Duplicate EasyRPG RTP engine");
    engines.push_back(family);
    args.push_back("--hardrpg-rtp-" + family);
    args.push_back(path->second.as_string());
  }
  return args;
}
inline std::string readEasyRpgRequest(const std::string &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file)
    throw std::runtime_error("Missing EasyRPG launch request");
  std::string text(4097, '\0');
  file.read(&text[0], text.size());
  if (file.bad())
    throw std::runtime_error("Cannot read EasyRPG launch request");
  text.resize(file.gcount());
  return text;
}
} // namespace hardrpg
