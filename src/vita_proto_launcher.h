#pragma once
// HardRPG browser handoff, compiled with -Dproto_launcher=true.
#ifdef MKXPZ_PROTO_LAUNCHER

#include <string>
#include <vector>
#include <map>

// Decides which project boots: the stub picker (app0:/stub) when no pick
// file exists, else a nested game under games/ or a prepared ZIP under cache/.
// Always returns true. consume=false validates without removing the handoff.
bool protoLauncherPick(std::string &gameFolder, int &rgssVersion,
                       std::vector<std::string> &rtps, std::string &vitaConfigPath,
                       std::map<std::string, std::string> &rtpRoots, bool consume = true);

#endif // MKXPZ_PROTO_LAUNCHER
