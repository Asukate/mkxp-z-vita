#ifndef HARDRPG_VITA_RESUME_H
#define HARDRPG_VITA_RESUME_H
#include <cstdint>

class VitaResumeGap {
    uint64_t previous = 0;
public:
    bool sample(uint64_t milliseconds) {
        bool gap = previous && milliseconds > previous && milliseconds - previous > 2000;
        previous = milliseconds;
        return gap;
    }
};

unsigned vitaResumeEpoch();
void vitaResumeNotify();
bool vitaPollResume();
#endif
