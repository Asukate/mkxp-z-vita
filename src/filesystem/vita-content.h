#ifndef MKXP_VITA_CONTENT_H
#define MKXP_VITA_CONTENT_H

#include "../util/iniconfig.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <string>
#include <vector>

namespace VitaContent {
inline std::string packName(const std::string &name) {
    if (name == "." || name == ".." || name.find_first_of("/\\:") != std::string::npos ||
        name.find('\0') != std::string::npos)
        throw std::invalid_argument("Game.ini RTP must name a pack, not a path");
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (lower == "standard" || lower == "xp") return "Standard";
    if (lower == "rpgvx" || lower == "vx") return "RPGVX";
    if (lower == "rpgvxace" || lower == "vxace") return "RPGVXAce";
    return name;
}

template<class Exists>
std::vector<std::string> declaredRtpPaths(const INIConfiguration &ini,
                                        std::string root, Exists exists) {
    if (!root.empty() && root.back() != '/') root += '/';
    std::vector<std::string> result;
    // VX/Ace use RTP; XP supports three separately declared packs.
    for (const char *key : {"RTP", "RTP1", "RTP2", "RTP3"}) {
        std::string name = packName(ini.getStringProperty("Game", key));
        if (name.empty()) continue; // Empty declaration means self-contained.
        std::string path = root + name;
        std::string legacy = name == "Standard" ? "XP" : name == "RPGVX" ? "VX" : name == "RPGVXAce" ? "VXACE" : "";
        if (!exists(path)) {
            if (exists(path + ".zip")) path += ".zip";
            else if (!legacy.empty() && exists(root + legacy)) path = root + legacy;
            else if (!legacy.empty() && exists(root + legacy + ".zip")) path = root + legacy + ".zip";
        }
        if (std::find(result.begin(), result.end(), path) == result.end())
            result.push_back(path);
    }
    return result;
}

inline std::vector<std::string> mountOrder(
    const std::string &gameRoot, const std::string &archive,
    const std::vector<std::string> &patches, const std::vector<std::string> &rtps,
    const std::string &assetsArchive = "") {
    std::vector<std::string> paths = patches;
    paths.push_back(gameRoot);
    if (!assetsArchive.empty()) paths.push_back(assetsArchive);
    if (!archive.empty()) paths.push_back(archive);
    paths.insert(paths.end(), rtps.begin(), rtps.end());
    return paths;
}
} // namespace VitaContent
#endif
