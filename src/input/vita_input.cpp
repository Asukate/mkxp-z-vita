#include "vita_input.h"

#include "eventthread.h"
#include "vita_diagnostic.h"

#include <SDL_gamecontroller.h>
#include <psp2/ctrl.h>
#include <stddef.h>


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
	if (sceCtrlPeekBufferPositive(0, &pad, 1) < 0)
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
