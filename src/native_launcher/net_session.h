#pragma once
#include <curl/curl.h>
#include <cstdlib>
#include <mutex>
#ifdef __vita__
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#endif

namespace hardrpg {
// The network for the launcher's downloads (artwork, achievements, the update
// check), each on its own thread and possibly at the same time. The first to
// start brings the network and curl up, the last to finish takes them down,
// so one never unloads the network module while another is still using it.
class NetSession {
public:
  NetSession() {
    std::lock_guard<std::mutex> hold(lock());
    auto &s = state();
    if (s.users++ == 0) {
#ifdef __vita__
      s.module = sceSysmoduleLoadModule(SCE_SYSMODULE_NET) == 0;
      s.pool = std::malloc(1024 * 1024);
      SceNetInitParam param{};
      param.memory = s.pool;
      param.size = 1024 * 1024;
      s.net = s.pool && sceNetInit(&param) == 0;
      s.ctl = sceNetCtlInit() == 0;
#endif
      s.curl = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }
  }
  ~NetSession() {
    std::lock_guard<std::mutex> hold(lock());
    auto &s = state();
    if (--s.users > 0)
      return;
    if (s.curl)
      curl_global_cleanup();
#ifdef __vita__
    if (s.ctl)
      sceNetCtlTerm();
    if (s.net)
      sceNetTerm();
    std::free(s.pool);
    if (s.module && s.net)
      sceSysmoduleUnloadModule(SCE_SYSMODULE_NET);
#endif
    s = State{};
  }
  NetSession(const NetSession &) = delete;
  NetSession &operator=(const NetSession &) = delete;

  // Connected to the internet, with curl ready.
  bool online() const {
    std::lock_guard<std::mutex> hold(lock());
    if (!state().curl)
      return false;
#ifdef __vita__
    int connection = 0;
    return sceNetCtlInetGetState(&connection) == 0 && connection == SCE_NETCTL_STATE_CONNECTED;
#else
    return true;
#endif
  }

private:
  struct State {
    int users = 0;
    bool module = false, net = false, ctl = false, curl = false;
    void *pool = nullptr;
  };
  static std::mutex &lock() {
    static std::mutex m;
    return m;
  }
  static State &state() {
    static State s;
    return s;
  }
};
} // namespace hardrpg
