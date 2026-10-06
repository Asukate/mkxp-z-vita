// HardRPG game browser and restart handoff.
// A selection file identifies the next game boot. The RGSS backend launches
// its packaged picker when there is no selection; the native backend handles
// menu boots before configuration loading and probes this hook without
// consuming the selection. A game boot consumes it exactly once.
#ifdef MKXPZ_PROTO_LAUNCHER

#include "vita_proto_launcher.h"

#include <cstdio>
#include <algorithm>
#include <string>
#include "util/json5pp.hpp"

namespace {

const char *kPickPath = "ux0:/data/hardrpg/selection.json";
const char *kGamesRoot = "ux0:/data/hardrpg/games";
const char *kCacheRoot = "ux0:/data/hardrpg/cache";
const char *kConfigRoot = "ux0:/data/hardrpg/config";
const char *kStub = "app0:/stub";

bool safeRelative(const std::string &path) {
    if (path.empty() || path.find_first_of("\\:\0", 0, 3) != std::string::npos ||
        std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32; }))
        return false;
    size_t start = 0;
    do {
        size_t end = path.find('/', start);
        std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) return true;
        start = end + 1;
    } while (start < path.size());
    return false;
}

bool safeExternal(const std::string &path) {
    for (const char *root : {"ux0:/", "uma0:/", "imc0:/"}) {
        if (path.compare(0, std::string(root).size(), root) == 0 &&
            safeRelative(path.substr(std::string(root).size())))
            return path.find_first_of("\r\n\t") == std::string::npos;
    }
    return false;
}

} // namespace

bool protoLauncherPick(std::string &gameFolder, int &rgssVersion,
                       std::vector<std::string> &rtps, std::string &vitaConfigPath,
                       std::map<std::string, std::string> &rtpRoots, bool consume) {
    rtps.clear();
    rtpRoots.clear();
    vitaConfigPath.clear();

    // The launcher just exited with a choice: validate and consume it.
    FILE *pick = std::fopen(kPickPath, "rb");
    if (pick) {
        char buffer[4097] = {0};
        size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, pick);
        bool complete = std::fgetc(pick) == EOF;
        std::fclose(pick);
        if (consume) std::remove(kPickPath);
        try {
            auto selection = json5pp::parse(std::string(buffer, n));
            const auto &object = selection.as_object();
            std::string source = object.at("source").as_string();
            std::string path = object.at("path").as_string();
            std::string key = object.at("config").as_string();
            int version = object.at("rgss").as_integer();
            bool validKey = key.size() == 16 && key.find_first_not_of("0123456789abcdef") == std::string::npos;
            bool validCache = source != "cache" || (path == "zip-" + key);
            bool validPath = source == "external" ? safeExternal(path) : safeRelative(path);
            std::vector<std::string> selectedRtps;
            std::map<std::string, std::string> selectedRoots;
            auto rtp = object.find("rtp");
            if (rtp != object.end()) {
                const auto &mounts = rtp->second.as_array();
                if (mounts.size() > 4) throw std::invalid_argument("Too many RTP packs");
                for (const auto &mount : mounts) {
                    std::string packPath = mount.as_object().at("path").as_string();
                    std::string root = mount.as_object().at("root").as_string();
                    if (!safeExternal(packPath) || (!root.empty() &&
                        (!safeRelative(root) || root.find_first_of("\r\n\t") != std::string::npos)))
                        throw std::invalid_argument("Invalid RTP path");
                    selectedRtps.push_back(packPath);
                    if (!root.empty()) selectedRoots[packPath] = root;
                }
            }
            if (complete && n && validPath && validKey && validCache &&
                (source == "games" || source == "cache" || source == "external") && version >= 1 && version <= 3) {
                gameFolder = source == "external" ? path :
                    std::string(source == "cache" ? kCacheRoot : kGamesRoot) + "/" + path;
                vitaConfigPath = std::string(kConfigRoot) + "/" + key + ".json";
                rgssVersion = version;
                rtps = std::move(selectedRtps);
                rtpRoots = std::move(selectedRoots);
                return true;
            }
        } catch (const std::exception &) {
            // Malformed/stale selections return to the browser.
        }
    }

    // No pick: boot the stub picker project.
    gameFolder = kStub;
    return true;
}

#endif // MKXPZ_PROTO_LAUNCHER
