#pragma once
#include "util/json5pp.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace hardrpg {
struct ReleaseVersion {
  std::vector<unsigned> core;
  std::vector<std::string> pre;
  bool parse(std::string tag) {
    core.clear();
    pre.clear();
    if (!tag.empty() && tag[0] == 'v')
      tag.erase(0, 1);
    auto plus = tag.find('+');
    if (plus != std::string::npos) {
      auto build = tag.substr(plus + 1);
      if (build.empty() || build.front() == '.' || build.back() == '.' ||
          build.find("..") != std::string::npos ||
          build.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQR"
                                  "STUVWXYZ0123456789-.") != std::string::npos)
        return false;
      tag.resize(plus);
    }
    auto dash = tag.find('-');
    auto base = tag.substr(0, dash);
    std::stringstream numbers(base);
    std::string part;
    while (std::getline(numbers, part, '.')) {
      if (part.empty() || part.size() > 9 ||
          (part.size() > 1 && part[0] == '0') ||
          part.find_first_not_of("0123456789") != std::string::npos)
        return false;
      core.push_back(unsigned(std::stoul(part)));
    }
    if (core.size() != 3 || base.empty() || base.back() == '.')
      return false;
    if (dash != std::string::npos) {
      auto suffix = tag.substr(dash + 1);
      std::stringstream ids(suffix);
      if (suffix.empty() || suffix.back() == '.')
        return false;
      while (std::getline(ids, part, '.')) {
        if (part.empty() ||
            part.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQ"
                                   "RSTUVWXYZ0123456789-") != std::string::npos)
          return false;
        bool numeric =
            part.find_first_not_of("0123456789") == std::string::npos;
        if (numeric && part.size() > 1 && part[0] == '0')
          return false;
        pre.push_back(part);
      }
    }
    return true;
  }
  bool operator<(const ReleaseVersion &o) const {
    if (core != o.core)
      return core < o.core;
    if (pre.empty() || o.pre.empty())
      return !pre.empty() && o.pre.empty();
    for (size_t i = 0; i < std::min(pre.size(), o.pre.size()); ++i)
      if (pre[i] != o.pre[i]) {
        bool a = pre[i].find_first_not_of("0123456789") == std::string::npos;
        bool b = o.pre[i].find_first_not_of("0123456789") == std::string::npos;
        if (a != b)
          return a;
        if (a && pre[i].size() != o.pre[i].size())
          return pre[i].size() < o.pre[i].size();
        return pre[i] < o.pre[i];
      }
    return pre.size() < o.pre.size();
  }
};
struct ReleaseNotice {
  std::string tag, name, body, url;
};
inline std::string releaseText(std::string value, size_t max) {
  std::string out;
  for (unsigned char c : value) {
    if (c == '\r')
      continue;
    if (c == '\t')
      c = ' ';
    if (c == '\n' || c >= 32)
      out += char(c);
    if (out.size() >= max)
      break;
  }
  return out;
}
inline ReleaseNotice newerRelease(const std::string &payload,
                                  const std::string &installed,
                                  bool *retrieved = nullptr) {
  if (retrieved)
    *retrieved = false;
  ReleaseNotice result;
  ReleaseVersion current, best;
  if (payload.size() > 262144 || !current.parse(installed))
    return result;
  best = current;
  try {
    auto releases = json5pp::parse(payload);
    auto &entries = releases.as_array();
    if (retrieved)
      *retrieved = true;
    for (auto &entry : entries) {
      try {
        auto &o = entry.as_object();
        if (o.at("draft").as_boolean())
          continue;
        bool prerelease = o.at("prerelease").as_boolean();
        if (current.pre.empty() && prerelease)
          continue;
        auto tag = o.at("tag_name").as_string();
        ReleaseVersion candidate;
        if (tag.size() > 128 || !candidate.parse(tag) || !(best < candidate) ||
            (current.pre.empty() && !candidate.pre.empty()))
          continue;
        auto url = o.at("html_url").as_string();
        const std::string prefix =
            "https://github.com/Asukate/mkxp-z-vita/releases/tag/";
        if (url.compare(0, prefix.size(), prefix) != 0 || url.size() > 512 ||
            url.find_first_of("\r\n\t ") != std::string::npos)
          continue;
        ReleaseNotice found;
        found.tag = tag;
        found.url = url;
        auto name = o.find("name");
        found.name = name != o.end() && name->second.is_string()
                         ? releaseText(name->second.as_string(), 200)
                         : tag;
        auto body = o.find("body");
        found.body = body != o.end() && body->second.is_string()
                         ? releaseText(body->second.as_string(), 16000)
                         : "";
        result = std::move(found);
        best = candidate;
      } catch (const std::exception &) {
      }
    }
  } catch (const std::exception &) {
  }
  return result;
}
// The worker owns its state independently of launcher resources. Cancellation
// on LoadExec never waits for DNS, TLS or a remote server on the UI thread.
class UpdateCheck {
  struct State;
  std::shared_ptr<State> state;

public:
  ~UpdateCheck();
  void cancel();
  void start();
  bool poll(ReleaseNotice &, bool *retrieved = nullptr);
};
} // namespace hardrpg
