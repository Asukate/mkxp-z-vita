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

void vitaPollPads() {
	static bool samplingSet = false;
	if (!samplingSet) {
		sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
		samplingSet = true;
	}

	SceCtrlData pad;
	int result = sceCtrlPeekBufferPositive(0, &pad, 1);
	if (result < 0)
		return;

	for (size_t i = 0; i < sizeof(buttonMap) / sizeof(buttonMap[0]); ++i) {
		bool pressed = (pad.buttons & buttonMap[i].sce) != 0;

		bool &slot = EventThread::controllerState.buttons[buttonMap[i].sdl];
		if (pressed == slot)
			continue;
		slot = pressed;
		vitaDiagControllerButton(buttonMap[i].sdl, pressed);
	}

	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_LEFTX] = scaleStick(pad.lx);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_LEFTY] = scaleStick(pad.ly);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_RIGHTX] = scaleStick(pad.rx);
	EventThread::controllerState.axes[SDL_CONTROLLER_AXIS_RIGHTY] = scaleStick(pad.ry);
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
hardrpg::ControlSample vitaReadControls(bool front,bool rear) {
  hardrpg::ControlSample sample;sample.milliseconds=sceKernelGetProcessTimeWide()/1000;
  static bool analog=false; if(!analog) {sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);analog=true;}
  SceCtrlData pad{};
  int result = sceCtrlPeekBufferPositive(0,&pad,1);
  if(result>0) {
    for(size_t i=0;i<sizeof(buttonMap)/sizeof(*buttonMap);++i)
      if(pad.buttons&buttonMap[i].sce)sample.buttons|=1u<<i;
    sample.lx=pad.lx;sample.ly=pad.ly;sample.rx=pad.rx;sample.ry=pad.ry;
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
