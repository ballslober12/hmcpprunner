#include "game.h"
#include "pad.h"
#include <mutex>

#ifdef HM_NC4
#include <lgnc.h>
#include <linux/input.h>
#endif

namespace {

// XInput button bits (what the game's getid() script returns)
enum : int {
  XB_UP = 1, XB_DOWN = 2, XB_LEFT = 4, XB_RIGHT = 8, XB_START = 16, XB_BACK = 32, XB_LTHUMB = 64, XB_RTHUMB = 128,
  XB_LB = 256, XB_RB = 512, XB_A = 4096, XB_B = 8192, XB_X = 16384, XB_Y = 32768
};

struct PadState {
  bool connected = false;
  int buttons = 0;
  int lx = 0, ly = 0, rx = 0, ry = 0;  // -32768..32767, Y up (XInput convention)
  int lt = 0, rt = 0;                  // 0..255
  int hatx = 0, haty = 0;              // evdev hat state (NetCast)
};
const int MAXPAD = 4;
PadState pads[MAXPAD];
std::mutex pmtx;

// scripted pad for headless tests: HM_PAD="30-120:lx=32767,btn=4096;200-260:rt=255"
struct Script { int a, b; PadState st; };
std::vector<Script> script;
bool scripted = false;

void parse_script(const char* s) {
  std::string str = s;
  size_t p = 0;
  while (p < str.size()) {
    size_t q = str.find(';', p);
    if (q == std::string::npos) q = str.size();
    std::string item = str.substr(p, q - p);
    p = q + 1;
    size_t c = item.find(':');
    if (c == std::string::npos) continue;
    Script sc;
    sc.a = atoi(item.c_str());
    size_t d = item.find('-');
    sc.b = d != std::string::npos && d < c ? atoi(item.c_str() + d + 1) : sc.a;
    sc.st.connected = true;
    std::string rest = item.substr(c + 1);
    size_t r = 0;
    while (r < rest.size()) {
      size_t e = rest.find(',', r);
      if (e == std::string::npos) e = rest.size();
      std::string kv = rest.substr(r, e - r);
      r = e + 1;
      size_t eq = kv.find('=');
      if (eq == std::string::npos) continue;
      std::string k = kv.substr(0, eq);
      int v = atoi(kv.c_str() + eq + 1);
      if (k == "lx") sc.st.lx = v; else if (k == "ly") sc.st.ly = v; else if (k == "rx") sc.st.rx = v;
      else if (k == "ry") sc.st.ry = v; else if (k == "lt") sc.st.lt = v; else if (k == "rt") sc.st.rt = v;
      else if (k == "btn") sc.st.buttons = v;
    }
    script.push_back(sc);
  }
  scripted = !script.empty();
}

int clamp16(int v) { return v < -32768 ? -32768 : v > 32767 ? 32767 : v; }

#ifndef HM_NC4
SDL_GameController* sdlpad = nullptr;
void sdl_open_first() {
  for (int i = 0; i < SDL_NumJoysticks(); i++)
    if (SDL_IsGameController(i)) {
      sdlpad = SDL_GameControllerOpen(i);
      if (sdlpad) { fprintf(stderr, "gamepad: %s\n", SDL_GameControllerName(sdlpad)); return; }
    }
}
#else
// ---- NetCast: LGNC_GAMEPAD delivers raw evdev events
FILE* padlog = nullptr;
int slot_for(int deviceID) {
  static int ids[MAXPAD] = {-1, -1, -1, -1};
  for (int i = 0; i < MAXPAD; i++) if (ids[i] == deviceID) return i;
  for (int i = 0; i < MAXPAD; i++) if (ids[i] < 0) { ids[i] = deviceID; return i; }
  return -1;
}
void setbit(PadState& s, int bit, bool on) { if (on) s.buttons |= bit; else s.buttons &= ~bit; }
void hat_to_dpad(PadState& s) {
  setbit(s, XB_LEFT, s.hatx < 0); setbit(s, XB_RIGHT, s.hatx > 0);
  setbit(s, XB_UP, s.haty < 0);   setbit(s, XB_DOWN, s.haty > 0);
}

void nc4_event(LGNC_ADDITIONAL_INPUT_INFO_T* e) {
  if (!e) return;
  const struct input_event& ev = e->event;
  if (padlog) { fprintf(padlog, "dev=%d type=%d ev.type=%d code=0x%x value=%d\n", e->deviceID, (int)e->deviceType, ev.type, ev.code, ev.value); fflush(padlog); }
  int slot = slot_for(e->deviceID);
  if (slot < 0) return;
  bool xpad = e->deviceType == LGNC_INPUT_TYPE_XPAD;
  std::lock_guard<std::mutex> lk(pmtx);
  PadState& s = pads[slot];
  s.connected = true;
  if (ev.type == EV_KEY) {
    bool on = ev.value != 0;
    switch (ev.code) {
      case 0x130: setbit(s, XB_A, on); break;        // BTN_SOUTH / BTN_A
      case 0x131: setbit(s, XB_B, on); break;        // BTN_EAST / BTN_B
      case 0x133: setbit(s, XB_X, on); break;        // BTN_NORTH / BTN_X
      case 0x134: setbit(s, XB_Y, on); break;        // BTN_WEST / BTN_Y
      case 0x136: setbit(s, XB_LB, on); break;       // BTN_TL
      case 0x137: setbit(s, XB_RB, on); break;       // BTN_TR
      case 0x138: if (!xpad) s.lt = on ? 255 : 0; break;  // BTN_TL2 (digital triggers on HID pads)
      case 0x139: if (!xpad) s.rt = on ? 255 : 0; break;  // BTN_TR2
      case 0x13a: setbit(s, XB_BACK, on); break;     // BTN_SELECT
      case 0x13b: setbit(s, XB_START, on); break;    // BTN_START
      case 0x13d: setbit(s, XB_LTHUMB, on); break;   // BTN_THUMBL
      case 0x13e: setbit(s, XB_RTHUMB, on); break;   // BTN_THUMBR
      case 0x220: setbit(s, XB_UP, on); break;       // BTN_DPAD_*
      case 0x221: setbit(s, XB_DOWN, on); break;
      case 0x222: setbit(s, XB_LEFT, on); break;
      case 0x223: setbit(s, XB_RIGHT, on); break;
      case 0x2c0: setbit(s, XB_LEFT, on); break;     // xpad "dpad as buttons" (BTN_TRIGGER_HAPPY1..4)
      case 0x2c1: setbit(s, XB_RIGHT, on); break;
      case 0x2c2: setbit(s, XB_UP, on); break;
      case 0x2c3: setbit(s, XB_DOWN, on); break;
      default: break;
    }
  } else if (ev.type == EV_ABS) {
    int v = ev.value;
    // generic HID pads report 0..255 centred on 128, xpad 16-bit signed
    int sv = xpad ? clamp16(v) : clamp16((v - 128) * 257);
    switch (ev.code) {
      case ABS_X: s.lx = sv; break;
      case ABS_Y: s.ly = clamp16(-sv); break;   // evdev Y is down-positive
      case ABS_Z:  if (xpad) s.lt = v < 0 ? 0 : v > 255 ? 255 : v; else s.rx = sv; break;
      case ABS_RX: s.rx = sv; break;
      case ABS_RY: s.ry = clamp16(-sv); break;
      case ABS_RZ: if (xpad) s.rt = v < 0 ? 0 : v > 255 ? 255 : v; else s.ry = clamp16(-sv); break;
      case ABS_HAT0X: s.hatx = v; hat_to_dpad(s); break;
      case ABS_HAT0Y: s.haty = v; hat_to_dpad(s); break;
      default: break;
    }
  }
}
void nc4_hotplug(LGNC_GAMEPAD_INFO* info, int count) {
  if (padlog) { fprintf(padlog, "hotplug: id=%d type=%d count=%d\n", info ? info->id : -1, info ? info->type : -1, count); fflush(padlog); }
  std::lock_guard<std::mutex> lk(pmtx);
  if (count <= 0) { for (auto& p : pads) p = PadState(); return; }
  pads[0].connected = true;
}
#endif

}  // namespace

void pad_init() {
  if (const char* s = getenv("HM_PAD")) parse_script(s);
#ifdef HM_NC4
  if (getenv("HM_PAD_LOG")) padlog = fopen((G->workdir + "/pad.log").c_str(), "a");
  int rc = LGNC_GAMEPAD_RegisterCallback(nc4_event, nc4_hotplug);
  fprintf(stderr, "LGNC_GAMEPAD_RegisterCallback = %d\n", rc);
#else
  if (SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0) sdl_open_first();
#endif
}

void pad_update(int frame_no) {
  if (scripted) {
    std::lock_guard<std::mutex> lk(pmtx);
    pads[0] = PadState();
    for (auto& sc : script) if (frame_no >= sc.a && frame_no <= sc.b) pads[0] = sc.st;
    pads[0].connected = true;
    return;
  }
#ifndef HM_NC4
  if (!sdlpad) { if (frame_no % 60 == 0) sdl_open_first(); }
  else if (!SDL_GameControllerGetAttached(sdlpad)) { SDL_GameControllerClose(sdlpad); sdlpad = nullptr; std::lock_guard<std::mutex> lk(pmtx); pads[0] = PadState(); }
  if (sdlpad) {
    std::lock_guard<std::mutex> lk(pmtx);
    PadState& s = pads[0];
    s.connected = true;
    auto B = [&](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(sdlpad, b) != 0; };
    int m = 0;
    if (B(SDL_CONTROLLER_BUTTON_DPAD_UP)) m |= XB_UP;
    if (B(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) m |= XB_DOWN;
    if (B(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) m |= XB_LEFT;
    if (B(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) m |= XB_RIGHT;
    if (B(SDL_CONTROLLER_BUTTON_START)) m |= XB_START;
    if (B(SDL_CONTROLLER_BUTTON_BACK)) m |= XB_BACK;
    if (B(SDL_CONTROLLER_BUTTON_LEFTSTICK)) m |= XB_LTHUMB;
    if (B(SDL_CONTROLLER_BUTTON_RIGHTSTICK)) m |= XB_RTHUMB;
    if (B(SDL_CONTROLLER_BUTTON_LEFTSHOULDER)) m |= XB_LB;
    if (B(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)) m |= XB_RB;
    if (B(SDL_CONTROLLER_BUTTON_A)) m |= XB_A;
    if (B(SDL_CONTROLLER_BUTTON_B)) m |= XB_B;
    if (B(SDL_CONTROLLER_BUTTON_X)) m |= XB_X;
    if (B(SDL_CONTROLLER_BUTTON_Y)) m |= XB_Y;
    s.buttons = m;
    auto A = [&](SDL_GameControllerAxis a) { return (int)SDL_GameControllerGetAxis(sdlpad, a); };
    s.lx = A(SDL_CONTROLLER_AXIS_LEFTX);  s.ly = clamp16(-A(SDL_CONTROLLER_AXIS_LEFTY));
    s.rx = A(SDL_CONTROLLER_AXIS_RIGHTX); s.ry = clamp16(-A(SDL_CONTROLLER_AXIS_RIGHTY));
    s.lt = A(SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7;
    s.rt = A(SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7;
  }
#else
  (void)frame_no;
#endif
}

bool pad_connected(int idx) {
  std::lock_guard<std::mutex> lk(pmtx);
  return idx >= 0 && idx < MAXPAD && pads[idx].connected;
}

double pad_ext_call(const std::string& fn, double a0, double a1, double a2) {
  int idx = (int)a0;
  if (fn == "setRumble") {
#ifndef HM_NC4
    if (sdlpad && idx == 0) SDL_GameControllerRumble(sdlpad, (Uint16)std::max(0.0, std::min(65535.0, a1)), (Uint16)std::max(0.0, std::min(65535.0, a2)), 120);
#endif
    return 0;
  }
  if (idx < 0 || idx >= MAXPAD) return 0;
  std::lock_guard<std::mutex> lk(pmtx);
  const PadState& s = pads[idx];
  if (fn == "getCtrlState") return s.connected ? 0 : 1167;  // ERROR_SUCCESS / ERROR_DEVICE_NOT_CONNECTED
  if (fn == "getButtonState") return s.buttons;
  if (fn == "checkButton") return (s.buttons & (int)a1) ? 1 : 0;
  if (fn == "leftTrigger") return s.lt;
  if (fn == "rightTrigger") return s.rt;
  if (fn == "leftThumbX") return s.lx;
  if (fn == "leftThumbY") return s.ly;
  if (fn == "rightThumbX") return s.rx;
  if (fn == "rightThumbY") return s.ry;
  return 0;
}
