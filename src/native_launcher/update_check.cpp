#include "update_check.h"
#include "net_session.h"
#include "release.h"
#include <cstdlib>
#include <curl/curl.h>
#ifdef __vita__
#include <psp2/kernel/threadmgr.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <psp2/sysmodule.h>
#else
#include <thread>
#endif

namespace hardrpg {
struct UpdateCheck::State {
  std::atomic<bool> cancel{false}, done{false};
  bool delivered = false;
  bool retrieved = false;
  ReleaseNotice notice;
  std::string payload;
  static size_t write(char *data, size_t size, size_t count, void *ptr) {
    auto &s = *static_cast<State *>(ptr);
    if (size && count > 262144 / size)
      return 0;
    size_t n = size * count;
    if (s.cancel || n > 262144 - s.payload.size())
      return 0;
    try {
      s.payload.append(data, n);
    } catch (...) {
      return 0;
    }
    return n;
  }
  static int progress(void *ptr, curl_off_t, curl_off_t, curl_off_t,
                      curl_off_t) {
    return static_cast<State *>(ptr)->cancel ? 1 : 0;
  }
  void fetch() {
    NetSession session;
    bool online = session.online();
    if (online && !cancel) {
      CURL *curl = curl_easy_init();
      if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL,
                         "https://api.github.com/repos/Asukate/mkxp-z-vita/"
                         "releases?per_page=10");
        curl_easy_setopt(curl, CURLOPT_USERAGENT,
                         "HardRPG/" HARDRPG_VERSION_TAG);
        curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 3000L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 6000L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#ifdef __vita__
        curl_easy_setopt(curl, CURLOPT_CAINFO, "app0:/ca-certificates.crt");
#endif
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, this);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, this);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        long status = 0;
        auto rc = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        if (rc == CURLE_OK && status == 200 && !cancel)
          notice = newerRelease(payload, HARDRPG_VERSION_TAG, &retrieved);
        curl_easy_cleanup(curl);
      }
    }
    payload.clear();
    done.store(true, std::memory_order_release);
  }
#ifdef __vita__
  static int worker(SceSize, void *argument) {
    auto holder = *static_cast<std::shared_ptr<State> **>(argument);
    auto state = *holder;
    delete holder;
    try {
      state->fetch();
    } catch (...) {
      state->done.store(true, std::memory_order_release);
    }
    state.reset();
    sceKernelExitDeleteThread(0);
    return 0;
  }
#endif
};
UpdateCheck::~UpdateCheck() { cancel(); }
void UpdateCheck::cancel() {
  if (state)
    state->cancel = true;
}
void UpdateCheck::start() {
  if (state && !state->done.load(std::memory_order_acquire))
    return;
  state = std::make_shared<State>();
#ifdef __vita__
  auto holder = new std::shared_ptr<State>(state);
  auto thread = sceKernelCreateThread("hardrpg_release_check", State::worker,
                                      0x10000100, 128 * 1024, 0, 0, nullptr);
  if (thread < 0 || sceKernelStartThread(thread, sizeof(holder), &holder) < 0) {
    if (thread >= 0)
      sceKernelDeleteThread(thread);
    delete holder;
    state->done = true;
  }
#else
  auto owned = state;
  std::thread([owned] {
    try {
      owned->fetch();
    } catch (...) {
      owned->done = true;
    }
  }).detach();
#endif
}
bool UpdateCheck::poll(ReleaseNotice &notice, bool *retrieved) {
  if (!state || !state->done.load(std::memory_order_acquire) ||
      state->delivered)
    return false;
  state->delivered = true;
  notice = state->notice;
  if (retrieved)
    *retrieved = state->retrieved;
  return true;
}
} // namespace hardrpg
