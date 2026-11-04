// Internal engine state shared between runtime translation units.
#pragma once
#include "rt.h"
#include "../gen/ids.h"
#include <algorithm>
#include <functional>
#include <set>

struct DrawState {
  uint32_t color = 0xFFFFFF;  // BGR (GM style)
  double alpha = 1;
  int blend = 0;
  int halign = 0, valign = 0;
  int font = -1;
  bool fog = false;
  uint32_t fogcol = 0xFFFFFF;
  bool interp = false;
};
struct Surface {
  SDL_Texture* tex = nullptr;
  int w = 0, h = 0;
  bool used = false;
  bool dirty = true;        // may contain non-transparent pixels
  unsigned long base = 0, clearbase = 0;
  bool cleared = false;
};
struct Proj { double ox = 0, oy = 0, sx = 1, sy = 1; };
struct PathPt { double x, y, speed; };
struct PathDef {
  std::vector<PathPt> pts;
  bool closed = true;
  int kind = 0, prec = 4;
  double length = 0;
};
struct MpGrid {
  int hcells, vcells;
  double left, top, cw, ch;
  std::vector<uint8_t> blocked;
};
struct SoundVoice {
  int snd = -1;
  const int16_t* data = nullptr;
  size_t frames = 0, pos = 0;
  float vol = 1;
  bool loop = false, active = false;
  int handle = 0;
  std::shared_ptr<std::vector<int16_t>> own;  // for streams
};

struct Game {
  // window / renderer
  SDL_Window* win = nullptr;
  SDL_Renderer* ren = nullptr;
  SDL_Texture* frame = nullptr;
  int winw = 1366, winh = 768;
  int fbw = 1366, fbh = 768;  // framebuffer (logical window)
  int rw = 1366, rh = 768;    // size of the frame texture actually rendered (internal resolution)
  double rscale = 1.0;        // rw / fbw: logical -> frame pixels
  bool headless = false;

  // resources
  std::vector<Sprite> spr;
  std::vector<Background> bg;
  std::vector<Font> fnt;
  std::vector<Sound> snd;
  std::vector<Room> rooms;
  std::string root;      // GameMaker project dir
  std::string workdir;   // writable working directory
  std::unordered_map<std::string, int> obj_by_name, spr_by_name, bg_by_name, snd_by_name, fnt_by_name, rm_by_name;

  // instances
  std::vector<Inst*> insts;
  std::unordered_map<int, Inst*> byid;
  std::vector<std::vector<Inst*>> byobj;
  std::vector<std::vector<int>> desc;  // desc[o]: o and all its descendants
  std::vector<Inst*> trash;
  std::vector<std::vector<EvFn>> evcache;
  std::unordered_map<long long, EvFn> evmap[1];
  int nextid = 100001;
  Inst dummy;

  // rooms
  int room = -1, next_room = -1;
  bool game_started = false;
  bool restart_game = false, quit = false;
  struct Tile { int bg; double x, y, w, h, xo, yo; int depth; double sx, sy; uint32_t col; int id; bool visible; };
  std::vector<Tile> tiles;
  int tile_layer_hidden_depth_min = 0;

  // globals
  std::vector<Value> gv;
  Value gb[GB__COUNT][8];

  // input
  bool keys[512];
  bool keys_pressed[512];
  bool keys_released[512];
  bool mbtn[8], mbtn_pressed[8], mbtn_released[8];
  int mouse_wx = 0, mouse_wy = 0;
  int wheel = 0;
  int last_key = 0;

  // drawing
  DrawState ds;
  std::vector<Surface> surfaces;
  std::vector<int> target_stack;
  int cur_target = -1;
  Proj proj;
  std::vector<Proj> proj_stack;
  double view_ox = 0, view_oy = 0;

  // ds structures / paths
  std::vector<std::vector<Value>> dslists;
  std::vector<bool> dslist_used;
  std::vector<PathDef> paths;
  std::vector<MpGrid> grids;

  // audio
  SDL_AudioDeviceID adev = 0;
  std::vector<SoundVoice> voices;
  int next_voice = 1;

  // timing
  double fps = 0;
  uint64_t frame_no = 0;
  std::map<int, std::vector<std::string>> files_dummy;
};

extern Game* G;

// rt.cpp
bool bbox_of(const Inst* i, double x, double y, double& l, double& t, double& r, double& b);
bool inst_meets(const Inst* a, double ax, double ay, const Inst* b);
void run_event(Inst* i, int type, int num, int otherobj = -1, Inst* other = nullptr);
bool has_event(int obj, int type, int num, int other = -1);
EvFn find_event(int obj, int type, int num, int other);
Inst* alloc_instance(double x, double y, int obj);
void load_game_data(const std::string& datfile);
Sprite& ensure_sprite(int s);
void ensure_bg(int b);
void ensure_font(int f);
void ensure_sound(int s);
void game_init_video();
void run_frame();
void do_game_save(const std::string& file);
bool do_game_load(const std::string& file);

// draw.cpp
void draw_begin_frame();
void draw_end_frame();
void draw_room_views();
void draw_sprite_full(int spr, double frame, double x, double y, double xs, double ys, double rot, uint32_t col, double alpha);
SDL_Texture* make_texture_from_png(const std::string& path, int* w, int* h, std::vector<uint8_t>* alpha = nullptr, bool white = false);
void draw_shutdown();

// audio.cpp
void audio_init();
int audio_play(int snd, bool loop, float vol);
void audio_stop_voice(int handle);
void audio_stop_snd(int snd);
bool audio_playing(int handle_or_snd);
void audio_gain(int handle, float v);
int audio_play_file(const std::string& path, bool loop, float vol);
