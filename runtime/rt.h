// Runtime for the GML -> C++ port of Hotline Miami (SDL2)
#pragma once
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED  // plain main() on every platform (no SDL2main needed on Windows)
#endif
#include <SDL.h>
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "builtins.h"

struct Arr;
struct Inst;

// ------------------------------------------------------------------ Value
struct Value {
  enum Type : uint8_t { R, S, A } t = R;
  double d = 0;
  std::string s;
  std::shared_ptr<Arr> a;

  Value() {}
  Value(double x) : d(x) {}
  Value(int x) : d(x) {}
  Value(long x) : d((double)x) {}
  Value(bool x) : d(x ? 1.0 : 0.0) {}
  Value(const char* x) : t(S), s(x) {}
  Value(const std::string& x) : t(S), s(x) {}
  Value(const Value& o);
  Value(Value&& o) noexcept = default;
  Value& operator=(const Value& o);
  Value& operator=(Value&& o) noexcept = default;
  Value& operator=(double x) { t = R; d = x; s.clear(); a.reset(); return *this; }
  Value& at(int i);
  Value& at(int i, int j);
  bool isStr() const { return t == S; }
};

struct Arr {
  std::vector<Value> v;
  std::unordered_map<long long, Value> m2;
  bool two = false;
};

double Num(const Value& v);
std::string Str(const Value& v);
inline bool T(const Value& v) { return v.t == Value::R ? v.d >= 0.5 : false; }
bool Eq(const Value& a, const Value& b);
int Cmp(const Value& a, const Value& b);
Value operator+(const Value& a, const Value& b);
Value operator-(const Value& a, const Value& b);
Value operator*(const Value& a, const Value& b);
Value operator/(const Value& a, const Value& b);
Value operator-(const Value& a);
Value Div(const Value& a, const Value& b);
Value Mod(const Value& a, const Value& b);
Value BitAnd(const Value& a, const Value& b);
Value BitOr(const Value& a, const Value& b);
Value BitXor(const Value& a, const Value& b);
Value BitShl(const Value& a, const Value& b);
Value BitShr(const Value& a, const Value& b);
Value ArrGet(const Value& v, int i);
Value ArrGet(const Value& v, int i, int j);

struct Args {
  const Value* p;
  int n;
  Args() : p(nullptr), n(0) {}
  Args(std::initializer_list<Value> l) : p(l.begin()), n((int)l.size()) {}
  const Value& operator[](int i) const;
  Value& ref(int i) const { return const_cast<Value&>((*this)[i]); }
  int size() const { return n; }
};

// ------------------------------------------------------------------ definitions
typedef void (*EvFn)(Inst* self, Inst* other);
typedef Value (*ScriptFn)(Inst* self, Inst* other, Args args);

struct EvDef {
  int type, num, other;
  EvFn fn;
};
struct ObjDef {
  const char* name;
  int sprite, mask, parent;
  int solid, visible, depth, persistent;
  int nev;
  std::vector<EvDef> events;
};

extern const char* const g_obj_names[];
extern const char* const g_spr_names[];
extern const char* const g_snd_names[];
extern const char* const g_bg_names[];
extern const char* const g_fnt_names[];
extern const char* const g_rm_names[];
extern const char* const g_var_names[];
extern const char* const g_scr_names[];
extern const ScriptFn g_scripts[];
extern const EvFn g_room_code[];
extern const EvFn g_inst_code[];
extern const ObjDef g_objs[];
extern const int g_obj_count;

// ------------------------------------------------------------------ instances
struct Inst {
  int id = 0;
  int obj = -1;
  bool alive = true;
  bool created = false;
  double x = 0, y = 0, xprevious = 0, yprevious = 0, xstart = 0, ystart = 0;
  double hspeed = 0, vspeed = 0, speed = 0, direction = 0, friction = 0, gravity = 0, gravity_direction = 270;
  int sprite_index = -1, mask_index = -1;
  double image_index = 0, image_speed = 1, image_xscale = 1, image_yscale = 1, image_angle = 0, image_alpha = 1;
  uint32_t image_blend = 0xFFFFFF;
  bool visible = true, solid = false, persistent = false;
  int depth = 0;
  int alarm[12];
  int path_index = -1;
  double path_position = 0, path_positionprevious = 0, path_speed = 0, path_scale = 1, path_orientation = 0;
  int path_endaction = 0;
  double path_ox = 0, path_oy = 0;
  std::vector<std::pair<int, Value>> vars;  // sorted by id

  Inst() { for (int i = 0; i < 12; i++) alarm[i] = -1; }
  Value& var(int id);
  const Value& vget(int id) const;
  bool hasvar(int id) const;
  Value get(int bi, int idx = 0) const;
  void set(int bi, int idx, const Value& v);
  void set_hv();   // recompute speed/direction from h/vspeed
  void set_sd();   // recompute h/vspeed from speed/direction
};

struct BRef {
  Inst* i;
  int id, idx;
  BRef(Inst* i_, int id_, int idx_ = 0) : i(i_), id(id_), idx(idx_) {}
  operator Value() const;
  BRef& operator=(const Value& v);
  BRef& operator=(const BRef& o) { return *this = (Value)o; }
};
Value GBget(int id, int idx = 0);
void GBset(int id, int idx, const Value& v);
Value& GV(int id);

Inst* ToInst(const Value& v, Inst* self, Inst* other);
Inst* FirstInst(int obj);
std::vector<Inst*> WithList(const Value& v, Inst* self, Inst* other);
void EventInherited(Inst* self, Inst* other, int obj, int type, int num);

// ------------------------------------------------------------------ resources
struct Sprite {
  std::string name;
  int xorig = 0, yorig = 0, bbl = 0, bbr = 0, bbt = 0, bbb = 0, w = 0, h = 0, colkind = 0, bboxmode = 0;
  std::vector<std::string> frames;
  std::vector<SDL_Texture*> tex;
  std::vector<SDL_Texture*> white;  // silhouette variants (fog)
  std::vector<std::vector<uint8_t>> mask;  // alpha masks (lazy)
  bool loaded = false;
  bool dynamic = false;
};
struct Background {
  std::string name, path;
  int w = 0, h = 0, istile = 0, tw = 0, th = 0;
  SDL_Texture* tex = nullptr;
  bool loaded = false;
};
struct Glyph { int x, y, w, h, shift, offset; };
struct Font {
  std::string name;
  int size = 12, bold = 0, italic = 0;
  std::map<int, Glyph> glyphs;
  SDL_Texture* tex = nullptr;
  int lineh = 0;
  bool loaded = false;
};
struct Sound {
  std::string name, file;
  double volume = 1;
  int kind = 0;
  std::vector<int16_t> pcm;  // stereo 44100
  bool loaded = false;
};
struct RoomInst { int obj; double x, y, sx, sy, rot; uint32_t col; int code; };
struct RoomTile { int bg; double x, y, w, h, xo, yo; int depth; double sx, sy; uint32_t col; int id; };
struct RoomView {
  int visible, xview, yview, wview, hview, xport, yport, wport, hport, hborder, vborder, hspeed, vspeed;
  int obj;
};
struct RoomBg {
  int visible, fore, bg;
  double x, y, hspeed, vspeed;
  int htiled, vtiled, stretch;
};
struct Room {
  std::string name;
  int w = 0, h = 0, speed = 30, persistent = 0;
  uint32_t colour = 0;
  int showcolour = 0, enableViews = 0;
  RoomView views[8];
  RoomBg bgs[8];
  std::vector<RoomInst> insts;
  std::vector<RoomTile> tiles;
};

struct Game;
extern Game* G;

// engine API (rt.cpp)
Inst* instance_create_obj(double x, double y, int obj);
void instance_destroy_inst(Inst* i);
bool inst_is_a(int obj, int ancestor);
void room_goto_index(int r);
void engine_run();
void start_game(int room);
std::string gm_path(const std::string& p);  // maps GM-style path to host path
void draw_init(int w, int h);

// misc helpers shared by funcs
double lengthdir_x_(double l, double d);
double lengthdir_y_(double l, double d);

// all F_* functions are declared in funcs.h
#include "funcs.h"
