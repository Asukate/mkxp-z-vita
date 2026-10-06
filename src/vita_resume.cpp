#include "vita_resume.h"
#include <atomic>
#include <sys/time.h>
#include <cstdio>
#ifdef __vita__
#include <psp2/appmgr.h>
extern "C" void SDL_VitaRequestAudioResume(void);
#endif

static std::atomic<unsigned> epoch{0};
unsigned vitaResumeEpoch() { return epoch.load(std::memory_order_acquire); }
void vitaResumeNotify() { epoch.fetch_add(1, std::memory_order_release); }

bool vitaPollResume() {
    static VitaResumeGap clock;
    timeval now{};
    bool resumed = gettimeofday(&now, nullptr) == 0 &&
        clock.sample(uint64_t(now.tv_sec) * 1000 + now.tv_usec / 1000);
#ifdef __vita__
    SceAppMgrSystemEvent event{};
    if (sceAppMgrReceiveSystemEvent(&event) >= 0 &&
        event.systemEvent == SCE_APPMGR_SYSTEMEVENT_ON_RESUME)
        resumed = true;
#endif
    if (resumed) {
        vitaResumeNotify();
#ifdef __vita__
        SDL_VitaRequestAudioResume();
        fprintf(stderr, "Standby recovery generation %u\n", vitaResumeEpoch());
#endif
    }
    return resumed;
}
