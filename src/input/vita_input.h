#ifndef VITA_INPUT_H
#define VITA_INPUT_H
#include "vita_controls.h"
#include <array>
#include <string>
#include <vector>

/* Reads the pad once per Input.update: every sample since the last update
 * goes through the replay, so presses between two slow frames are kept. */
void vitaPadBeginFrame();
/* history: replay the samples between updates (default on).
 * test: the pad comes from vitaPadScript (benchmark packages only). */
void vitaConfigurePad(bool history, bool test);
/* The next update starts from the current pad state (menus, resume). */
void vitaPadReset();
/* Scripted presses [down_ms, up_ms, button bits] from now; test mode only. */
void vitaPadScript(const std::vector<std::array<double, 3>> &presses);
std::string vitaPadStats();
/* Applies this update's pad to the default controller bindings. */
void vitaPollPads();
/* fromFrame: this update's replayed pad (Input.update); otherwise the live pad. */
hardrpg::ControlSample vitaReadControls(bool front = true, bool rear = true, bool fromFrame = false);
void vitaLoadControls(const std::string &configPath);
const hardrpg::ControlProfile &vitaControlProfile();
hardrpg::ControlResult vitaProcessControls(hardrpg::ControlSample sample);
void vitaResetControls(unsigned reason = 1);
bool vitaControlScancode(int scancode);
/* Keyboard keys held this frame through a key mapping in the control profile. */
void vitaSetMappedKeys(const std::array<bool, 256> &keys);
bool vitaMappedKey(int scancode);
// In-game overlay keys share the mapped key path, while its pad and touch
// navigation are withheld from gameplay until the closing inputs are up.
void vitaSetKeyboardOverlay(const std::array<bool, 256> &keys, bool captures);
bool vitaKeyboardCapturesInput();
// The game asked for typed text (Input.text_input = true).
void vitaSetTextInput(bool on);
bool vitaTextInput();
void vitaApplyKeyboardOverlay(uint8_t *rawKeys);
// An input mapped to Fast forward is held (read by Graphics each frame).
void vitaSetFastForwardHeld(bool held);
// When the game itself last called Input.update (SDL ticks), and whether
// the current update is one the engine started for a keyboard-only game.
extern unsigned int vitaGameInputTicks;
extern bool vitaInputAutoUpdate;
bool vitaFastForwardHeld();

#endif
