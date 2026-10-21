// SPDX-License-Identifier: GPL-3.0-or-later
// HardRPG Vita entry point, added 2026-10-08. Replaces EasyRPG's app/library
// boot.
#include "native_launcher/easyrpg_handoff.h"
#include "output.h"
#include "player.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <psp2/appmgr.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/processmgr.h>
#include <sys/stat.h>

int _newlib_heap_size_user = 330 * 1024 * 1024;
namespace {
constexpr const char *request = "ux0:/data/hardrpg/selection.json";
void report(const std::string &message) {
  if (FILE *f = std::fopen("ux0:/data/hardrpg/last-error.txt", "wb")) {
    std::fprintf(f, "EasyRPG Player\n%s\n", message.c_str());
    std::fclose(f);
  }
}
// Leaving a game restarts the launcher. The patched Player::Exit() calls this
// before the display and audio threads are torn down, and main() calls it
// again once the player loop ends. Relying on the atexit handler alone left
// HardRPG closed after a game quit, with no report from this function (0.3.0).
// The handler stays for exit() calls such as Output::Error.
bool returning = false;
void returnToLibrary(const char *from) {
  if (returning)
    return;
  returning = true;
  sceClibPrintf("HardRPG EasyRPG: returning to the launcher from %s\n", from);
  std::remove(request);
  char back[] = "--hardrpg-return";
  char *args[] = {back, nullptr};
  int rc = sceAppMgrLoadExec("app0:/eboot.bin", args, nullptr);
  // Vita3K returns 0 and swaps the process later; only an error is a failure.
  if (rc < 0)
    report("Could not return to HardRPG (LoadExec " + std::to_string(rc) + ").");
  sceKernelExitProcess(Player::exit_code);
}
void log(LogLevel level, const std::string &message, LogCallbackUserData) {
  auto label = Output::LogLevelToString(level);
  sceClibPrintf("HardRPG EasyRPG %s: %s\n", label.c_str(), message.c_str());
  if (level == LogLevel::Error)
    report(message);
}
} // namespace
// Called by the patched Player::Exit() once saves and the config are written.
void HardRpgReturnToLauncher() { returnToLibrary("Player::Exit"); }
int main(int argc, char **argv) {
  sceClibPrintf("HardRPG EasyRPG backend\n");
  std::atexit([] { returnToLibrary("atexit"); });
  try {
    bool play = false;
    for (int i = 0; i < argc; ++i)
      if (argv[i] && !std::strcmp(argv[i], "--hardrpg-play"))
        play = true;
    if (!play)
      throw std::runtime_error("EasyRPG requires a HardRPG game selection");
    auto args = hardrpg::easyRpgArguments(hardrpg::readEasyRpgRequest(request));
    std::remove(request); // A request is consumed once, before initializing the
                          // runtime.
    struct stat st{};
    if (stat(args[2].c_str(), &st) || !S_ISDIR(st.st_mode) ||
        stat(args[4].c_str(), &st) || !S_ISDIR(st.st_mode))
      throw std::runtime_error("EasyRPG game or save folder is unavailable");
    // Set exact per-engine roots without splitting mount prefixes or names.
    unsetenv("HARDRPG_RTP_2000");
    unsetenv("HARDRPG_RTP_2003");
    for (size_t i = 5; i < args.size();) {
      const char *name = args[i] == "--hardrpg-rtp-2000" ? "HARDRPG_RTP_2000"
                       : args[i] == "--hardrpg-rtp-2003" ? "HARDRPG_RTP_2003"
                                                        : nullptr;
      if (!name || i + 1 >= args.size() || setenv(name, args[i + 1].c_str(), 1))
        throw std::runtime_error("Could not configure EasyRPG RTP path");
      args.erase(args.begin() + i, args.begin() + i + 2);
    }
    Output::SetLogCallback(log);
    Player::Init(std::move(args));
    Player::Run();
  } catch (const std::exception &error) {
    Player::exit_code = 1;
    report(error.what());
  }
  returnToLibrary("main");
  return Player::exit_code;
}
