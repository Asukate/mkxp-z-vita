#ifndef VITA_INPUT_H
#define VITA_INPUT_H
#include "vita_controls.h"
#include <string>

/* Direct SceCtrl fallback used only when SDL has no opened controller. */
void vitaPollPads();
hardrpg::ControlSample vitaReadControls(bool front = true, bool rear = true);
void vitaLoadControls(const std::string &configPath);
const hardrpg::ControlProfile &vitaControlProfile();
hardrpg::ControlResult vitaProcessControls(hardrpg::ControlSample sample);
void vitaResetControls(unsigned reason = 1);
bool vitaControlScancode(int scancode);
/* Keyboard keys held this frame through a key mapping in the control profile. */
void vitaSetMappedKeys(const std::array<bool, 256> &keys);
bool vitaMappedKey(int scancode);
// An input mapped to Fast forward is held (read by Graphics each frame).
void vitaSetFastForwardHeld(bool held);
bool vitaFastForwardHeld();

#endif
