// Host checks for the native launch parser, including path boundaries.
#include "src/vita_proto_launcher.h"
#include "src/filesystem/vita-content.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>

static void check(const std::string &json, const std::string &expected, int rgss = 0, bool rtp = false) {
    std::ofstream("ux0:/data/hardrpg/selection.json", std::ios::binary) << json;
    std::string folder, config;
    std::vector<std::string> rtps;
    std::map<std::string, std::string> roots;
    int version = 0;
    protoLauncherPick(folder, version, rtps, config, roots);
    if (folder != expected || (rgss && version != rgss) ||
        std::filesystem::exists("ux0:/data/hardrpg/selection.json"))
        throw std::runtime_error("Invalid native handoff result: " + folder);
    if (rgss && config != "ux0:/data/hardrpg/config/0123456789abcdef.json")
        throw std::runtime_error("Invalid config path");
    if (rtp && rtps != std::vector<std::string>{"ux0:/data/mkxp-z-vita/rtp/VXACE.zip"})
        throw std::runtime_error("RTP selection did not reach runtime");
    if (rtp && roots[rtps.front()] != "RPGVXAce")
        throw std::runtime_error("RTP wrapper root did not reach runtime");
}

int main(int argc, char **argv) {
    if (argc != 2) return 2;
    std::filesystem::current_path(argv[1]);
    std::filesystem::create_directories("ux0:/data/hardrpg");
    const std::string key = ",\"config\":\"0123456789abcdef\",\"rgss\":3}";
    check("{\"source\":\"games\",\"path\":\"Collection/Game\"" + key,
          "ux0:/data/hardrpg/games/Collection/Game", 3);
    check("{\"source\":\"cache\",\"path\":\"zip-0123456789abcdef\"" + key,
          "ux0:/data/hardrpg/cache/zip-0123456789abcdef", 3);
    check("{\"source\":\"external\",\"path\":\"ux0:/data/mkxp-z-vita/games/Hylics\"" + key,
          "ux0:/data/mkxp-z-vita/games/Hylics", 3);
    check("{\"source\":\"external\",\"path\":\"uma0:/RPG Games/Ace\"" + key,
          "uma0:/RPG Games/Ace", 3);
    check("{\"source\":\"cache\",\"path\":\"zip-0123456789abcdef\",\"rtp\":[{\"path\":\"ux0:/data/mkxp-z-vita/rtp/VXACE.zip\",\"root\":\"RPGVXAce\"}]" + key,
          "ux0:/data/hardrpg/cache/zip-0123456789abcdef", 3, true);
    for (const char *path : {"ur0:/tai", "ux0:/../escape", "ux0:/rtp\\nbad", "/host/rtp"})
        check(std::string("{\"source\":\"games\",\"path\":\"Game\",\"rtp\":[{\"path\":\"") + path + "\",\"root\":\"\"}]" + key, "app0:/stub");
    for (const char *inner : {"../escape", "/absolute", "a//b", "a\\nb"})
        check(std::string("{\"source\":\"games\",\"path\":\"Game\",\"rtp\":[{\"path\":\"ux0:/RTP.zip\",\"root\":\"") + inner + "\"}]" + key, "app0:/stub");
    for (const char *path : {"ux0:/../outside", "ux0:/data//Game", "ur0:/tai", "app0:/stub", "/host/Game", "ux0:/", "ux0:/a\\u0000b", "ux0:/a\\nb"})
        check(std::string("{\"source\":\"external\",\"path\":\"") + path + "\"" + key, "app0:/stub");
    for (const char *path : {"../outside", "/absolute", "a/../b", "a//b", "a/", "ux0:escape", "a\\\\b", "a\\u0000b"})
        check(std::string("{\"source\":\"games\",\"path\":\"") + path + "\"" + key, "app0:/stub");
    check("{\"source\":\"cache\",\"path\":\"other-cache\"" + key, "app0:/stub");
    check("{\"source\":\"other\",\"path\":\"Game\"" + key, "app0:/stub");
    check("not JSON", "app0:/stub");
    check("{}", "app0:/stub");
    check(std::string(5000, 'x'), "app0:/stub");
    INIConfiguration ini;
    std::istringstream gameIni("[Game]\nRTP=RPGVXAce\n");
    if (!ini.load(gameIni)) throw std::runtime_error("Failed to load RTP test INI");
    auto normal = VitaContent::declaredRtpPaths(ini, "ux0:/data/hardrpg/rtp",
        [](const std::string &p) { return p == "ux0:/data/hardrpg/rtp/RPGVXAce"; });
    auto legacy = VitaContent::declaredRtpPaths(ini, "ux0:/data/mkxp-z-vita/rtp",
        [](const std::string &p) { return p == "ux0:/data/mkxp-z-vita/rtp/VXACE.zip"; });
    if (normal != std::vector<std::string>{"ux0:/data/hardrpg/rtp/RPGVXAce"} ||
        legacy != std::vector<std::string>{"ux0:/data/mkxp-z-vita/rtp/VXACE.zip"})
        throw std::runtime_error("Native default/legacy RTP path mismatch");
    std::cout << "PASS: native RTP paths/wrapper handoff, nested/cache launch, consumed handoff, traversal/device/control-character rejection and malformed JSON\n";
}
