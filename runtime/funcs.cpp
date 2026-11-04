#include "game.h"
#include "pad.h"
#include <cctype>
#include <fstream>
#include <queue>
#include <random>

#define FN(n) Value F_##n(Inst* self, Inst* other, Args a)
#define N(i) Num(a[i])
static const double DEG = M_PI / 180.0;

bool gm_key_down(int vk);
bool gm_key_pressed(int vk);
void setup_room_state(int r);
void kill_all_instances();
Inst* register_instance(Inst* i);

static std::mt19937_64 rng(12345);
static double rnd01() { return std::uniform_real_distribution<double>(0.0, 1.0)(rng); }

// ------------------------------------------------------------------ math
FN(abs) { return std::fabs(N(0)); }
FN(ceil) { return std::ceil(N(0)); }
FN(floor) { return std::floor(N(0)); }
FN(round) { double x = N(0); return x < 0 ? -std::floor(-x + 0.5) : std::floor(x + 0.5); }
FN(frac) { double x = N(0); return x - std::trunc(x); }
FN(sign) { double x = N(0); return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; }
FN(sqr) { return N(0) * N(0); }
FN(sqrt) { return std::sqrt(N(0)); }
FN(sin) { return std::sin(N(0)); }
FN(clamp) { return std::max(N(1), std::min(N(2), N(0))); }
FN(random) { return rnd01() * N(0); }
FN(choose) {
  if (a.n == 0) return 0;
  return a[(int)(rnd01() * a.n) % a.n];
}
FN(lengthdir_x) { return lengthdir_x_(N(0), N(1)); }
FN(lengthdir_y) { return lengthdir_y_(N(0), N(1)); }
FN(point_distance) { return std::hypot(N(2) - N(0), N(3) - N(1)); }
FN(point_direction) {
  double dx = N(2) - N(0), dy = N(3) - N(1);
  if (dx == 0 && dy == 0) return 0;
  double d = std::atan2(-dy, dx) / DEG;
  if (d < 0) d += 360;
  return d;
}
FN(merge_color) {
  uint32_t c1 = (uint32_t)(long long)N(0), c2 = (uint32_t)(long long)N(1);
  double t = std::max(0.0, std::min(1.0, N(2)));
  auto ch = [&](int sh) { return (uint32_t)std::floor(((c1 >> sh) & 255) * (1 - t) + ((c2 >> sh) & 255) * t); };
  return (double)(ch(0) | (ch(8) << 8) | (ch(16) << 16));
}
FN(make_color_hsv) {
  double h = N(0) / 255.0 * 360.0, s = N(1) / 255.0, v = N(2) / 255.0;
  double c = v * s, hh = std::fmod(h, 360.0) / 60.0, x = c * (1 - std::fabs(std::fmod(hh, 2.0) - 1)), m = v - c;
  double r = 0, g = 0, b = 0;
  int sec = (int)hh;
  switch (sec) {
    case 0: r = c; g = x; break;
    case 1: r = x; g = c; break;
    case 2: g = c; b = x; break;
    case 3: g = x; b = c; break;
    case 4: r = x; b = c; break;
    default: r = c; b = x; break;
  }
  uint32_t R = (uint32_t)std::round((r + m) * 255), Gc = (uint32_t)std::round((g + m) * 255), B = (uint32_t)std::round((b + m) * 255);
  return (double)(R | (Gc << 8) | (B << 16));
}
FN(array_length_1d) { const Value& v = a[0]; return v.t == Value::A && v.a ? (double)v.a->v.size() : 0.0; }

// ------------------------------------------------------------------ strings
FN(string) { return Value(Str(a[0])); }
FN(real) { return a[0].t == Value::S ? atof(a[0].s.c_str()) : a[0].d; }
FN(ord) { std::string s = Str(a[0]); return s.empty() ? 0.0 : (double)(unsigned char)s[0]; }
FN(chr) { return Value(std::string(1, (char)(int)N(0))); }
FN(string_length) { return (double)Str(a[0]).size(); }
FN(string_upper) { std::string s = Str(a[0]); for (auto& c : s) c = toupper((unsigned char)c); return s; }
FN(string_lower) { std::string s = Str(a[0]); for (auto& c : s) c = tolower((unsigned char)c); return s; }
FN(string_copy) {
  std::string s = Str(a[0]);
  int i = (int)N(1) - 1, n = (int)N(2);
  if (i < 0) i = 0;
  if (i >= (int)s.size() || n <= 0) return Value(std::string());
  return Value(s.substr(i, n));
}
FN(string_replace) {
  std::string s = Str(a[0]), f = Str(a[1]), r = Str(a[2]);
  if (f.empty()) return Value(s);
  size_t p = s.find(f);
  if (p != std::string::npos) s.replace(p, f.size(), r);
  return Value(s);
}
FN(string_count) {
  std::string sub = Str(a[0]), s = Str(a[1]);
  if (sub.empty()) return 0;
  int n = 0;
  for (size_t p = s.find(sub); p != std::string::npos; p = s.find(sub, p + sub.size())) n++;
  return n;
}
FN(string_digits) {
  std::string s = Str(a[0]), o;
  for (char c : s) if (isdigit((unsigned char)c)) o.push_back(c);
  return Value(o);
}

// ------------------------------------------------------------------ instances
FN(instance_create) { return (double)instance_create_obj(N(0), N(1), (int)N(2))->id; }
FN(instance_destroy) {
  if (a.n == 0) { instance_destroy_inst(self); return 0; }
  for (Inst* i : WithList(a[0], self, other)) instance_destroy_inst(i);
  return 0;
}
FN(instance_exists) {
  int n = (int)N(0);
  if (n == -3) return G->insts.empty() ? 0 : 1;
  if (n >= 100000) { auto it = G->byid.find(n); return it != G->byid.end() && it->second->alive ? 1 : 0; }
  if (n < 0 || n >= g_obj_count) return 0;
  for (int o : G->desc[n]) for (Inst* i : G->byobj[o]) if (i->alive) return 1;
  return 0;
}
FN(instance_number) {
  int n = (int)N(0), c = 0;
  if (n == -3) { for (Inst* i : G->insts) if (i->alive) c++; return c; }
  if (n < 0 || n >= g_obj_count) return 0;
  for (int o : G->desc[n]) for (Inst* i : G->byobj[o]) if (i->alive) c++;
  return c;
}
FN(instance_find) {
  auto l = WithList(a[0], self, other);
  int k = (int)N(1);
  if (k < 0 || k >= (int)l.size()) return -4;
  return l[k]->id;
}
FN(instance_nearest) {
  double x = N(0), y = N(1), best = 1e18;
  Inst* r = nullptr;
  for (Inst* i : WithList(a[2], self, other)) {
    double d = (i->x - x) * (i->x - x) + (i->y - y) * (i->y - y);
    if (d < best) { best = d; r = i; }
  }
  return r ? (double)r->id : -4.0;
}
FN(object_get_parent) {
  int o = (int)N(0);
  return o >= 0 && o < g_obj_count ? g_objs[o].parent : -1;
}
static std::vector<Inst*> inactive;
FN(instance_deactivate_all) {
  bool notme = T(a[0]);
  std::vector<Inst*> keep;
  for (Inst* i : G->insts) {
    if (notme && i == self) { keep.push_back(i); continue; }
    auto& v = G->byobj[i->obj];
    v.erase(std::remove(v.begin(), v.end(), i), v.end());
    inactive.push_back(i);
  }
  G->insts.swap(keep);
  return 0;
}
FN(instance_deactivate_object) {
  int o = (int)N(0);
  if (o < 0 || o >= g_obj_count) return 0;
  std::vector<Inst*> keep;
  for (Inst* i : G->insts) {
    if (inst_is_a(i->obj, o)) {
      auto& v = G->byobj[i->obj];
      v.erase(std::remove(v.begin(), v.end(), i), v.end());
      inactive.push_back(i);
    } else keep.push_back(i);
  }
  G->insts.swap(keep);
  return 0;
}
FN(instance_activate_all) {
  for (Inst* i : inactive) { G->insts.push_back(i); G->byobj[i->obj].push_back(i); }
  inactive.clear();
  std::stable_sort(G->insts.begin(), G->insts.end(), [](Inst* x, Inst* y) { return x->id < y->id; });
  return 0;
}
FN(event_perform) {
  run_event(self, (int)N(0), (int)N(1));
  return 0;
}

// ------------------------------------------------------------------ collisions & movement
static bool meets_candidates(Inst* self, double x, double y, const Value& which, bool solid_only, Inst** hit) {
  int n = (int)Num(which);
  auto test = [&](Inst* b) {
    if (!b->alive || b == self) return false;
    if (solid_only && !b->solid) return false;
    if (inst_meets(self, x, y, b)) { if (hit) *hit = b; return true; }
    return false;
  };
  if (n == -3) { for (Inst* b : G->insts) if (test(b)) return true; return false; }
  if (n >= 100000) { auto it = G->byid.find(n); return it != G->byid.end() && test(it->second); }
  if (n == -1) return false;
  if (n < 0 || n >= g_obj_count) return false;
  for (int o : G->desc[n]) for (Inst* b : G->byobj[o]) if (test(b)) return true;
  return false;
}
static bool place_free_at(Inst* self, double x, double y) {
  for (Inst* b : G->insts)
    if (b != self && b->alive && b->solid && inst_meets(self, x, y, b)) return false;
  return true;
}
FN(place_meeting) { return meets_candidates(self, N(0), N(1), a[2], false, nullptr) ? 1 : 0; }
FN(place_free) { return place_free_at(self, N(0), N(1)) ? 1 : 0; }
FN(collision_line) {
  double x1 = N(0), y1 = N(1), x2 = N(2), y2 = N(3);
  bool notme = T(a[5]);
  auto hit = [&](Inst* b) {
    if (!b->alive || (notme && b == self)) return false;
    double l, t, r, bt;
    if (!bbox_of(b, b->x, b->y, l, t, r, bt)) return false;
    // Liang-Barsky segment vs rect
    double dx = x2 - x1, dy = y2 - y1, t0 = 0, t1 = 1;
    double p[4] = {-dx, dx, -dy, dy}, q[4] = {x1 - l, r - x1, y1 - t, bt - y1};
    for (int k = 0; k < 4; k++) {
      if (p[k] == 0) { if (q[k] < 0) return false; }
      else {
        double u = q[k] / p[k];
        if (p[k] < 0) { if (u > t1) return false; if (u > t0) t0 = u; }
        else { if (u < t0) return false; if (u < t1) t1 = u; }
      }
    }
    return true;
  };
  int n = (int)N(4);
  if (n == -3) { for (Inst* b : G->insts) if (hit(b)) return b->id; return -4; }
  if (n >= 100000) { auto it = G->byid.find(n); return it != G->byid.end() && hit(it->second) ? n : -4; }
  if (n < 0 || n >= g_obj_count) return -4;
  for (int o : G->desc[n]) for (Inst* b : G->byobj[o]) if (hit(b)) return b->id;
  return -4;
}
FN(motion_add) {
  double d = N(0), s = N(1);
  self->hspeed += s * std::cos(d * DEG);
  self->vspeed += -s * std::sin(d * DEG);
  self->set_hv();
  return 0;
}
static Value contact(Inst* self, double dir, double maxd, bool solid_only) {
  double dx = std::cos(dir * DEG), dy = -std::sin(dir * DEG);
  if (maxd <= 0) maxd = 1000;
  auto free_at = [&](double x, double y) {
    if (solid_only) return place_free_at(self, x, y);
    for (Inst* b : G->insts) if (b != self && b->alive && inst_meets(self, x, y, b)) return false;
    return true;
  };
  if (!free_at(self->x, self->y)) return 0;
  for (double d = 0; d < maxd; d += 1) {
    double step = std::min(1.0, maxd - d);
    if (!free_at(self->x + dx * step, self->y + dy * step)) break;
    self->x += dx * step; self->y += dy * step;
  }
  return 0;
}
FN(move_contact_solid) { return contact(self, N(0), N(1), true); }
FN(move_contact_all) { return contact(self, N(0), N(1), false); }
FN(move_outside_solid) {
  double dir = N(0), maxd = N(1);
  if (maxd <= 0) maxd = 1000;
  double dx = std::cos(dir * DEG), dy = -std::sin(dir * DEG);
  for (double d = 0; d < maxd && !place_free_at(self, self->x, self->y); d += 1) { self->x += dx; self->y += dy; }
  return 0;
}
static void bounce(Inst* self, bool solid_only) {
  auto free_at = [&](double x, double y) {
    if (solid_only) return place_free_at(self, x, y);
    for (Inst* b : G->insts) if (b != self && b->alive && inst_meets(self, x, y, b)) return false;
    return true;
  };
  bool bx = !free_at(self->x + self->hspeed, self->y), by = !free_at(self->x, self->y + self->vspeed);
  if (bx) self->hspeed = -self->hspeed;
  if (by) self->vspeed = -self->vspeed;
  if (!bx && !by && !free_at(self->x + self->hspeed, self->y + self->vspeed)) { self->hspeed = -self->hspeed; self->vspeed = -self->vspeed; }
  self->set_hv();
}
FN(move_bounce_solid) { bounce(self, true); return 0; }
FN(move_bounce_all) { bounce(self, false); return 0; }

// ------------------------------------------------------------------ rooms / game
FN(room_goto) { room_goto_index((int)N(0)); return 0; }
FN(room_goto_next) { room_goto_index(G->room + 1); return 0; }
FN(room_next) { int r = (int)N(0) + 1; return r < RM__COUNT ? r : -1; }
FN(room_previous) { int r = (int)N(0) - 1; return r >= 0 ? r : -1; }
FN(room_restart) { room_goto_index(G->room); return 0; }
FN(room_exists) { int r = (int)N(0); return r >= 0 && r < RM__COUNT ? 1 : 0; }
FN(room_get_name) { int r = (int)N(0); return r >= 0 && r < RM__COUNT ? Value(g_rm_names[r]) : Value("<undefined>"); }
FN(room_set_persistent) { return 0; }
FN(room_set_view) {
  int r = (int)N(0), v = (int)N(1);
  if (r < 0 || r >= RM__COUNT || v < 0 || v > 7) return 0;
  RoomView& rv = G->rooms[r].views[v];
  rv.visible = (int)N(2); rv.xview = (int)N(3); rv.yview = (int)N(4); rv.wview = (int)N(5); rv.hview = (int)N(6);
  rv.xport = (int)N(7); rv.yport = (int)N(8); rv.wport = (int)N(9); rv.hport = (int)N(10);
  rv.hborder = (int)N(11); rv.vborder = (int)N(12); rv.hspeed = (int)N(13); rv.vspeed = (int)N(14);
  rv.obj = (int)N(15) >= 0 ? (int)N(15) : -1;
  return 0;
}
FN(game_end) { G->quit = true; return 0; }
FN(game_restart) { G->restart_game = true; return 0; }
FN(show_message) { fprintf(stderr, "[show_message] %s\n", Str(a[0]).c_str()); return 0; }
FN(window_handle) { return 0; }
FN(display_get_width) { SDL_DisplayMode m; if (SDL_GetDesktopDisplayMode(0, &m) == 0) return m.w; return 1366; }
FN(display_get_height) { SDL_DisplayMode m; if (SDL_GetDesktopDisplayMode(0, &m) == 0) return m.h; return 768; }
FN(display_mouse_get_x) { int x, y; SDL_GetGlobalMouseState(&x, &y); return x; }
FN(display_mouse_get_y) { int x, y; SDL_GetGlobalMouseState(&x, &y); return y; }

// tiles
FN(tile_layer_hide) { int d = (int)N(0); for (auto& t : G->tiles) if (t.depth == d) t.visible = false; return 0; }
FN(tile_layer_show) { int d = (int)N(0); for (auto& t : G->tiles) if (t.depth == d) t.visible = true; return 0; }
FN(tile_layer_find) {
  int d = (int)N(0); double x = N(1), y = N(2);
  for (auto& t : G->tiles) if (t.depth == d && x >= t.x && x < t.x + t.w && y >= t.y && y < t.y + t.h) return t.id;
  return -1;
}
FN(tile_exists) { int id = (int)N(0); for (auto& t : G->tiles) if (t.id == id) return 1; return 0; }
FN(tile_delete) {
  int id = (int)N(0);
  G->tiles.erase(std::remove_if(G->tiles.begin(), G->tiles.end(), [&](const Game::Tile& t) { return t.id == id; }), G->tiles.end());
  return 0;
}

// ------------------------------------------------------------------ input
FN(keyboard_check) { return gm_key_down((int)N(0)) ? 1 : 0; }
FN(keyboard_check_direct) {
  bool r = gm_key_down((int)N(0));
  static bool tr = getenv("HM_TRACE_KEYS") != nullptr;
  if (tr) fprintf(stderr, "[kcd] frame %llu key=%d -> %d (self=%s)\n", (unsigned long long)G->frame_no, (int)N(0), (int)r, g_objs[self->obj].name);
  return r ? 1 : 0;
}
FN(keyboard_check_pressed) { return gm_key_pressed((int)N(0)) ? 1 : 0; }
static int mbidx(int b) { return b == 1 ? 1 : b == 2 ? 3 : b == 3 ? 2 : 0; }
FN(mouse_check_button) {
  int b = (int)N(0);
  if (b == -1) return (G->mbtn[1] || G->mbtn[2] || G->mbtn[3]) ? 1 : 0;
  return G->mbtn[mbidx(b)] ? 1 : 0;
}
FN(mouse_check_button_pressed) {
  int b = (int)N(0);
  if (b == -1) return (G->mbtn_pressed[1] || G->mbtn_pressed[2] || G->mbtn_pressed[3]) ? 1 : 0;
  return G->mbtn_pressed[mbidx(b)] ? 1 : 0;
}
FN(mouse_clear) {
  int b = (int)N(0);
  if (b == -1) { for (int i = 0; i < 8; i++) { G->mbtn[i] = false; G->mbtn_pressed[i] = false; } }
  else { G->mbtn[mbidx(b)] = false; G->mbtn_pressed[mbidx(b)] = false; }
  return 0;
}

// ------------------------------------------------------------------ ds_list
static int dsl_new() {
  for (size_t i = 0; i < G->dslists.size(); i++) if (!G->dslist_used[i]) { G->dslist_used[i] = true; G->dslists[i].clear(); return (int)i; }
  G->dslists.emplace_back(); G->dslist_used.push_back(true);
  return (int)G->dslists.size() - 1;
}
static std::vector<Value>* dsl(const Value& v) {
  int i = (int)Num(v);
  if (i < 0 || i >= (int)G->dslists.size() || !G->dslist_used[i]) return nullptr;
  return &G->dslists[i];
}
FN(ds_list_create) { return dsl_new(); }
FN(ds_list_destroy) { int i = (int)N(0); if (i >= 0 && i < (int)G->dslists.size()) { G->dslist_used[i] = false; G->dslists[i].clear(); } return 0; }
FN(ds_list_add) { auto* l = dsl(a[0]); if (l) for (int k = 1; k < a.n; k++) l->push_back(a[k]); return 0; }
FN(ds_list_size) { auto* l = dsl(a[0]); return l ? (double)l->size() : 0.0; }
FN(ds_list_clear) { auto* l = dsl(a[0]); if (l) l->clear(); return 0; }
FN(ds_list_find_value) {
  auto* l = dsl(a[0]); int i = (int)N(1);
  if (!l || i < 0 || i >= (int)l->size()) return 0;
  return (*l)[i];
}
FN(ds_list_replace) {
  auto* l = dsl(a[0]); int i = (int)N(1);
  if (l && i >= 0 && i < (int)l->size()) (*l)[i] = a[2];
  return 0;
}
FN(ds_list_delete) { auto* l = dsl(a[0]); int i = (int)N(1); if (l && i >= 0 && i < (int)l->size()) l->erase(l->begin() + i); return 0; }
FN(ds_list_sort) {
  auto* l = dsl(a[0]);
  if (!l) return 0;
  bool asc = T(a[1]);
  std::stable_sort(l->begin(), l->end(), [&](const Value& x, const Value& y) { int c = Cmp(x, y); return asc ? c < 0 : c > 0; });
  return 0;
}
FN(ds_list_shuffle) {
  auto* l = dsl(a[0]);
  if (l) std::shuffle(l->begin(), l->end(), rng);
  return 0;
}

// ------------------------------------------------------------------ files
struct TF { FILE* f = nullptr; bool w = false; std::string line; bool have = false; size_t pos = 0; };
static std::vector<TF> tfs;
static std::string host_path(const Value& v) {
  std::string p = gm_path(Str(v));
  if (p.empty()) return p;
  bool abs = p[0] == '/' || (p.size() > 1 && p[1] == ':');  // POSIX or Windows drive path
  if (!abs) p = gm_path(G->workdir + "/" + p);
  return p;
}
FN(file_exists) { FILE* f = fopen(host_path(a[0]).c_str(), "rb"); if (f) { fclose(f); return 1; } return 0; }
FN(file_delete) { return remove(host_path(a[0]).c_str()) == 0 ? 1 : 0; }
static int tf_open(const std::string& p, const char* mode) {
  FILE* f = fopen(p.c_str(), mode);
  if (!f) return -1;
  TF t; t.f = f; t.w = mode[0] == 'w';
  for (size_t i = 0; i < tfs.size(); i++) if (!tfs[i].f) { tfs[i] = t; return (int)i; }
  tfs.push_back(t);
  return (int)tfs.size() - 1;
}
static TF* tf(const Value& v) { int i = (int)Num(v); return (i >= 0 && i < (int)tfs.size() && tfs[i].f) ? &tfs[i] : nullptr; }
FN(file_text_open_read) { return tf_open(host_path(a[0]), "rb"); }
FN(file_text_open_write) { return tf_open(host_path(a[0]), "wb"); }
FN(file_text_close) { TF* t = tf(a[0]); if (t) { fclose(t->f); t->f = nullptr; } return 0; }
static void tf_fill(TF* t) {
  if (t->have) return;
  t->line.clear(); t->pos = 0;
  int c;
  bool any = false;
  while ((c = fgetc(t->f)) != EOF) {
    any = true;
    if (c == '\n') break;
    if (c != '\r') t->line.push_back((char)c);
  }
  t->have = any;
}
FN(file_text_read_string) { TF* t = tf(a[0]); if (!t) return Value(std::string()); tf_fill(t); return Value(t->line.substr(t->pos)); }
FN(file_text_readln) { TF* t = tf(a[0]); if (!t) return 0; tf_fill(t); t->have = false; return 0; }
FN(file_text_read_real) {
  TF* t = tf(a[0]);
  if (!t) return 0;
  tf_fill(t);
  const char* s = t->line.c_str() + t->pos;
  char* e;
  double d = strtod(s, &e);
  t->pos = e - t->line.c_str();
  return d;
}
FN(file_text_eof) {
  TF* t = tf(a[0]);
  if (!t) return 1;
  if (t->have && t->pos < t->line.size()) return 0;
  int c = fgetc(t->f);
  if (c == EOF) return 1;
  ungetc(c, t->f);
  return 0;
}
FN(file_text_write_string) { TF* t = tf(a[0]); if (t) fputs(Str(a[1]).c_str(), t->f); return 0; }
FN(file_text_writeln) { TF* t = tf(a[0]); if (t) fputc('\n', t->f); return 0; }

// ------------------------------------------------------------------ external DLL stubs
// External DLL calls: only GMXInput.dll (the XInput wrapper) is emulated, see pad.cpp; Steam/vcapture/etc. return 0.
static std::vector<std::string> g_ext_names{""};
FN(external_define) { g_ext_names.push_back(Str(a[1])); return (double)(g_ext_names.size() - 1); }
FN(external_call) {
  int id = (int)N(0);
  if (id <= 0 || id >= (int)g_ext_names.size()) return 0;
  return pad_ext_call(g_ext_names[id], a.size() > 1 ? N(1) : 0, a.size() > 2 ? N(2) : 0, a.size() > 3 ? N(3) : 0);
}
FN(external_free) { return 0; }

// ------------------------------------------------------------------ paths and mp_grid
FN(path_add) { G->paths.emplace_back(); return (double)G->paths.size() - 1; }
FN(path_exists) { int p = (int)N(0); return p >= 0 && p < (int)G->paths.size() ? 1 : 0; }
FN(path_set_closed) { int p = (int)N(0); if (p >= 0 && p < (int)G->paths.size()) G->paths[p].closed = T(a[1]); return 0; }
FN(path_set_kind) { int p = (int)N(0); if (p >= 0 && p < (int)G->paths.size()) G->paths[p].kind = (int)N(1); return 0; }
FN(path_set_precision) { int p = (int)N(0); if (p >= 0 && p < (int)G->paths.size()) G->paths[p].prec = (int)N(1); return 0; }
FN(path_get_length) { int p = (int)N(0); return p >= 0 && p < (int)G->paths.size() ? G->paths[p].length : 0.0; }
static void path_pos(const PathDef& p, double t, double& x, double& y) {
  if (p.pts.empty()) { x = y = 0; return; }
  if (p.pts.size() == 1 || p.length <= 0) { x = p.pts[0].x; y = p.pts[0].y; return; }
  double target = t * p.length, acc = 0;
  size_t n = p.pts.size();
  size_t segs = p.closed ? n : n - 1;
  for (size_t i = 0; i < segs; i++) {
    const PathPt& A = p.pts[i];
    const PathPt& B = p.pts[(i + 1) % n];
    double l = std::hypot(B.x - A.x, B.y - A.y);
    if (acc + l >= target || i == segs - 1) {
      double f = l > 0 ? std::max(0.0, std::min(1.0, (target - acc) / l)) : 0;
      x = A.x + (B.x - A.x) * f; y = A.y + (B.y - A.y) * f;
      return;
    }
    acc += l;
  }
}
static void path_recalc(PathDef& p) {
  p.length = 0;
  size_t n = p.pts.size();
  size_t segs = p.closed ? n : (n ? n - 1 : 0);
  for (size_t i = 0; i < segs; i++) {
    const PathPt& A = p.pts[i]; const PathPt& B = p.pts[(i + 1) % n];
    p.length += std::hypot(B.x - A.x, B.y - A.y);
  }
}
FN(path_start) {
  int p = (int)N(0);
  if (p < 0 || p >= (int)G->paths.size()) return 0;
  path_recalc(G->paths[p]);
  self->path_index = p;
  self->path_speed = N(1);
  self->path_endaction = (int)N(2);
  self->path_position = self->path_speed < 0 ? 1.0 : 0.0;
  self->path_positionprevious = self->path_position;
  double x, y;
  path_pos(G->paths[p], self->path_position, x, y);
  self->path_ox = self->path_oy = 0;
  if (!T(a[3]) && !G->paths[p].pts.empty()) {
    double sx, sy; path_pos(G->paths[p], self->path_position, sx, sy);
    self->path_ox = self->x - sx; self->path_oy = self->y - sy;
  }
  x += self->path_ox; y += self->path_oy;
  self->x = x; self->y = y;
  return 0;
}
FN(path_end) { self->path_index = -1; self->path_speed = 0; return 0; }
void advance_paths(Inst* i) {
  if (i->path_index < 0 || i->path_index >= (int)G->paths.size()) return;
  PathDef& p = G->paths[i->path_index];
  if (p.length <= 0 || p.pts.size() < 2) { i->path_index = -1; return; }
  double step = i->path_speed / p.length;
  double pos = i->path_position + step;
  bool ended = false;
  if (pos >= 1.0 || pos < 0.0) {
    switch (i->path_endaction) {
      case 0: ended = true; pos = std::max(0.0, std::min(1.0, pos)); break;
      case 1: case 2: pos = pos - std::floor(pos); break;
      case 3: i->path_speed = -i->path_speed; pos = std::max(0.0, std::min(1.0, pos)); break;
    }
  }
  i->path_positionprevious = i->path_position;
  i->path_position = pos;
  double x, y;
  path_pos(p, pos, x, y);
  x += i->path_ox; y += i->path_oy;
  i->direction = std::fmod(std::atan2(-(y - i->y), x - i->x) / DEG + 360, 360);
  i->x = x; i->y = y;
  if (ended) {
    i->path_index = -1;
    run_event(i, 7, 8);
  }
}
FN(mp_grid_create) {
  MpGrid g;
  g.left = N(0); g.top = N(1); g.hcells = (int)N(2); g.vcells = (int)N(3); g.cw = N(4); g.ch = N(5);
  g.blocked.assign((size_t)g.hcells * g.vcells, 0);
  G->grids.push_back(g);
  return (double)G->grids.size() - 1;
}
FN(mp_grid_add_instances) {
  int gi = (int)N(0);
  if (gi < 0 || gi >= (int)G->grids.size()) return 0;
  MpGrid& g = G->grids[gi];
  for (Inst* i : WithList(a[1], self, other)) {
    double l, t, r, b;
    if (!bbox_of(i, i->x, i->y, l, t, r, b)) continue;
    int x0 = (int)std::floor((l - g.left) / g.cw), x1 = (int)std::floor((r - 1e-6 - g.left) / g.cw);
    int y0 = (int)std::floor((t - g.top) / g.ch), y1 = (int)std::floor((b - 1e-6 - g.top) / g.ch);
    for (int cy = std::max(0, y0); cy <= std::min(g.vcells - 1, y1); cy++)
      for (int cx = std::max(0, x0); cx <= std::min(g.hcells - 1, x1); cx++) g.blocked[(size_t)cy * g.hcells + cx] = 1;
  }
  return 0;
}
FN(mp_grid_path) {
  // (grid, path, xstart, ystart, xgoal, ygoal, allowdiag)
  int gi = (int)N(0), pi = (int)N(1);
  if (gi < 0 || gi >= (int)G->grids.size() || pi < 0 || pi >= (int)G->paths.size()) return 0;
  MpGrid& g = G->grids[gi];
  PathDef& path = G->paths[pi];
  path.pts.clear();
  auto cell = [&](double x, double y, int& cx, int& cy) {
    cx = (int)std::floor((x - g.left) / g.cw); cy = (int)std::floor((y - g.top) / g.ch);
    return cx >= 0 && cy >= 0 && cx < g.hcells && cy < g.vcells;
  };
  int sx, sy, ex, ey;
  if (!cell(N(2), N(3), sx, sy) || !cell(N(4), N(5), ex, ey)) return 0;
  if (g.blocked[(size_t)ey * g.hcells + ex]) return 0;
  bool diag = T(a[6]);
  int W = g.hcells, H = g.vcells;
  std::vector<float> dist((size_t)W * H, 1e30f);
  std::vector<int> prev((size_t)W * H, -1);
  typedef std::pair<float, int> QI;
  std::priority_queue<QI, std::vector<QI>, std::greater<QI>> pq;
  auto h = [&](int x, int y) { float dx = (float)(x - ex), dy = (float)(y - ey); return std::sqrt(dx * dx + dy * dy); };
  dist[(size_t)sy * W + sx] = 0;
  pq.push({h(sx, sy), sy * W + sx});
  bool found = false;
  while (!pq.empty()) {
    auto [f, cur] = pq.top(); pq.pop();
    int cx = cur % W, cy = cur / W;
    if (cx == ex && cy == ey) { found = true; break; }
    if (f - h(cx, cy) > dist[cur] + 1e-4f) continue;
    for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
      if (!dx && !dy) continue;
      if (!diag && dx && dy) continue;
      int nx = cx + dx, ny = cy + dy;
      if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
      if (g.blocked[(size_t)ny * W + nx]) continue;
      if (dx && dy && (g.blocked[(size_t)cy * W + nx] || g.blocked[(size_t)ny * W + cx])) continue;
      float nd = dist[cur] + (dx && dy ? 1.41421356f : 1.0f);
      if (nd < dist[(size_t)ny * W + nx]) {
        dist[(size_t)ny * W + nx] = nd;
        prev[(size_t)ny * W + nx] = cur;
        pq.push({nd + h(nx, ny), ny * W + nx});
      }
    }
  }
  if (!found) return 0;
  std::vector<int> chain;
  for (int c = ey * W + ex; c != -1; c = prev[c]) chain.push_back(c);
  std::reverse(chain.begin(), chain.end());
  for (size_t k = 0; k < chain.size(); k++) {
    int c = chain[k];
    double px = g.left + (c % W + 0.5) * g.cw, py = g.top + (c / W + 0.5) * g.ch;
    if (k == 0) { px = N(2); py = N(3); }
    if (k + 1 == chain.size()) { px = N(4); py = N(5); }
    path.pts.push_back({px, py, 100});
  }
  path.closed = false;
  path_recalc(path);
  return 1;
}

// ------------------------------------------------------------------ save / load
struct W {
  std::vector<uint8_t> b;
  void raw(const void* p, size_t n) { const uint8_t* q = (const uint8_t*)p; b.insert(b.end(), q, q + n); }
  void i32(int v) { raw(&v, 4); }
  void f64(double v) { raw(&v, 8); }
  void str(const std::string& s) { i32((int)s.size()); raw(s.data(), s.size()); }
};
struct R {
  const std::vector<uint8_t>& b; size_t p = 0; bool ok = true;
  R(const std::vector<uint8_t>& v) : b(v) {}
  void raw(void* o, size_t n) { if (p + n > b.size()) { ok = false; memset(o, 0, n); return; } memcpy(o, &b[p], n); p += n; }
  int i32() { int v; raw(&v, 4); return v; }
  double f64() { double v; raw(&v, 8); return v; }
  std::string str() { int n = i32(); if (n < 0 || p + n > b.size()) { ok = false; return ""; } std::string s((const char*)&b[p], n); p += n; return s; }
};
static void ser(W& w, const Value& v) {
  w.i32(v.t);
  if (v.t == Value::R) w.f64(v.d);
  else if (v.t == Value::S) w.str(v.s);
  else {
    if (!v.a) { w.i32(0); w.i32(0); return; }
    w.i32((int)v.a->v.size());
    for (auto& e : v.a->v) ser(w, e);
    w.i32((int)v.a->m2.size());
    for (auto& kv : v.a->m2) { w.raw(&kv.first, 8); ser(w, kv.second); }
  }
}
static Value de(R& r) {
  int t = r.i32();
  Value v;
  if (t == Value::R) v = Value(r.f64());
  else if (t == Value::S) v = Value(r.str());
  else {
    v.t = Value::A; v.a = std::make_shared<Arr>();
    int n = r.i32();
    if (n < 0 || n > 10000000) { r.ok = false; return v; }
    v.a->v.resize(n);
    for (int i = 0; i < n; i++) v.a->v[i] = de(r);
    int m = r.i32();
    for (int i = 0; i < m && r.ok; i++) { long long k; r.raw(&k, 8); v.a->m2[k] = de(r); v.a->two = true; }
  }
  return v;
}
void do_game_save(const std::string& file) {
  W w;
  w.i32(0x484D5356);
  w.i32(G->room);
  w.i32(G->nextid);
  w.i32((int)G->gv.size());
  for (auto& v : G->gv) ser(w, v);
  for (int a = 0; a < GB__COUNT; a++) for (int b = 0; b < 8; b++) ser(w, G->gb[a][b]);
  std::vector<Inst*> all;
  for (Inst* i : G->insts) if (i->alive) all.push_back(i);
  w.i32((int)all.size());
  for (Inst* i : all) {
    w.i32(i->obj); w.i32(i->id);
    double f[] = {i->x, i->y, i->xprevious, i->yprevious, i->xstart, i->ystart, i->hspeed, i->vspeed, i->speed, i->direction,
                  i->friction, i->gravity, i->gravity_direction, i->image_index, i->image_speed, i->image_xscale,
                  i->image_yscale, i->image_angle, i->image_alpha, i->path_position, i->path_speed, i->path_scale};
    for (double d : f) w.f64(d);
    w.i32(i->sprite_index); w.i32(i->mask_index); w.i32((int)i->image_blend);
    w.i32(i->visible); w.i32(i->solid); w.i32(i->persistent); w.i32(i->depth); w.i32(i->path_index); w.i32(i->path_endaction);
    for (int k = 0; k < 12; k++) w.i32(i->alarm[k]);
    w.i32((int)i->vars.size());
    for (auto& kv : i->vars) { w.i32(kv.first); ser(w, kv.second); }
  }
  w.i32((int)G->tiles.size());
  for (auto& t : G->tiles) w.i32(t.id), w.i32(t.visible);
  std::string path = file;
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fwrite(w.b.data(), 1, w.b.size(), f);
  fclose(f);
}
bool do_game_load(const std::string& file) {
  std::ifstream f(file, std::ios::binary);
  if (!f) return false;
  std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
  R r(buf);
  if (r.i32() != 0x484D5356) return false;
  int room = r.i32();
  int nextid = r.i32();
  if (room < 0 || room >= RM__COUNT) return false;
  int ngv = r.i32();
  std::vector<Value> gv(ngv > 0 ? ngv : 0);
  for (auto& v : gv) v = de(r);
  static Value gbtmp[GB__COUNT][8];
  for (int a = 0; a < GB__COUNT; a++) for (int b = 0; b < 8; b++) gbtmp[a][b] = de(r);
  if (!r.ok) return false;
  kill_all_instances();
  setup_room_state(room);
  G->gv = gv;
  for (int a = 0; a < GB__COUNT; a++) for (int b = 0; b < 8; b++) G->gb[a][b] = gbtmp[a][b];
  int n = r.i32();
  for (int k = 0; k < n && r.ok; k++) {
    Inst* i = new Inst();
    i->obj = r.i32(); i->id = r.i32();
    double* fp[] = {&i->x, &i->y, &i->xprevious, &i->yprevious, &i->xstart, &i->ystart, &i->hspeed, &i->vspeed, &i->speed,
                    &i->direction, &i->friction, &i->gravity, &i->gravity_direction, &i->image_index, &i->image_speed,
                    &i->image_xscale, &i->image_yscale, &i->image_angle, &i->image_alpha, &i->path_position,
                    &i->path_speed, &i->path_scale};
    for (double* d : fp) *d = r.f64();
    i->sprite_index = r.i32(); i->mask_index = r.i32(); i->image_blend = (uint32_t)r.i32();
    i->visible = r.i32(); i->solid = r.i32(); i->persistent = r.i32(); i->depth = r.i32(); i->path_index = r.i32();
    i->path_endaction = r.i32();
    for (int a = 0; a < 12; a++) i->alarm[a] = r.i32();
    int nv = r.i32();
    for (int a = 0; a < nv && r.ok; a++) { int id = r.i32(); i->var(id) = de(r); }
    i->created = true;
    if (i->obj < 0 || i->obj >= g_obj_count) { delete i; continue; }
    register_instance(i);
  }
  int nt = r.i32();
  for (int k = 0; k < nt && r.ok; k++) {
    int id = r.i32(), vis = r.i32();
    for (auto& t : G->tiles) if (t.id == id) t.visible = vis != 0;
  }
  G->nextid = std::max(G->nextid, nextid);
  return true;
}
static std::string save_path(const Value& v) { return host_path(v); }
FN(game_save) { do_game_save(save_path(a[0])); return 0; }
FN(game_load) {
  if (!do_game_load(save_path(a[0]))) fprintf(stderr, "game_load failed: %s\n", save_path(a[0]).c_str());
  return 0;
}
