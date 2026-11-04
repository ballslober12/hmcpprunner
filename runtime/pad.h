// Gamepad layer: emulates the XInput DLL (GMXInput.dll) that Hotline Miami's "360 gamepad" mode talks to.
// Backends: SDL game controllers (PC) or LGNC_GAMEPAD (LG NetCast, HM_NC4), plus a scripted pad for tests.
#pragma once
#include <string>

void pad_init();                 // after the window exists (LGNC must be registered on NetCast)
void pad_update(int frame_no);   // once per frame
bool pad_connected(int idx = 0);
// GMXInput.dll entry points by name (setRumble, leftTrigger, leftThumbX, getButtonState, checkButton, ...)
double pad_ext_call(const std::string& fn, double a0, double a1, double a2);
