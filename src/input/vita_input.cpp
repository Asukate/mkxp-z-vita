#include "vita_input.h"

#include "eventthread.h"
#include "vita_diagnostic.h"

#include <SDL_gamecontroller.h>
#include <psp2/ctrl.h>
#include <stddef.h>
#include <psp2/touch.h>
#include <psp2/kernel/processmgr.h>
#include <SDL_scancode.h>
#include <fstream>
#include <cstdio>
#include "vita_resume.h"
#include "vita_pad_replay.h"
#include <algorithm>
#include <vector>


struct VitaButtonMap {
	unsigned int sce;
	int sdl;
};

/* Standard Vita layout: cross = A (confirm), circle = B (cancel). */
static const VitaButtonMap buttonMap[] = {
	{ SCE_CTRL_CROSS, SDL_CONTROLLER_BUTTON_A },
	{ SCE_CTRL_CIRCLE, SDL_CONTROLLER_BUTTON_B },
	{ SCE_CTRL_SQUARE, SDL_CONTROLLER_BUTTON_X },
	{ SCE_CTRL_TRIANGLE, SDL_CONTROLLER_BUTTON_Y },
	{ SCE_CTRL_SELECT, SDL_CONTROLLER_BUTTON_BACK },
	{ SCE_CTRL_START, SDL_CONTROLLER_BUTTON_START },
	{ SCE_CTRL_UP, SDL_CONTROLLER_BUTTON_DPAD_UP },
	{ SCE_CTRL_DOWN, SDL_CONTROLLER_BUTTON_DPAD_DOWN },
	{ SCE_CTRL_LEFT, SDL_CONTROLLER_BUTTON_DPAD_LEFT },
	{ SCE_CTRL_RIGHT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT },
	{ SCE_CTRL_LTRIGGER, SDL_CONTROLLER_BUTTON_LEFTSHOULDER },
	{ SCE_CTRL_RTRIGGER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER },
};

/* Stick bytes run 0..255 around a 128 center; SDL axes are int16 with
 * negative = left/up. Values match on sign, so a linear map suffices. */
static int scaleStick(unsigned char v) {
	int d = (int)v - 128;
	if (d > -6 && d < 6)
		return 0;
	return d * 256;
}

namespace {
/* The pad as the game sees it on this Input.update, made once per update by
 * vitaPadBeginFrame and read by both the default bindings and the profiles. */
struct PadFrame {
	uint16_t buttons = 0;
	unsigned char lx = 128, ly = 128, rx = 128, ry = 128;
	bool valid = false;
};
PadFrame padFrame;
hardrpg::PadReplay padReplay;
bool padHistory = true, padTest = false, padPrimed = false;
uint64_t padLastStamp = 0, padLastFrameUs = 0;
unsigned padEpoch = 0;
/* Presses older than this when the game reads the pad again are not
 * replayed: after a load, a menu or a resume the game gets the current state. */
const uint64_t padMaxGapUs = 500000;
unsigned long padUpdates = 0, padReplayed = 0;
int padLogs = 0;
/* Test packages only (vitaPadTest): scripted presses replace the pad. */
struct ScriptPress {
	uint64_t down, up;
	uint16_t mask;
};
std::vector<ScriptPress> padScript;
uint64_t padSynthUs = 0;

uint16_t padBits(uint32_t sce) {
	uint16_t bits = 0;
	for (size_t i = 0; i < sizeof(buttonMap) / sizeof(buttonMap[0]); ++i)
		if (sce & buttonMap[i].sce)
			bits |= uint16_t(1u << i);
	return bits;
}
uint16_t scriptMask(uint64_t us) {
	uint16_t mask = 0;
	for (auto &p : padScript)
		if (us >= p.down && us < p.up)
			mask |= p.mask;
	return mask;
}
void padRestart(uint16_t bits, uint64_t stamp) {
	padReplay.reset(bits);
	padLastStamp = stamp;
}
}

void vitaConfigurePad(bool history, bool test) {
	padHistory = history;
	padTest = test;
	padPrimed = false;
}

void vitaPadReset() { padPrimed = false; }

void vitaPadBeginFrame() {
	static bool samplingSet = false;
	if (!samplingSet) {
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
		samplingSet = true;
	}
	const uint64_t now = sceKernelGetProcessTimeWide();
	const unsigned epoch = vitaResumeEpoch();
	const bool gap = padLastFrameUs && now - padLastFrameUs > padMaxGapUs;
	/* Only the samples taken since the last update can be new (one per
	 * 16.7 ms); a normal frame asks for three instead of the whole buffer. */
	const uint64_t elapsed = padLastFrameUs ? now - padLastFrameUs : padMaxGapUs;
	const int wanted = int(std::min<uint64_t>(64, elapsed / 16667 + 3));
	if (epoch != padEpoch || gap)
		padPrimed = false;
	padEpoch = epoch;
	padLastFrameUs = now;
	++padUpdates;

	if (padTest) {
		/* The scripted pad is sampled on the same ~60 Hz grid as the real one. */
		if (!padPrimed) {
			padSynthUs = now;
			padReplay.reset(scriptMask(now));
			padPrimed = true;
		}
		if (padHistory) {
			for (; padSynthUs + 16667 <= now; padSynthUs += 16667)
				padReplay.feed(padSynthUs + 16667, scriptMask(padSynthUs + 16667));
		} else {
			padSynthUs = now;
			padReplay.reset(scriptMask(now));
		}
		padFrame.buttons = padReplay.deliver();
		padFrame.lx = padFrame.ly = padFrame.rx = padFrame.ry = 128;
		padFrame.valid = true;
		return;
	}

	SceCtrlData pad[64];
	const int n = sceCtrlPeekBufferPositive(0, pad, padHistory && padPrimed ? wanted : padHistory ? 64 : 1);
	if (n <= 0) {
		padFrame.valid = false;
		return;
	}
	std::sort(pad, pad + n, [](const SceCtrlData &a, const SceCtrlData &b) {
		return a.timeStamp < b.timeStamp;
	});
	const SceCtrlData &latest = pad[n - 1];
	if (!padHistory || !padPrimed) {
		/* No replay of what happened before the game started reading. */
		padRestart(padBits(latest.buttons), latest.timeStamp);
		padPrimed = true;
		if (padHistory && padLogs == 0 && n > 1) {
			++padLogs;
			std::fprintf(stderr, "Pad history: %d samples, %.1f ms apart\n", n,
			             double(latest.timeStamp - pad[0].timeStamp) / 1000.0 / (n - 1));
		}
	} else {
		int fresh = 0;
		for (int i = 0; i < n; ++i) {
			if (pad[i].timeStamp <= padLastStamp)
				continue;
			padReplay.feed(pad[i].timeStamp, padBits(pad[i].buttons));
			++fresh;
		}
		padLastStamp = latest.timeStamp;
		if (fresh > 1) {
			++padReplayed;
			if (padLogs < 4 && padReplay.pending()) {
				++padLogs;
				std::fprintf(stderr, "Pad replay: %d samples in one frame, %lu changes waiting\n",
				             fresh, (unsigned long)padReplay.pending());
			}
		}
	}
	padFrame.buttons = padReplay.deliver();
	padFrame.lx = latest.lx;
	padFrame.ly = latest.ly;
	padFrame.rx = latest.rx;
	padFrame.ry = latest.ry;
	padFrame.valid = true;
}

void vitaPadScript(const std::vector<std::array<double, 3>> &presses) {
	if (!padTest)
		return;
	const uint64_t now = sceKernelGetProcessTimeWide();
	padScript.clear();
	for (auto &p : presses)
		padScript.push_back({now + uint64_t(p[0] * 1000), now + uint64_t(p[1] * 1000), uint16_t(p[2])});
}

std::string vitaPadStats() {
	char text[200];
	std::snprintf(text, sizeof(text),
	              "history=%d test=%d updates=%lu multi_sample=%lu deferred=%lu dropped=%lu pending=%lu",
	              padHistory, padTest, padUpdates, padReplayed, padReplay.deferredUpdates,
	              padReplay.dropped, (unsigned long)padReplay.pending());
	return text;
}

void vitaPollPads() {
	if (!padFrame.valid)
		return;
	for (size_t i = 0; i < sizeof(buttonMap) / sizeof(buttonMap[0]); ++i) {
		bool pressed = (padFrame.buttons & (1u << i)) != 0;

		bool &slot = EventThread::controllerState.buttons[buttonMap[i].sdl];
		if (pressed == slot)
			continue;
		slot = pressed;
		vitaDiagControllerButton(buttonMap[i].sdl, pressed);
	}

	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_LEFTX] = scaleStick(padFrame.lx);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_LEFTY] = scaleStick(padFrame.ly);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_RIGHTX] = scaleStick(padFrame.rx);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_RIGHTY] = scaleStick(padFrame.ry);
}

namespace {
hardrpg::ControlProfile profile;
hardrpg::ControlState controls;
unsigned controlsEpoch=0;
}
const hardrpg::ControlProfile &vitaControlProfile() {return profile;}
void vitaResetControls(unsigned reason) {controls.reset(reason);controlsEpoch=vitaResumeEpoch();}
void vitaLoadControls(const std::string &configPath) {
  profile={}; vitaResetControls();
  const std::string prefix="ux0:/data/hardrpg/config/";
  if(configPath.compare(0,prefix.size(),prefix)!=0)return;
  auto name=configPath.substr(prefix.size());
  if(name.size()!=21 || name.substr(16)!=".json" ||
     name.substr(0,16).find_first_not_of("0123456789abcdef")!=std::string::npos)return;
  std::ifstream in("ux0:/data/hardrpg/controls/"+name,std::ios::binary);
  if(!in)return;
  char buffer[8193];in.read(buffer,sizeof(buffer));
  if(in.gcount()>8192)return;
  try {profile=hardrpg::ControlProfile::parse(json5pp::parse(std::string(buffer,size_t(in.gcount()))));}
  catch(const std::exception &e) {std::fprintf(stderr,"Controls disabled: %s\n",e.what());}
  // Tap-to-walk reads taps as the pointer, so the front screen is the mouse,
  // unless the screen was given touch buttons or zones, which come first.
  if(profile.tapToWalk && profile.enabled && profile.front==0)profile.front=2;
}
hardrpg::ControlSample vitaReadControls(bool front,bool rear,bool fromFrame) {
  hardrpg::ControlSample sample;sample.milliseconds=sceKernelGetProcessTimeWide()/1000;
  if(fromFrame) {
    if(padFrame.valid) {
      sample.buttons=padFrame.buttons;
      sample.lx=padFrame.lx;sample.ly=padFrame.ly;sample.rx=padFrame.rx;sample.ry=padFrame.ry;
    }
  } else {
    /* Menus and the launcher preview read the live pad. */
    static bool analog=false; if(!analog) {sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);analog=true;}
    SceCtrlData pad{};
    if(sceCtrlPeekBufferPositive(0,&pad,1)>0) {
      sample.buttons=padBits(pad.buttons);
      sample.lx=pad.lx;sample.ly=pad.ly;sample.rx=pad.rx;sample.ry=pad.ry;
    }
  }
  static bool ready[2]={false,false};static SceTouchPanelInfo info[2]{};
  for(int port=0;port<2;++port) {
    if(!(port==0?front:rear))continue;
    if(!ready[port]) {
      if(sceTouchGetPanelInfo(port,&info[port])<0)continue;
      sceTouchSetSamplingState(port,SCE_TOUCH_SAMPLING_STATE_START);ready[port]=true;
    }
    SceTouchData data{};if(sceTouchPeek(port,&data,1)<=0)continue;
    auto &panel=info[port];int x0=port==0?panel.minDispX:panel.minAaX;
    int y0=port==0?panel.minDispY:panel.minAaY;
    int w=(port==0?panel.maxDispX:panel.maxAaX)-x0;
    int h=(port==0?panel.maxDispY:panel.maxAaY)-y0;
    if(w<=0 || h<=0)continue;
    auto &contacts=port==0?sample.front:sample.rear;
    for(unsigned i=0;i<data.reportNum && i<SCE_TOUCH_MAX_REPORT;++i) {
      auto &c=data.report[i];contacts.push_back({c.id,float(c.x-x0)/w,float(c.y-y0)/h});
    }
  }
  return sample;
}
hardrpg::ControlResult vitaProcessControls(hardrpg::ControlSample sample) {
  unsigned epoch=vitaResumeEpoch();if(epoch!=controlsEpoch)vitaResetControls(2);
  return controls.update(profile,sample);
}
namespace {
std::array<bool, 256> mappedKeys{};
}
void vitaSetMappedKeys(const std::array<bool, 256> &keys) { mappedKeys = keys; }
namespace {
bool fastForwardHeld = false;
}
void vitaSetFastForwardHeld(bool held) { fastForwardHeld = held; }
bool vitaFastForwardHeld() { return fastForwardHeld; }
bool vitaMappedKey(int scancode) {
  return scancode > 0 && scancode < 256 && mappedKeys[scancode];
}
bool vitaControlScancode(int code) {
  switch(code) {
    case SDL_SCANCODE_RETURN:case SDL_SCANCODE_ESCAPE:case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_A:case SDL_SCANCODE_S:case SDL_SCANCODE_D:
    case SDL_SCANCODE_UP:case SDL_SCANCODE_DOWN:case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_RIGHT:case SDL_SCANCODE_Q:case SDL_SCANCODE_W:return true;
    default:return false;
  }
}
