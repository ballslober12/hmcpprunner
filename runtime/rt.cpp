#include "game.h"
#include "pad.h"
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

Game* G = nullptr;

// ------------------------------------------------------------------ small math helpers
static const double DEG = M_PI / 180.0;
double lengthdir_x_(double l, double d) { return l * std::cos(d * DEG); }
double lengthdir_y_(double l, double d) { return -l * std::sin(d * DEG); }

std::string gm_path(const std::string& p) {
  std::string r = p;
  for (auto& c : r) if (c == '\\') c = '/';
  // big read-only audio files are not copied into the work dir: read them from the assets' datafiles
  if (r.size() > 4 && G && !G->workdir.empty() && r.compare(0, G->workdir.size(), G->workdir) == 0) {
    std::string ext = r.substr(r.size() - 4);
    for (auto& c : ext) c = (char)tolower((unsigned char)c);
    if (ext == ".ogg" || ext == ".wav") {
      FILE* f = fopen(r.c_str(), "rb");
      if (f) fclose(f);
      else {
        std::string alt = G->root + "/datafiles" + r.substr(G->workdir.size());
        if ((f = fopen(alt.c_str(), "rb"))) { fclose(f); return alt; }
      }
    }
  }
  return r;
}

// ------------------------------------------------------------------ data loading
static Room* cur_room_def = nullptr;

void load_game_data(const std::string& datfile) {
  std::ifstream f(datfile);
  if (!f) { fprintf(stderr, "cannot open %s\n", datfile.c_str()); exit(1); }
  for (int i = 0; i < g_obj_count; i++) G->obj_by_name[g_objs[i].name] = i;
  for (int i = 0; i < SPR__COUNT; i++) G->spr_by_name[g_spr_names[i]] = i;
  for (int i = 0; i < BG__COUNT; i++) G->bg_by_name[g_bg_names[i]] = i;
  for (int i = 0; i < SND__COUNT; i++) G->snd_by_name[g_snd_names[i]] = i;
  for (int i = 0; i < FNT__COUNT; i++) G->fnt_by_name[g_fnt_names[i]] = i;
  for (int i = 0; i < RM__COUNT; i++) G->rm_by_name[g_rm_names[i]] = i;
  G->spr.resize(SPR__COUNT);
  G->bg.resize(BG__COUNT);
  G->fnt.resize(FNT__COUNT);
  G->snd.resize(SND__COUNT);
  G->rooms.resize(RM__COUNT);
  std::string line;
  Room* cr = nullptr;
  int vi = 0, bi = 0;
  while (std::getline(f, line)) {
    if (line.empty()) continue;
    std::istringstream is(line);
    std::string tag;
    is >> tag;
    if (tag == "SPR") {
      std::string name; is >> name;
      Sprite& s = G->spr[G->spr_by_name[name]];
      int nf;
      s.name = name;
      is >> s.xorig >> s.yorig >> s.bbl >> s.bbr >> s.bbt >> s.bbb >> s.w >> s.h >> s.colkind >> s.bboxmode >> nf;
      for (int i = 0; i < nf; i++) { std::string p; is >> p; s.frames.push_back(p); }
    } else if (tag == "BG") {
      std::string name; is >> name;
      Background& b = G->bg[G->bg_by_name[name]];
      b.name = name;
      is >> b.w >> b.h >> b.istile >> b.tw >> b.th >> b.path;
    } else if (tag == "FONT") {
      std::string name; is >> name;
      Font& ft = G->fnt[G->fnt_by_name[name]];
      ft.name = name;
      int n;
      is >> ft.size >> ft.bold >> ft.italic >> n;
      for (int i = 0; i < n; i++) {
        int ch; Glyph g;
        is >> ch >> g.x >> g.y >> g.w >> g.h >> g.shift >> g.offset;
        ft.glyphs[ch] = g;
        ft.lineh = std::max(ft.lineh, g.h);
      }
    } else if (tag == "SND") {
      std::string name, file; is >> name;
      Sound& s = G->snd[G->snd_by_name[name]];
      s.name = name;
      std::string rest; std::getline(is, rest);
      std::istringstream r2(rest);
      r2 >> file >> s.volume >> s.kind;
      s.file = file;
    } else if (tag == "ROOM") {
      std::string name; is >> name;
      int idx = G->rm_by_name[name];
      cr = &G->rooms[idx];
      cr->name = name;
      int ri;
      is >> cr->w >> cr->h >> cr->speed >> cr->persistent >> cr->colour >> cr->showcolour >> cr->enableViews >> ri;
      vi = bi = 0;
    } else if (tag == "VIEW" && cr) {
      RoomView& v = cr->views[vi++ & 7];
      std::string o;
      is >> v.visible >> v.xview >> v.yview >> v.wview >> v.hview >> v.xport >> v.yport >> v.wport >> v.hport >>
          v.hborder >> v.vborder >> v.hspeed >> v.vspeed >> o;
      v.obj = (o == "-") ? -1 : (G->obj_by_name.count(o) ? G->obj_by_name[o] : -1);
    } else if (tag == "RBG" && cr) {
      RoomBg& b = cr->bgs[bi++ & 7];
      std::string n;
      is >> b.visible >> b.fore >> n >> b.x >> b.y >> b.htiled >> b.vtiled >> b.hspeed >> b.vspeed >> b.stretch;
      b.bg = (n == "-" || !G->bg_by_name.count(n)) ? -1 : G->bg_by_name[n];
    } else if (tag == "INST" && cr) {
      RoomInst ri; std::string o;
      is >> o >> ri.x >> ri.y >> ri.sx >> ri.sy >> ri.rot >> ri.col >> ri.code;
      auto it = G->obj_by_name.find(o);
      if (it == G->obj_by_name.end()) continue;
      ri.obj = it->second;
      cr->insts.push_back(ri);
    } else if (tag == "TILE" && cr) {
      RoomTile t; std::string n;
      is >> n >> t.x >> t.y >> t.w >> t.h >> t.xo >> t.yo >> t.depth >> t.sx >> t.sy >> t.col >> t.id;
      auto it = G->bg_by_name.find(n);
      if (it == G->bg_by_name.end()) continue;
      t.bg = it->second;
      cr->tiles.push_back(t);
    } else if (tag == "ENDROOM") {
      cr = nullptr;
    }
  }
  // descendant tables
  G->desc.assign(g_obj_count, {});
  for (int o = 0; o < g_obj_count; o++) {
    for (int p = o; p >= 0; p = g_objs[p].parent) G->desc[p].push_back(o);
  }
  G->byobj.assign(g_obj_count, {});
  G->evcache.assign(g_obj_count, {});
}

// ------------------------------------------------------------------ built-in variable access
static inline Sprite* spr_of(int s) { return (s >= 0 && s < (int)G->spr.size()) ? &G->spr[s] : nullptr; }

bool bbox_of(const Inst* i, double x, double y, double& l, double& t, double& r, double& b) {
  int s = i->mask_index >= 0 ? i->mask_index : i->sprite_index;
  Sprite* sp = spr_of(s);
  if (!sp) { l = r = x; t = b = y; return false; }
  double xs = i->image_xscale, ys = i->image_yscale;
  double a = x + (sp->bbl - sp->xorig) * xs, c = x + (sp->bbr + 1 - sp->xorig) * xs;
  double d = y + (sp->bbt - sp->yorig) * ys, e = y + (sp->bbb + 1 - sp->yorig) * ys;
  l = std::min(a, c); r = std::max(a, c); t = std::min(d, e); b = std::max(d, e);
  return true;
}
bool inst_meets(const Inst* a, double ax, double ay, const Inst* b) {
  double l1, t1, r1, b1, l2, t2, r2, b2;
  if (!bbox_of(a, ax, ay, l1, t1, r1, b1)) return false;
  if (!bbox_of(b, b->x, b->y, l2, t2, r2, b2)) return false;
  return l1 < r2 && l2 < r1 && t1 < b2 && t2 < b1;
}

void Inst::set_hv() {
  speed = std::sqrt(hspeed * hspeed + vspeed * vspeed);
  if (speed > 1e-12) {
    direction = std::atan2(-vspeed, hspeed) / DEG;
    if (direction < 0) direction += 360;
  }
}
void Inst::set_sd() {
  hspeed = speed * std::cos(direction * DEG);
  vspeed = -speed * std::sin(direction * DEG);
  if (std::fabs(hspeed) < 1e-12) hspeed = 0;
  if (std::fabs(vspeed) < 1e-12) vspeed = 0;
}

Value Inst::get(int bi, int idx) const {
  switch (bi) {
    case BI_x: return x;
    case BI_y: return y;
    case BI_xprevious: return xprevious;
    case BI_yprevious: return yprevious;
    case BI_xstart: return xstart;
    case BI_ystart: return ystart;
    case BI_hspeed: return hspeed;
    case BI_vspeed: return vspeed;
    case BI_speed: return speed;
    case BI_direction: return direction;
    case BI_friction: return friction;
    case BI_gravity: return gravity;
    case BI_gravity_direction: return gravity_direction;
    case BI_sprite_index: return sprite_index;
    case BI_image_index: return image_index;
    case BI_image_speed: return image_speed;
    case BI_image_xscale: return image_xscale;
    case BI_image_yscale: return image_yscale;
    case BI_image_angle: return image_angle;
    case BI_image_alpha: return image_alpha;
    case BI_image_blend: return (double)image_blend;
    case BI_visible: return visible ? 1.0 : 0.0;
    case BI_solid: return solid ? 1.0 : 0.0;
    case BI_depth: return depth;
    case BI_persistent: return persistent ? 1.0 : 0.0;
    case BI_id: return id;
    case BI_object_index: return obj;
    case BI_mask_index: return mask_index;
    case BI_bbox_left: { double l, t, r, b; bbox_of(this, x, y, l, t, r, b); return l; }
    case BI_bbox_right: { double l, t, r, b; bbox_of(this, x, y, l, t, r, b); return r - 1; }
    case BI_bbox_top: { double l, t, r, b; bbox_of(this, x, y, l, t, r, b); return t; }
    case BI_bbox_bottom: { double l, t, r, b; bbox_of(this, x, y, l, t, r, b); return b - 1; }
    case BI_sprite_width: { Sprite* s = spr_of(sprite_index); return s ? s->w * image_xscale : 0.0; }
    case BI_sprite_height: { Sprite* s = spr_of(sprite_index); return s ? s->h * image_yscale : 0.0; }
    case BI_sprite_xoffset: { Sprite* s = spr_of(sprite_index); return s ? s->xorig * image_xscale : 0.0; }
    case BI_sprite_yoffset: { Sprite* s = spr_of(sprite_index); return s ? s->yorig * image_yscale : 0.0; }
    case BI_image_number: { Sprite* s = spr_of(sprite_index); return s ? (double)s->frames.size() : 0.0; }
    case BI_alarm: return (idx >= 0 && idx < 12) ? alarm[idx] : -1;
    case BI_path_index: return path_index;
    case BI_path_position: return path_position;
    case BI_path_positionprevious: return path_positionprevious;
    case BI_path_speed: return path_speed;
    case BI_path_scale: return path_scale;
    case BI_path_orientation: return path_orientation;
    case BI_path_endaction: return path_endaction;
    default: return 0.0;
  }
}

void Inst::set(int bi, int idx, const Value& v) {
  double n = Num(v);
  switch (bi) {
    case BI_x: x = n; break;
    case BI_y: y = n; break;
    case BI_xprevious: xprevious = n; break;
    case BI_yprevious: yprevious = n; break;
    case BI_xstart: xstart = n; break;
    case BI_ystart: ystart = n; break;
    case BI_hspeed: hspeed = n; set_hv(); break;
    case BI_vspeed: vspeed = n; set_hv(); break;
    case BI_speed: speed = n; set_sd(); break;
    case BI_direction: direction = std::fmod(n, 360.0); if (direction < 0) direction += 360; set_sd(); break;
    case BI_friction: friction = n; break;
    case BI_gravity: gravity = n; break;
    case BI_gravity_direction: gravity_direction = n; break;
    case BI_sprite_index: sprite_index = (int)n; break;
    case BI_image_index: image_index = n; break;
    case BI_image_speed: image_speed = n; break;
    case BI_image_xscale: image_xscale = n; break;
    case BI_image_yscale: image_yscale = n; break;
    case BI_image_angle: image_angle = n; break;
    case BI_image_alpha: image_alpha = n; break;
    case BI_image_blend: image_blend = (uint32_t)(long long)n; break;
    case BI_visible: visible = n >= 0.5; break;
    case BI_solid: solid = n >= 0.5; break;
    case BI_depth: depth = (int)n; break;
    case BI_persistent: persistent = n >= 0.5; break;
    case BI_mask_index: mask_index = (int)n; break;
    case BI_alarm: if (idx >= 0 && idx < 12) alarm[idx] = (int)n; break;
    case BI_path_index: path_index = (int)n; break;
    case BI_path_position: path_position = n; break;
    case BI_path_speed: path_speed = n; break;
    case BI_path_scale: path_scale = n; break;
    case BI_path_orientation: path_orientation = n; break;
    case BI_path_endaction: path_endaction = (int)n; break;
    default: break;  // read-only
  }
}

BRef::operator Value() const { return i ? i->get(id, idx) : GBget(id, idx); }
BRef& BRef::operator=(const Value& v) {
  if (i) i->set(id, idx, v); else GBset(id, idx, v);
  return *this;
}

Value GBget(int id, int idx) {
  switch (id) {
    case GB_current_time: return (double)SDL_GetTicks();
    case GB_fps: return G->fps;
    case GB_instance_count: return (double)G->insts.size();
    case GB_room_width: return G->room >= 0 ? G->rooms[G->room].w : 0;
    case GB_room_height: return G->room >= 0 ? G->rooms[G->room].h : 0;
    case GB_room: return G->room;
    case GB_mouse_x: return G->mouse_wx;
    case GB_mouse_y: return G->mouse_wy;
    default: break;
  }
  if (idx < 0 || idx > 7) idx = 0;
  return G->gb[id][idx];
}
void GBset(int id, int idx, const Value& v) {
  if (id == GB_room) { room_goto_index((int)Num(v)); return; }
  if (id == GB_room_speed) { G->gb[id][0] = v; return; }
  if (idx < 0 || idx > 7) idx = 0;
  G->gb[id][idx] = v;
}
Value& GV(int id) {
  if ((size_t)id >= G->gv.size()) G->gv.resize(id + 1);
  return G->gv[id];
}

// ------------------------------------------------------------------ instance lookup
Inst* FirstInst(int obj) {
  if (obj < 0 || obj >= g_obj_count) return &G->dummy;
  Inst* best = nullptr;
  for (int o : G->desc[obj])
    for (Inst* i : G->byobj[o])
      if (i->alive && (!best || i->id < best->id)) best = i;
  return best ? best : &G->dummy;
}
Inst* ToInst(const Value& v, Inst* self, Inst* other) {
  double d = Num(v);
  int n = (int)d;
  if (n == -1) return self ? self : &G->dummy;
  if (n == -2) return other ? other : &G->dummy;
  if (n >= 100000) {
    auto it = G->byid.find(n);
    if (it != G->byid.end() && it->second->alive) return it->second;
    return &G->dummy;
  }
  if (n >= 0) return FirstInst(n);
  return &G->dummy;
}
std::vector<Inst*> WithList(const Value& v, Inst* self, Inst* other) {
  std::vector<Inst*> out;
  int n = (int)Num(v);
  if (n == -1) { out.push_back(self); return out; }
  if (n == -2) { if (other) out.push_back(other); return out; }
  if (n == -3) {
    for (Inst* i : G->insts) if (i->alive) out.push_back(i);
    return out;
  }
  if (n >= 100000) {
    auto it = G->byid.find(n);
    if (it != G->byid.end() && it->second->alive) out.push_back(it->second);
    return out;
  }
  if (n >= 0 && n < g_obj_count) {
    for (int o : G->desc[n])
      for (Inst* i : G->byobj[o]) if (i->alive) out.push_back(i);
    std::sort(out.begin(), out.end(), [](Inst* a, Inst* b) { return a->id < b->id; });
  }
  return out;
}
bool inst_is_a(int obj, int anc) {
  for (int p = obj; p >= 0; p = g_objs[p].parent) if (p == anc) return true;
  return false;
}

// ------------------------------------------------------------------ events
static long long ev_key(int type, int num, int other) {
  return ((long long)type << 40) | ((long long)(num & 0xFFFFF) << 20) | (long long)(other + 1);
}
EvFn find_event(int obj, int type, int num, int other) {
  static std::unordered_map<long long, EvFn> cache[1];
  (void)cache;
  for (int o = obj; o >= 0; o = g_objs[o].parent)
    for (const EvDef& e : g_objs[o].events)
      if (e.type == type && e.num == num && (type != 4 || e.other == other)) return e.fn;
  return nullptr;
}
bool has_event(int obj, int type, int num, int other) { return find_event(obj, type, num, other) != nullptr; }

void run_event(Inst* i, int type, int num, int otherobj, Inst* other) {
  EvFn fn = find_event(i->obj, type, num, otherobj);
  if (fn) fn(i, other ? other : &G->dummy);
}
void EventInherited(Inst* self, Inst* other, int obj, int type, int num) {
  if (obj < 0) return;
  int p = g_objs[obj].parent;
  if (p < 0) return;
  EvFn fn = find_event(p, type, num, -1);
  if (fn) fn(self, other);
}

// ------------------------------------------------------------------ instance lifecycle
Inst* alloc_instance(double x, double y, int obj) {
  Inst* i = new Inst();
  const ObjDef& d = g_objs[obj];
  i->id = G->nextid++;
  i->obj = obj;
  i->x = i->xprevious = i->xstart = x;
  i->y = i->yprevious = i->ystart = y;
  i->sprite_index = d.sprite;
  i->mask_index = d.mask;
  i->visible = d.visible;
  i->solid = d.solid;
  i->depth = d.depth;
  i->persistent = d.persistent;
  G->insts.push_back(i);
  G->byid[i->id] = i;
  G->byobj[obj].push_back(i);
  return i;
}
Inst* instance_create_obj(double x, double y, int obj) {
  if (obj < 0 || obj >= g_obj_count) return &G->dummy;
  Inst* i = alloc_instance(x, y, obj);
  run_event(i, 0, 0);
  i->created = true;
  return i;
}
void instance_destroy_inst(Inst* i) {
  if (!i->alive || i == &G->dummy) return;
  run_event(i, 1, 0);
  i->alive = false;
  G->trash.push_back(i);
}
static void flush_trash() {
  if (G->trash.empty()) return;
  for (Inst* i : G->trash) {
    auto& v = G->byobj[i->obj];
    v.erase(std::remove(v.begin(), v.end(), i), v.end());
    G->byid.erase(i->id);
  }
  std::vector<Inst*> keep;
  keep.reserve(G->insts.size());
  for (Inst* i : G->insts) if (i->alive) keep.push_back(i);
  G->insts.swap(keep);
  for (Inst* i : G->trash) delete i;
  G->trash.clear();
}

// ------------------------------------------------------------------ rooms
void room_goto_index(int r) {
  if (r < 0 || r >= RM__COUNT) return;
  G->next_room = r;
}

void setup_room_state(int r) {
  Room& rm = G->rooms[r];
  G->room = r;
  G->next_room = -1;
  // tiles
  G->tiles.clear();
  for (auto& t : rm.tiles)
    G->tiles.push_back({t.bg, t.x, t.y, t.w, t.h, t.xo, t.yo, t.depth, t.sx, t.sy, t.col, t.id, true});
  // globals describing room
  G->gb[GB_room_speed][0] = (double)rm.speed;
  G->gb[GB_view_enabled][0] = (double)(rm.enableViews ? 1 : 0);
  G->gb[GB_background_color][0] = (double)rm.colour;
  G->gb[GB_background_colour][0] = (double)rm.colour;
  G->gb[GB_background_showcolor][0] = (double)(rm.showcolour ? 1 : 0);
  G->gb[GB_background_showcolour][0] = (double)(rm.showcolour ? 1 : 0);
  for (int v = 0; v < 8; v++) {
    const RoomView& rv = rm.views[v];
    G->gb[GB_view_visible][v] = (double)(rv.visible ? 1 : 0);
    G->gb[GB_view_xview][v] = (double)rv.xview;
    G->gb[GB_view_yview][v] = (double)rv.yview;
    G->gb[GB_view_wview][v] = (double)rv.wview;
    G->gb[GB_view_hview][v] = (double)rv.hview;
    G->gb[GB_view_xport][v] = (double)rv.xport;
    G->gb[GB_view_yport][v] = (double)rv.yport;
    G->gb[GB_view_wport][v] = (double)rv.wport;
    G->gb[GB_view_hport][v] = (double)rv.hport;
    G->gb[GB_view_hborder][v] = (double)rv.hborder;
    G->gb[GB_view_vborder][v] = (double)rv.vborder;
    G->gb[GB_view_hspeed][v] = (double)rv.hspeed;
    G->gb[GB_view_vspeed][v] = (double)rv.vspeed;
    G->gb[GB_view_object][v] = (double)(rv.obj >= 0 ? rv.obj : -4);
    G->gb[GB_view_angle][v] = 0.0;
    const RoomBg& bg = rm.bgs[v];
    G->gb[GB_background_visible][v] = (double)(bg.visible ? 1 : 0);
    G->gb[GB_background_foreground][v] = (double)(bg.fore ? 1 : 0);
    G->gb[GB_background_index][v] = (double)bg.bg;
    G->gb[GB_background_x][v] = bg.x;
    G->gb[GB_background_y][v] = bg.y;
    G->gb[GB_background_htiled][v] = (double)(bg.htiled ? 1 : 0);
    G->gb[GB_background_vtiled][v] = (double)(bg.vtiled ? 1 : 0);
    G->gb[GB_background_hspeed][v] = bg.hspeed;
    G->gb[GB_background_vspeed][v] = bg.vspeed;
    G->gb[GB_background_alpha][v] = 1.0;
    G->gb[GB_background_blend][v] = 16777215.0;
    G->gb[GB_background_xscale][v] = 1.0;
    G->gb[GB_background_yscale][v] = 1.0;
  }
}

void kill_all_instances() {
  for (Inst* i : G->insts) { i->alive = false; G->trash.push_back(i); }
  flush_trash();
}
Inst* register_instance(Inst* i) {
  G->insts.push_back(i);
  G->byid[i->id] = i;
  G->byobj[i->obj].push_back(i);
  return i;
}

static void enter_room(int r) {
  setup_room_state(r);
  Room& rm = G->rooms[r];
  // instances
  std::vector<Inst*> created;
  for (auto& ri : rm.insts) {
    Inst* i = alloc_instance(ri.x, ri.y, ri.obj);
    i->image_xscale = ri.sx;
    i->image_yscale = ri.sy;
    i->image_angle = ri.rot;
    i->image_blend = ri.col & 0xFFFFFF;
    created.push_back(i);
  }
  for (size_t k = 0; k < created.size(); k++) {
    Inst* i = created[k];
    if (!i->alive) continue;
    run_event(i, 0, 0);
    i->created = true;
    int c = rm.insts[k].code;
    if (c >= 0 && g_inst_code[c]) g_inst_code[c](i, &G->dummy);
  }
  if (g_room_code[r]) g_room_code[r](&G->dummy, &G->dummy);
  if (!G->game_started) {
    G->game_started = true;
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) if (i->alive) run_event(i, 7, 2);
  }
  std::vector<Inst*> snap = G->insts;
  for (Inst* i : snap) if (i->alive) run_event(i, 7, 4);
}

static void leave_room() {
  std::vector<Inst*> snap = G->insts;
  for (Inst* i : snap) if (i->alive) run_event(i, 7, 5);
  for (Inst* i : snap) if (i->alive && !i->persistent) { i->alive = false; G->trash.push_back(i); }
  flush_trash();
}

// ------------------------------------------------------------------ keyboard mapping
static SDL_Scancode vk_to_sc(int vk) {
  if (vk >= 'A' && vk <= 'Z') return (SDL_Scancode)(SDL_SCANCODE_A + (vk - 'A'));
  if (vk >= '1' && vk <= '9') return (SDL_Scancode)(SDL_SCANCODE_1 + (vk - '1'));
  if (vk == '0') return SDL_SCANCODE_0;
  if (vk >= 112 && vk <= 123) return (SDL_Scancode)(SDL_SCANCODE_F1 + (vk - 112));
  if (vk >= 96 && vk <= 105) return vk == 96 ? SDL_SCANCODE_KP_0 : (SDL_Scancode)(SDL_SCANCODE_KP_1 + (vk - 97));
  switch (vk) {
    case 8: return SDL_SCANCODE_BACKSPACE;
    case 9: return SDL_SCANCODE_TAB;
    case 13: return SDL_SCANCODE_RETURN;
    case 16: return SDL_SCANCODE_LSHIFT;
    case 17: return SDL_SCANCODE_LCTRL;
    case 18: return SDL_SCANCODE_LALT;
    case 19: return SDL_SCANCODE_PAUSE;
    case 27: return SDL_SCANCODE_ESCAPE;
    case 32: return SDL_SCANCODE_SPACE;
    case 33: return SDL_SCANCODE_PAGEUP;
    case 34: return SDL_SCANCODE_PAGEDOWN;
    case 35: return SDL_SCANCODE_END;
    case 36: return SDL_SCANCODE_HOME;
    case 37: return SDL_SCANCODE_LEFT;
    case 38: return SDL_SCANCODE_UP;
    case 39: return SDL_SCANCODE_RIGHT;
    case 40: return SDL_SCANCODE_DOWN;
    case 45: return SDL_SCANCODE_INSERT;
    case 46: return SDL_SCANCODE_DELETE;
    case 106: return SDL_SCANCODE_KP_MULTIPLY;
    case 107: return SDL_SCANCODE_KP_PLUS;
    case 109: return SDL_SCANCODE_KP_MINUS;
    case 110: return SDL_SCANCODE_KP_PERIOD;
    case 111: return SDL_SCANCODE_KP_DIVIDE;
    case 160: return SDL_SCANCODE_LSHIFT;
    case 161: return SDL_SCANCODE_RSHIFT;
    case 162: return SDL_SCANCODE_LCTRL;
    case 163: return SDL_SCANCODE_RCTRL;
    case 164: return SDL_SCANCODE_LALT;
    case 165: return SDL_SCANCODE_RALT;
    case 186: return SDL_SCANCODE_SEMICOLON;
    case 187: return SDL_SCANCODE_EQUALS;
    case 188: return SDL_SCANCODE_COMMA;
    case 189: return SDL_SCANCODE_MINUS;
    case 190: return SDL_SCANCODE_PERIOD;
    case 191: return SDL_SCANCODE_SLASH;
    case 192: return SDL_SCANCODE_GRAVE;
    case 219: return SDL_SCANCODE_LEFTBRACKET;
    case 220: return SDL_SCANCODE_BACKSLASH;
    case 221: return SDL_SCANCODE_RIGHTBRACKET;
    case 222: return SDL_SCANCODE_APOSTROPHE;
  }
  return SDL_SCANCODE_UNKNOWN;
}
static int sc_to_vk(SDL_Scancode sc) {
  for (int vk = 1; vk < 256; vk++) if (vk_to_sc(vk) == sc) return vk;
  return 0;
}
bool gm_key_down(int vk) {
  if (vk == 1) { for (int i = 8; i < 256; i++) if (G->keys[i]) return true; return false; }
  if (vk == 0) { for (int i = 8; i < 256; i++) if (G->keys[i]) return false; return true; }
  if (vk < 0 || vk > 255) return false;
  if (vk == 16) return G->keys[16] || G->keys[160] || G->keys[161];
  if (vk == 17) return G->keys[17] || G->keys[162] || G->keys[163];
  if (vk == 18) return G->keys[18] || G->keys[164] || G->keys[165];
  return G->keys[vk];
}
bool gm_key_pressed(int vk) {
  if (vk == 1) { for (int i = 8; i < 256; i++) if (G->keys_pressed[i]) return true; return false; }
  if (vk < 0 || vk > 255) return false;
  return G->keys_pressed[vk];
}
bool gm_key_released(int vk) {
  if (vk < 0 || vk > 255) return false;
  return G->keys_released[vk];
}

// ------------------------------------------------------------------ frame processing
static void poll_input() {
  memset(G->keys_pressed, 0, sizeof G->keys_pressed);
  memset(G->keys_released, 0, sizeof G->keys_released);
  memset(G->mbtn_pressed, 0, sizeof G->mbtn_pressed);
  memset(G->mbtn_released, 0, sizeof G->mbtn_released);
  G->wheel = 0;
  SDL_Event e;
  while (SDL_PollEvent(&e)) {
    switch (e.type) {
      case SDL_QUIT: G->quit = true; break;
      case SDL_KEYDOWN:
        if (!e.key.repeat) {
          int vk = sc_to_vk(e.key.keysym.scancode);
          if (vk) { G->keys[vk] = true; G->keys_pressed[vk] = true; G->last_key = vk; }
        }
        break;
      case SDL_KEYUP: {
        int vk = sc_to_vk(e.key.keysym.scancode);
        if (vk) { G->keys[vk] = false; G->keys_released[vk] = true; }
        break;
      }
      case SDL_MOUSEBUTTONDOWN: if (e.button.button < 8) { G->mbtn[e.button.button] = true; G->mbtn_pressed[e.button.button] = true; } break;
      case SDL_MOUSEBUTTONUP: if (e.button.button < 8) { G->mbtn[e.button.button] = false; G->mbtn_released[e.button.button] = true; } break;
      case SDL_MOUSEWHEEL: G->wheel += e.wheel.y; break;
      default: break;
    }
  }
}

// scripted input for automated tests: HM_KEYS="87:10-60,65:70-90" (vk:startframe-endframe), HM_MOUSE="1:20-21"
static void scripted_input() {
  static bool init = false;
  struct K { int vk, a, b; };
  static std::vector<K> ks, ms;
  if (!init) {
    init = true;
    auto parse = [](const char* e, std::vector<K>& out) {
      if (!e) return;
      std::string str = e;
      size_t p = 0;
      while (p < str.size()) {
        size_t q = str.find(',', p);
        if (q == std::string::npos) q = str.size();
        int vk, a, b;
        if (sscanf(str.substr(p, q - p).c_str(), "%d:%d-%d", &vk, &a, &b) == 3) out.push_back({vk, a, b});
        p = q + 1;
      }
    };
    parse(getenv("HM_KEYS"), ks);
    parse(getenv("HM_MOUSE"), ms);
  }
  for (auto& k : ks) {
    if ((int)G->frame_no == k.a) { G->keys[k.vk] = true; G->keys_pressed[k.vk] = true; G->last_key = k.vk; }
    if ((int)G->frame_no == k.b) { G->keys[k.vk] = false; G->keys_released[k.vk] = true; }
  }
  for (auto& m : ms) {
    if ((int)G->frame_no == m.a) { G->mbtn[m.vk] = true; G->mbtn_pressed[m.vk] = true; }
    if ((int)G->frame_no == m.b) { G->mbtn[m.vk] = false; G->mbtn_released[m.vk] = true; }
  }
}

static void update_mouse() {
  int mx, my;
  SDL_GetMouseState(&mx, &my);
  // window -> framebuffer coords (letterboxed)
  int ww, wh;
  SDL_GetWindowSize(G->win, &ww, &wh);
  double sc = std::min((double)ww / G->fbw, (double)wh / G->fbh);
  double ox = (ww - G->fbw * sc) / 2, oy = (wh - G->fbh * sc) / 2;
  double fx = (mx - ox) / sc, fy = (my - oy) / sc;
  G->gb[GB_mouse_button][0] = G->mbtn[1] ? 1.0 : G->mbtn[3] ? 2.0 : G->mbtn[2] ? 3.0 : 0.0;
  // framebuffer -> room using view 0 (or direct)
  Room& rm = G->rooms[G->room];
  double rx = fx, ry = fy;
  if (rm.enableViews) {
    for (int v = 0; v < 8; v++) {
      if (!Num(G->gb[GB_view_visible][v])) continue;
      double xp = Num(G->gb[GB_view_xport][v]), yp = Num(G->gb[GB_view_yport][v]);
      double wp = Num(G->gb[GB_view_wport][v]), hp = Num(G->gb[GB_view_hport][v]);
      double wv = Num(G->gb[GB_view_wview][v]), hv = Num(G->gb[GB_view_hview][v]);
      if (fx >= xp && fx < xp + wp && fy >= yp && fy < yp + hp) {
        rx = Num(G->gb[GB_view_xview][v]) + (fx - xp) * wv / wp;
        ry = Num(G->gb[GB_view_yview][v]) + (fy - yp) * hv / hp;
        break;
      }
    }
  }
  G->mouse_wx = (int)std::floor(rx);
  G->mouse_wy = (int)std::floor(ry);
  G->gb[GB_mouse_x][0] = (double)G->mouse_wx;
  G->gb[GB_mouse_y][0] = (double)G->mouse_wy;
  G->gb[GB_keyboard_key][0] = (double)G->last_key;
  G->gb[GB_keyboard_lastkey][0] = (double)G->last_key;
}

static void move_instance(Inst* i) {
  i->xprevious = i->x;
  i->yprevious = i->y;
  if (i->friction != 0 && i->speed != 0) {
    double s = i->speed;
    if (s > 0) s = std::max(0.0, s - i->friction); else s = std::min(0.0, s + i->friction);
    i->speed = s;
    i->set_sd();
  }
  if (i->gravity != 0) {
    i->hspeed += i->gravity * std::cos(i->gravity_direction * DEG);
    i->vspeed += -i->gravity * std::sin(i->gravity_direction * DEG);
    i->set_hv();
  }
  i->x += i->hspeed;
  i->y += i->vspeed;
}

void advance_paths(Inst* i);  // funcs.cpp

static void call_all(int type, int num) {
  std::vector<Inst*> snap = G->insts;
  for (Inst* i : snap) if (i->alive) run_event(i, type, num);
}

static void collision_events() {
  std::vector<Inst*> snap = G->insts;
  for (Inst* a : snap) {
    if (!a->alive) continue;
    // gather collision events defined for this object chain
    for (int o = a->obj; o >= 0 && a->alive; o = g_objs[o].parent) {
      for (const EvDef& e : g_objs[o].events) {
        if (e.type != 4 || !a->alive) continue;
        // an event defined in a parent can be overridden by a child with same target
        if (o != a->obj && find_event(a->obj, 4, 0, e.other) != e.fn) continue;
        for (int od : G->desc[e.other]) {
          std::vector<Inst*> cand = G->byobj[od];
          for (Inst* b : cand) {
            if (!a->alive) break;
            if (!b->alive || b == a) continue;
            if (!inst_meets(a, a->x, a->y, b)) continue;
            if (b->solid || a->solid) { a->x = a->xprevious; a->y = a->yprevious; }
            e.fn(a, b);
          }
        }
      }
    }
  }
}

static void mouse_events() {
  // global mouse events (50..61) and local button events
  bool lp = G->mbtn_pressed[1], rp = G->mbtn_pressed[3], mp = G->mbtn_pressed[2];
  bool lr = G->mbtn_released[1], rr = G->mbtn_released[3], mr = G->mbtn_released[2];
  bool ld = G->mbtn[1], rd = G->mbtn[3], md = G->mbtn[2];
  std::vector<Inst*> snap = G->insts;
  for (Inst* i : snap) {
    if (!i->alive) continue;
    if (ld) run_event(i, 6, 50);
    if (rd) run_event(i, 6, 51);
    if (md) run_event(i, 6, 52);
    if (lp) run_event(i, 6, 53);
    if (rp) run_event(i, 6, 54);
    if (mp) run_event(i, 6, 55);
    if (lr) run_event(i, 6, 56);
    if (rr) run_event(i, 6, 57);
    if (mr) run_event(i, 6, 58);
    if (G->wheel > 0) run_event(i, 6, 60);
    if (G->wheel < 0) run_event(i, 6, 61);
    // local events: only if mouse is over the instance
    bool over = false, checked = false;
    auto isover = [&]() {
      if (!checked) {
        checked = true;
        double l, t, r, b;
        over = bbox_of(i, i->x, i->y, l, t, r, b) && G->mouse_wx >= l && G->mouse_wx < r && G->mouse_wy >= t && G->mouse_wy < b;
      }
      return over;
    };
    for (int n : {0, 1, 2, 3, 4, 5, 6, 7, 8, 9}) {
      if (!i->alive) break;
      bool fire = (n == 0 && ld) || (n == 1 && rd) || (n == 2 && md) || (n == 3 && !ld && !rd && !md) ||
                  (n == 4 && lp) || (n == 5 && rp) || (n == 6 && mp) || (n == 7 && lr) || (n == 8 && rr) || (n == 9 && mr);
      if (fire && has_event(i->obj, 6, n) && isover()) run_event(i, 6, n);
    }
  }
}

static void keyboard_events() {
  std::vector<Inst*> snap = G->insts;
  for (Inst* i : snap) {
    if (!i->alive) continue;
    for (int o = i->obj; o >= 0; o = g_objs[o].parent) {
      for (const EvDef& e : g_objs[o].events) {
        if (!i->alive) break;
        if (e.type == 5 && getenv("HM_DEBUG_KEYS") && gm_key_down(e.num)) fprintf(stderr, "[key] frame %llu obj=%s num=%d match=%d\n", (unsigned long long)G->frame_no, g_objs[i->obj].name, e.num, find_event(i->obj, 5, e.num, -1) == e.fn);
        if (e.type == 5 && gm_key_down(e.num) && find_event(i->obj, 5, e.num, -1) == e.fn) e.fn(i, &G->dummy);
        else if (e.type == 9 && gm_key_pressed(e.num) && find_event(i->obj, 9, e.num, -1) == e.fn) e.fn(i, &G->dummy);
        else if (e.type == 10 && gm_key_released(e.num) && find_event(i->obj, 10, e.num, -1) == e.fn) e.fn(i, &G->dummy);
      }
    }
  }
}

static void animate(Inst* i) {
  Sprite* s = spr_of(i->sprite_index);
  if (!s || s->frames.empty()) return;
  double n = (double)s->frames.size();
  if (i->image_speed == 0) return;
  i->image_index += i->image_speed;
  if (i->image_index >= n || i->image_index < 0) {
    i->image_index = std::fmod(i->image_index, n);
    if (i->image_index < 0) i->image_index += n;
    run_event(i, 7, 7);
  }
}

extern double g_area; extern int g_ncalls, g_big;
void run_frame() {
  auto frame_start = std::chrono::steady_clock::now();
  poll_input();
  scripted_input();
  pad_update((int)G->frame_no + 1);
  if (G->quit) return;
  update_mouse();
  G->frame_no++;
  // begin step
  call_all(3, 1);
  // alarms
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) {
      if (!i->alive) continue;
      for (int a = 0; a < 12 && i->alive; a++) {
        if (i->alarm[a] > 0) {
          if (--i->alarm[a] == 0) {
            i->alarm[a] = -1;
            run_event(i, 2, a);
          }
        }
      }
    }
  }
  keyboard_events();
  mouse_events();
  call_all(3, 0);
  // movement, paths and animation
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) {
      if (!i->alive) continue;
      move_instance(i);
      advance_paths(i);
      animate(i);
    }
  }
  collision_events();
  call_all(3, 2);
  // outside room / boundary events
  {
    Room& rm = G->rooms[G->room];
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) {
      if (!i->alive) continue;
      if (has_event(i->obj, 7, 0) || has_event(i->obj, 7, 1)) {
        double l, t, r, b;
        if (!bbox_of(i, i->x, i->y, l, t, r, b)) { l = r = i->x; t = b = i->y; }
        if (r < 0 || l > rm.w || b < 0 || t > rm.h) run_event(i, 7, 0);
        else if (l < 0 || r > rm.w || t < 0 || b > rm.h) run_event(i, 7, 1);
      }
    }
  }
  // view following
  {
    Room& rm = G->rooms[G->room];
    if (rm.enableViews) {
      for (int v = 0; v < 8; v++) {
        int ob = (int)Num(G->gb[GB_view_object][v]);
        if (ob < 0 || !Num(G->gb[GB_view_visible][v])) continue;
        Inst* f = FirstInst(ob);
        if (f == &G->dummy) continue;
        double wv = Num(G->gb[GB_view_wview][v]), hv = Num(G->gb[GB_view_hview][v]);
        double xv = Num(G->gb[GB_view_xview][v]), yv = Num(G->gb[GB_view_yview][v]);
        double hb = Num(G->gb[GB_view_hborder][v]), vb = Num(G->gb[GB_view_vborder][v]);
        if (f->x < xv + hb) xv = f->x - hb;
        if (f->x > xv + wv - hb) xv = f->x - wv + hb;
        if (f->y < yv + vb) yv = f->y - vb;
        if (f->y > yv + hv - vb) yv = f->y - hv + vb;
        xv = std::max(0.0, std::min(xv, (double)rm.w - wv));
        yv = std::max(0.0, std::min(yv, (double)rm.h - hv));
        G->gb[GB_view_xview][v] = xv;
        G->gb[GB_view_yview][v] = yv;
      }
    }
  }
  // background scrolling
  for (int v = 0; v < 8; v++) {
    G->gb[GB_background_x][v] = Num(G->gb[GB_background_x][v]) + Num(G->gb[GB_background_hspeed][v]);
    G->gb[GB_background_y][v] = Num(G->gb[GB_background_y][v]) + Num(G->gb[GB_background_vspeed][v]);
  }
  flush_trash();
  // draw
  {
    static bool prof = getenv("HM_PROF") != nullptr;
    static double tl = 0, td = 0, te = 0; static int pn = 0;
    auto t0 = std::chrono::steady_clock::now();
    draw_begin_frame();
    draw_room_views();
    auto t1 = std::chrono::steady_clock::now();
    draw_end_frame();
    if (prof) {
      auto t2 = std::chrono::steady_clock::now();
      td += std::chrono::duration<double, std::milli>(t1 - t0).count();
      te += std::chrono::duration<double, std::milli>(t2 - t1).count();
      tl += std::chrono::duration<double, std::milli>(t0 - frame_start).count();
      if (++pn == 60) { fprintf(stderr, "[prof] tex calls/frame=%d  px area/frame=%.2f screens  big(>400k px)=%d\n", g_ncalls / 60, g_area / 60 / (1366.0 * 768), g_big / 60); g_area = 0; g_ncalls = g_big = 0; fprintf(stderr, "[prof] per frame ms: logic=%.2f draw=%.2f present=%.2f\n", tl / pn, td / pn, te / pn); pn = 0; tl = td = te = 0; }
    }
  }
  flush_trash();
  // room change
  if (G->restart_game) {
    G->restart_game = false;
    leave_room();
    for (Inst* i : G->insts) { i->alive = false; G->trash.push_back(i); }
    flush_trash();
    G->gv.clear();
    for (int a = 0; a < GB__COUNT; a++) for (int b = 0; b < 8; b++) G->gb[a][b] = Value();
    G->game_started = false;
    enter_room(0);
  } else if (G->next_room >= 0) {
    int r = G->next_room;
    leave_room();
    enter_room(r);
  }
}

// ------------------------------------------------------------------ main loop
void engine_run() {
  using clk = std::chrono::steady_clock;
  auto last = clk::now();
  double acc = 0;
  auto fps_t = clk::now();
  int fps_n = 0;
  while (!G->quit) {
    double speed = std::max(1.0, Num(G->gb[GB_room_speed][0]));
    double step = 1.0 / speed;
    auto now = clk::now();
    acc += std::chrono::duration<double>(now - last).count();
    last = now;
    if (acc > 0.25) acc = 0.25;
    if (acc >= step) {
      acc -= step;
      run_frame();
      fps_n++;
      if (std::chrono::duration<double>(clk::now() - fps_t).count() >= 1.0) {
        G->fps = fps_n;
        fps_n = 0;
        fps_t = clk::now();
      }
    } else {
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
  }
}

void start_game(int room) { enter_room(room); }
