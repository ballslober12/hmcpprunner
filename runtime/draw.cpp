#include "game.h"
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#define FN(n) Value F_##n(Inst* self, Inst* other, Args a)
#define N(i) Num(a[i])
static const double DEG = M_PI / 180.0;

struct ProjX : Proj { double px = 0, py = 0; };
static ProjX P;
static std::vector<ProjX> pstack;
// internal resolution: the frame texture is rw x rh while the game thinks in fbw x fbh; surfaces render 1:1
static inline double RS() { return G->cur_target < 0 ? G->rscale : 1.0; }
static int gui_w = 1366, gui_h = 768;

static inline SDL_Color col(uint32_t bgr, double alpha) {
  SDL_Color c;
  c.r = bgr & 255; c.g = (bgr >> 8) & 255; c.b = (bgr >> 16) & 255;
  c.a = (Uint8)std::max(0.0, std::min(255.0, alpha * 255.0 + 0.5));
  return c;
}
static inline double tx(double x) { return (x - P.ox) * P.sx + P.px; }
static unsigned long g_ops = 0;
static bool g_noskip = getenv("HM_NOSKIP") != nullptr;
static inline double ty(double y) { return (y - P.oy) * P.sy + P.py; }

static bool is_software() {
  static int sw = -1;
  if (sw < 0) { SDL_RendererInfo ri; sw = (SDL_GetRendererInfo(G->ren, &ri) == 0 && (ri.flags & SDL_RENDERER_SOFTWARE)) ? 1 : 0; }
  return sw == 1;
}
// custom blend modes (subtract/max) are unsupported by SDL's software renderer: skip those draws there
static bool blend_unsupported() { static int sk = getenv("HM_SKIPBLEND") ? atoi(getenv("HM_SKIPBLEND")) : 0; return (G->ds.blend == 2 || G->ds.blend == 3) && (sk == 1 || sk == G->ds.blend || is_software()); }
static SDL_BlendMode blend_mode() {
  switch (G->ds.blend) {
    case 1: return SDL_BLENDMODE_ADD;
    case 3: {
      static int v = getenv("HM_SUBF") ? atoi(getenv("HM_SUBF")) : 4;
      if (v == 4) {
        // dst * (1 - src). Needs separate alpha factors; GLES 1.1 without GL_OES_blend_func_separate gets the plain variant.
        static SDL_BlendMode m = [] {
          SDL_BlendMode sep = SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                                         SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
          if (SDL_SetRenderDrawBlendMode(G->ren, sep) == 0) return sep;
          return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD,
                                            SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_COLOR, SDL_BLENDOPERATION_ADD);
        }();
        return m;
      }
      if (v == 3) return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_SUBTRACT, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      if (v == 1) return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                              SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      if (v == 2) return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                              SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
      return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_REV_SUBTRACT,
                                              SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    }
    case 2: return SDL_ComposeCustomBlendMode(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_MAXIMUM,
                                              SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDOPERATION_ADD);
    default: return SDL_BLENDMODE_BLEND;
  }
}

// ------------------------------------------------------------------ textures
SDL_Texture* make_texture_from_png(const std::string& path, int* w, int* h, std::vector<uint8_t>* alpha, bool white) {
  int cw, ch, comp;
  unsigned char* px = stbi_load(path.c_str(), &cw, &ch, &comp, 4);
  if (!px) return nullptr;
  if (white)
    for (int i = 0; i < cw * ch; i++) { px[i * 4] = px[i * 4 + 1] = px[i * 4 + 2] = 255; }
  if (alpha) {
    alpha->resize((size_t)cw * ch);
    for (int i = 0; i < cw * ch; i++) (*alpha)[i] = px[i * 4 + 3];
  }
  SDL_Texture* t = SDL_CreateTexture(G->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, cw, ch);
  SDL_UpdateTexture(t, nullptr, px, cw * 4);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  stbi_image_free(px);
  if (w) *w = cw;
  if (h) *h = ch;
  return t;
}

Sprite& ensure_sprite(int s) {
  Sprite& sp = G->spr[s];
  if (sp.loaded) return sp;
  sp.loaded = true;
  sp.tex.assign(sp.frames.size(), nullptr);
  sp.white.assign(sp.frames.size(), nullptr);
  if (!sp.dynamic)
    for (size_t i = 0; i < sp.frames.size(); i++)
      sp.tex[i] = make_texture_from_png(G->root + "/sprites/" + sp.frames[i], nullptr, nullptr);
  return sp;
}
void ensure_bg(int b) {
  Background& bg = G->bg[b];
  if (bg.loaded) return;
  bg.loaded = true;
  bg.tex = make_texture_from_png(G->root + "/background/" + bg.path, &bg.w, &bg.h);
}
void ensure_font(int f) {
  Font& ft = G->fnt[f];
  if (ft.loaded) return;
  ft.loaded = true;
  ft.tex = make_texture_from_png(G->root + "/fonts/" + ft.name + ".png", nullptr, nullptr);
  if (!ft.tex) {
    // GameMaker names font images after the resource; fall back to scanning
    fprintf(stderr, "font image missing for %s\n", ft.name.c_str());
  }
}

double g_area = 0; int g_ncalls = 0, g_big = 0;
// ------------------------------------------------------------------ low-level drawing
static void draw_tex(SDL_Texture* t, int tw, int th, const SDL_Rect* src, double x, double y, double xo, double yo,
                     double xs, double ys, double rot, uint32_t color, double alpha) {
  if (!t || blend_unsupported()) return;
  int sw = src ? src->w : tw, sh = src ? src->h : th;
  double left = std::min(x - xo * xs, x - xo * xs + sw * xs);
  double top = std::min(y - yo * ys, y - yo * ys + sh * ys);
  double w = sw * std::fabs(xs), h = sh * std::fabs(ys);
  SDL_FRect d = {(float)tx(left), (float)ty(top), (float)(w * P.sx), (float)(h * P.sy)};
  SDL_SetTextureColorMod(t, color & 255, (color >> 8) & 255, (color >> 16) & 255);
  SDL_SetTextureAlphaMod(t, (Uint8)std::max(0.0, std::min(255.0, alpha * 255.0 + 0.5)));
  SDL_SetTextureBlendMode(t, blend_mode());
  int flip = (xs < 0 ? SDL_FLIP_HORIZONTAL : 0) | (ys < 0 ? SDL_FLIP_VERTICAL : 0);
  SDL_FPoint c = {(float)((x - left) * P.sx), (float)((y - top) * P.sy)};
  (g_ops++, SDL_RenderCopyExF)(G->ren, t, src, &d, -rot, &c, (SDL_RendererFlip)flip);
  g_area += (double)d.w * d.h; g_ncalls++;
  if (d.w * d.h > 400000) g_big++;
  { static int cnt = 0; if (getenv("HM_PROF") && d.w * d.h > 60000 && ++cnt < 40 && cnt > 0) fprintf(stderr, "  bigdraw %.0fx%.0f blend=%d alpha=%.2f\n", d.w, d.h, G->ds.blend, alpha); }
}

static int dbg_calls = 0, dbg_null = 0;
void draw_debug_report() { fprintf(stderr, "[draw] sprite calls=%d null_tex=%d\n", dbg_calls, dbg_null); dbg_calls = dbg_null = 0; }
void draw_sprite_full(int s, double frame, double x, double y, double xs, double ys, double rot, uint32_t color, double alpha) {
  dbg_calls++;
  if (s < 0 || s >= (int)G->spr.size()) return;
  Sprite& sp = ensure_sprite(s);
  if (sp.frames.empty() && sp.tex.empty()) return;
  int n = (int)sp.tex.size();
  if (n == 0) return;
  int f = (int)std::floor(frame) % n;
  if (f < 0) f += n;
  SDL_Texture* t = sp.tex[f];
  if (!t) { dbg_null++; if (dbg_null < 3) fprintf(stderr, "[draw] null texture sprite=%s frame=%d path=%s\n", sp.name.c_str(), f, sp.frames[f].c_str()); }
  if (G->ds.fog) {
    if (!sp.white[f] && !sp.dynamic) sp.white[f] = make_texture_from_png(G->root + "/sprites/" + sp.frames[f], nullptr, nullptr, nullptr, true);
    if (sp.white[f]) { t = sp.white[f]; color = G->ds.fogcol; }
  }
  draw_tex(t, sp.w, sp.h, nullptr, x, y, sp.xorig, sp.yorig, xs, ys, rot, color, alpha);
}

static void geom(const std::vector<SDL_Vertex>& v, SDL_Texture* t = nullptr) {
  if (v.empty() || blend_unsupported()) return;
  if (G->ds.blend == 3 && v.size() > 6 && getenv("HM_SKIPCIRCLE")) return;
  if (G->ds.blend == 3 && getenv("HM_BLENDLOG")) { fprintf(stderr, "sub geom n=%zu v0=(%.0f,%.0f) c=%d,%d,%d,%d tex=%d\n", v.size(), (double)v[0].position.x, (double)v[0].position.y, (int)v[0].color.r, (int)v[0].color.g, (int)v[0].color.b, (int)v[0].color.a, (int)(t != nullptr)); }
  if (t) SDL_SetTextureBlendMode(t, blend_mode());
  SDL_SetRenderDrawBlendMode(G->ren, blend_mode());
  (g_ops++, SDL_RenderGeometry)(G->ren, t, v.data(), (int)v.size(), nullptr, 0);
}
static SDL_Vertex vtx(double x, double y, uint32_t c, double a, double u = 0, double v = 0) {
  SDL_Vertex r;
  r.position = {(float)tx(x), (float)ty(y)};
  r.color = col(c, a);
  r.tex_coord = {(float)u, (float)v};
  return r;
}
static void tri(std::vector<SDL_Vertex>& o, SDL_Vertex a, SDL_Vertex b, SDL_Vertex c) { o.push_back(a); o.push_back(b); o.push_back(c); }
static void quad_fill(double x1, double y1, double x2, double y2, uint32_t c1, uint32_t c2, uint32_t c3, uint32_t c4, double al) {
  std::vector<SDL_Vertex> v;
  SDL_Vertex a = vtx(x1, y1, c1, al), b = vtx(x2, y1, c2, al), c = vtx(x2, y2, c3, al), d = vtx(x1, y2, c4, al);
  tri(v, a, b, c);
  tri(v, a, c, d);
  geom(v);
}
static void line_quad(double x1, double y1, double x2, double y2, double w, uint32_t c1, uint32_t c2, double al) {
  double dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
  if (len < 1e-9) { dx = 1; dy = 0; len = 1; }
  double nx = -dy / len * w / 2, ny = dx / len * w / 2;
  std::vector<SDL_Vertex> v;
  SDL_Vertex a = vtx(x1 + nx, y1 + ny, c1, al), b = vtx(x1 - nx, y1 - ny, c1, al), c = vtx(x2 - nx, y2 - ny, c2, al),
             d = vtx(x2 + nx, y2 + ny, c2, al);
  tri(v, a, b, c);
  tri(v, a, c, d);
  geom(v);
}

// ------------------------------------------------------------------ frame / views
static std::vector<Inst*> drawlist;

void draw_begin_frame() {
  if (G->cur_target >= 0 && G->cur_target < (int)G->surfaces.size()) G->surfaces[G->cur_target].dirty = true;
  SDL_SetRenderTarget(G->ren, G->frame);
  G->cur_target = -1;
  SDL_RenderSetClipRect(G->ren, nullptr);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, 0, 0, 0, 255);
  SDL_RenderClear(G->ren);
  G->ds.blend = 0;
  G->ds.fog = false;
  G->ds.alpha = 1;
  G->ds.color = 0xFFFFFF;
}
void draw_end_frame() {
  SDL_SetRenderTarget(G->ren, nullptr);
  SDL_RenderSetClipRect(G->ren, nullptr);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, 0, 0, 0, 255);
  SDL_RenderClear(G->ren);
  int ww, wh;
  SDL_GetRendererOutputSize(G->ren, &ww, &wh);
  double sc = std::min((double)ww / G->fbw, (double)wh / G->fbh);
  SDL_FRect d = {(float)((ww - G->fbw * sc) / 2), (float)((wh - G->fbh * sc) / 2), (float)(G->fbw * sc), (float)(G->fbh * sc)};
  SDL_SetTextureBlendMode(G->frame, SDL_BLENDMODE_NONE);
  (g_ops++, SDL_RenderCopyF)(G->ren, G->frame, nullptr, &d);
  SDL_RenderPresent(G->ren);
}

static void draw_inst(Inst* i) {
  if (!i->alive || !i->visible) return;
  if (has_event(i->obj, 8, 0)) {
    run_event(i, 8, 0);
  } else if (i->sprite_index >= 0) {
    F_draw_self(i, &G->dummy, Args{});
  }
}

static void draw_backgrounds(Room& rm, bool fore, double vx, double vy, double vw, double vh) {
  for (int b = 0; b < 8; b++) {
    if (!Num(G->gb[GB_background_visible][b])) continue;
    if ((Num(G->gb[GB_background_foreground][b]) != 0) != fore) continue;
    int bi = (int)Num(G->gb[GB_background_index][b]);
    if (bi < 0 || bi >= (int)G->bg.size()) continue;
    ensure_bg(bi);
    Background& bg = G->bg[bi];
    if (!bg.tex) continue;
    double bx = Num(G->gb[GB_background_x][b]), by = Num(G->gb[GB_background_y][b]);
    double xs = Num(G->gb[GB_background_xscale][b]), ys = Num(G->gb[GB_background_yscale][b]);
    uint32_t bl = (uint32_t)Num(G->gb[GB_background_blend][b]);
    double al = Num(G->gb[GB_background_alpha][b]);
    bool ht = Num(G->gb[GB_background_htiled][b]) != 0, vt = Num(G->gb[GB_background_vtiled][b]) != 0;
    double w = bg.w * xs, h = bg.h * ys;
    if (w <= 0 || h <= 0) continue;
    double x0 = bx, y0 = by;
    if (ht) x0 = bx - std::ceil((bx - vx) / w) * w;
    if (vt) y0 = by - std::ceil((by - vy) / h) * h;
    for (double yy = y0; yy < (vt ? vy + vh : y0 + 1); yy += h) {
      for (double xx = x0; xx < (ht ? vx + vw : x0 + 1); xx += w) {
        draw_tex(bg.tex, bg.w, bg.h, nullptr, xx, yy, 0, 0, xs, ys, 0, bl, al);
        if (!ht) break;
      }
      if (!vt) break;
    }
  }
}

struct DrawItem { int depth; int kind; Inst* inst; int tile; int seq; };

static void draw_one_view(Room& rm, double vx, double vy, double vw, double vh, double px, double py, double pw, double ph) {
  P = ProjX();
  P.ox = vx; P.oy = vy; P.sx = pw / vw * RS(); P.sy = ph / vh * RS(); P.px = px * RS(); P.py = py * RS();
  SDL_Rect clip = {(int)(px * RS()), (int)(py * RS()), (int)std::ceil(pw * RS()), (int)std::ceil(ph * RS())};
  SDL_RenderSetClipRect(G->ren, &clip);
  if (Num(G->gb[GB_background_showcolor][0])) {
    SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
    uint32_t c = (uint32_t)Num(G->gb[GB_background_color][0]);
    SDL_SetRenderDrawColor(G->ren, c & 255, (c >> 8) & 255, (c >> 16) & 255, 255);
    (g_ops++, SDL_RenderFillRect)(G->ren, &clip);
  }
  draw_backgrounds(rm, false, vx, vy, vw, vh);
  // draw-begin events
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) if (i->alive && i->visible) run_event(i, 8, 72);
  }
  std::vector<DrawItem> items;
  int seq = 0;
  for (size_t t = 0; t < G->tiles.size(); t++) items.push_back({G->tiles[t].depth, 1, nullptr, (int)t, seq++});
  for (Inst* i : G->insts) if (i->alive && i->visible) items.push_back({i->depth, 0, i, 0, seq++});
  std::stable_sort(items.begin(), items.end(), [](const DrawItem& a, const DrawItem& b) {
    if (a.depth != b.depth) return a.depth > b.depth;
    return a.seq < b.seq;
  });
  for (auto& it : items) {
    if (it.kind == 1) {
      auto& t = G->tiles[it.tile];
      if (!t.visible) continue;
      ensure_bg(t.bg);
      Background& bg = G->bg[t.bg];
      if (!bg.tex) continue;
      // cull
      if (t.x + t.w < vx || t.x > vx + vw || t.y + t.h < vy || t.y > vy + vh) continue;
      SDL_Rect src = {(int)t.xo, (int)t.yo, (int)t.w, (int)t.h};
      draw_tex(bg.tex, bg.w, bg.h, &src, t.x, t.y, 0, 0, t.sx, t.sy, 0, t.col & 0xFFFFFF, ((t.col >> 24) & 255) / 255.0);
    } else {
      draw_inst(it.inst);
    }
    P.ox = vx; P.oy = vy; P.sx = pw / vw * RS(); P.sy = ph / vh * RS(); P.px = px * RS(); P.py = py * RS();
    if (G->cur_target != -1) { /* script left a surface target active; restore */ }
  }
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) if (i->alive && i->visible) run_event(i, 8, 73);
  }
  draw_backgrounds(rm, true, vx, vy, vw, vh);
}

void draw_room_views() {
  Room& rm = G->rooms[G->room];
  G->ds.blend = 0;
  G->ds.fog = false;
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) if (i->alive) run_event(i, 8, 76);
  }
  if (rm.enableViews) {
    bool any = false;
    for (int v = 0; v < 8; v++) {
      if (!Num(G->gb[GB_view_visible][v])) continue;
      any = true;
      G->gb[GB_view_current][0] = (double)v;
      draw_one_view(rm, Num(G->gb[GB_view_xview][v]), Num(G->gb[GB_view_yview][v]), Num(G->gb[GB_view_wview][v]),
                    Num(G->gb[GB_view_hview][v]), Num(G->gb[GB_view_xport][v]), Num(G->gb[GB_view_yport][v]),
                    Num(G->gb[GB_view_wport][v]), Num(G->gb[GB_view_hport][v]));
    }
    if (!any) draw_one_view(rm, 0, 0, rm.w, rm.h, 0, 0, G->fbw, G->fbh);
  } else {
    draw_one_view(rm, 0, 0, rm.w, rm.h, 0, 0, std::min(rm.w, G->fbw), std::min(rm.h, G->fbh));
  }
  // GUI layer
  P = ProjX();
  P.sx = (double)G->rw / gui_w;
  P.sy = (double)G->rh / gui_h;
  SDL_RenderSetClipRect(G->ren, nullptr);
  G->ds.blend = 0; G->ds.fog = false;
  {
    std::vector<Inst*> snap = G->insts;
    for (Inst* i : snap) if (i->alive && i->visible) { run_event(i, 8, 74); }
    for (Inst* i : snap) if (i->alive && i->visible) { run_event(i, 8, 64); }
    for (Inst* i : snap) if (i->alive && i->visible) { run_event(i, 8, 75); }
    for (Inst* i : snap) if (i->alive) run_event(i, 8, 77);
  }
  G->ds.blend = 0;
}

void draw_shutdown() {}
int save_png_rgba(const char* path, int w, int h, const void* px) { return stbi_write_png(path, w, h, 4, px, w * 4); }

// ------------------------------------------------------------------ draw state functions
FN(draw_set_color) { G->ds.color = (uint32_t)(long long)N(0) & 0xFFFFFF; return 0; }
FN(draw_set_colour) { G->ds.color = (uint32_t)(long long)N(0) & 0xFFFFFF; return 0; }
FN(draw_set_alpha) { G->ds.alpha = N(0); return 0; }
FN(draw_set_blend_mode) { G->ds.blend = (int)N(0); return 0; }
FN(draw_set_font) { G->ds.font = (int)N(0); return 0; }
FN(draw_set_halign) { G->ds.halign = (int)N(0); return 0; }
FN(draw_set_valign) { G->ds.valign = (int)N(0); return 0; }
FN(d3d_set_fog) {
  G->ds.fog = T(a[0]);
  G->ds.fogcol = (uint32_t)(long long)N(1) & 0xFFFFFF;
  return 0;
}
FN(d3d_set_projection_ortho) {
  // (x, y, w, h, angle)
  double x = N(0), y = N(1), w = N(2), h = N(3);
  SDL_Rect clip;
  SDL_Texture* tgt = SDL_GetRenderTarget(G->ren);
  int tw, th;
  if (G->cur_target >= 0) { tw = G->surfaces[G->cur_target].w; th = G->surfaces[G->cur_target].h; }
  else { tw = G->rw; th = G->rh; }
  (void)tgt; (void)clip;
  P = ProjX();
  P.ox = x; P.oy = y;
  P.sx = w != 0 ? tw / w : 1;
  P.sy = h != 0 ? th / h : 1;
  return 0;
}
FN(display_set_gui_size) { gui_w = (int)N(0); gui_h = (int)N(1); return 0; }
FN(texture_set_interpolation) { G->ds.interp = T(a[0]); return 0; }

// ------------------------------------------------------------------ sprites
FN(draw_self) {
  draw_sprite_full(self->sprite_index, self->image_index, self->x, self->y, self->image_xscale, self->image_yscale,
                   self->image_angle, self->image_blend, self->image_alpha);
  return 0;
}
FN(draw_sprite) {
  draw_sprite_full((int)N(0), N(1), N(2), N(3), 1, 1, 0, 0xFFFFFF, G->ds.alpha);
  return 0;
}
FN(draw_sprite_ext) {
  draw_sprite_full((int)N(0), N(1), N(2), N(3), N(4), N(5), N(6), (uint32_t)(long long)N(7) & 0xFFFFFF, N(8));
  return 0;
}
FN(draw_sprite_general) {
  // sprite, subimg, left, top, width, height, x, y, xscale, yscale, rot, c1, c2, c3, c4, alpha
  int s = (int)N(0);
  if (s < 0 || s >= (int)G->spr.size()) return 0;
  Sprite& sp = ensure_sprite(s);
  int n = (int)sp.tex.size();
  if (!n) return 0;
  int f = ((int)std::floor(N(1)) % n + n) % n;
  SDL_Rect src = {(int)N(2), (int)N(3), (int)N(4), (int)N(5)};
  draw_tex(sp.tex[f], sp.w, sp.h, &src, N(6), N(7), 0, 0, N(8), N(9), N(10), (uint32_t)(long long)N(11) & 0xFFFFFF, N(15));
  return 0;
}
static void tiled(int s, double sub, double x, double y, double xs, double ys, uint32_t c, double al) {
  if (s < 0 || s >= (int)G->spr.size()) return;
  Sprite& sp = ensure_sprite(s);
  if (sp.w <= 0 || sp.h <= 0) return;
  double w = sp.w * xs, h = sp.h * ys;
  double vx = P.ox, vy = P.oy, vw = (G->cur_target >= 0 ? G->surfaces[G->cur_target].w : G->rw) / P.sx,
         vh = (G->cur_target >= 0 ? G->surfaces[G->cur_target].h : G->rh) / P.sy;
  double x0 = x - std::ceil((x - vx) / w) * w, y0 = y - std::ceil((y - vy) / h) * h;
  for (double yy = y0; yy < vy + vh; yy += h)
    for (double xx = x0; xx < vx + vw; xx += w) draw_sprite_full(s, sub, xx + sp.xorig * xs, yy + sp.yorig * ys, xs, ys, 0, c, al);
}
FN(draw_sprite_tiled) { tiled((int)N(0), N(1), N(2), N(3), 1, 1, 0xFFFFFF, G->ds.alpha); return 0; }
FN(draw_sprite_tiled_ext) {
  tiled((int)N(0), N(1), N(2), N(3), N(4), N(5), (uint32_t)(long long)N(6) & 0xFFFFFF, N(7));
  return 0;
}
FN(sprite_exists) { int s = (int)N(0); return s >= 0 && s < (int)G->spr.size() && !G->spr[s].frames.empty() ? 1 : 0; }
FN(sprite_get_width) { int s = (int)N(0); return s >= 0 && s < (int)G->spr.size() ? G->spr[s].w : 0; }
FN(sprite_get_height) { int s = (int)N(0); return s >= 0 && s < (int)G->spr.size() ? G->spr[s].h : 0; }
FN(sprite_get_number) { int s = (int)N(0); return s >= 0 && s < (int)G->spr.size() ? (double)G->spr[s].frames.size() : 0.0; }
FN(sprite_get_name) { int s = (int)N(0); return s >= 0 && s < SPR__COUNT ? Value(g_spr_names[s]) : Value("<undefined>"); }
FN(sprite_add) {
  // (fname, imgnum, removeback, smooth, xorig, yorig)
  Sprite sp;
  int w, h;
  std::string path = gm_path(Str(a[0]));
  SDL_Texture* t = make_texture_from_png(path, &w, &h);
  if (!t) { fprintf(stderr, "sprite_add failed: %s\n", path.c_str()); return -1; }
  sp.dynamic = true;
  sp.loaded = true;
  sp.w = w; sp.h = h;
  sp.xorig = (int)N(4); sp.yorig = (int)N(5);
  sp.bbl = 0; sp.bbt = 0; sp.bbr = w - 1; sp.bbb = h - 1;
  sp.frames.push_back("");
  sp.tex.push_back(t);
  sp.white.push_back(nullptr);
  G->spr.push_back(sp);
  return (double)(G->spr.size() - 1);
}
FN(sprite_delete) {
  int s = (int)N(0);
  if (s < SPR__COUNT || s >= (int)G->spr.size()) return 0;
  Sprite& sp = G->spr[s];
  for (auto t : sp.tex) if (t) SDL_DestroyTexture(t);
  sp.tex.clear(); sp.frames.clear(); sp.w = sp.h = 0;
  return 0;
}
FN(sprite_save) { return 0; }

// ------------------------------------------------------------------ surfaces
FN(surface_create) {
  int w = std::max(1, (int)N(0)), h = std::max(1, (int)N(1));
  SDL_Texture* t = SDL_CreateTexture(G->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
  if (!t) return -1;
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_Texture* prev = SDL_GetRenderTarget(G->ren);
  SDL_SetRenderTarget(G->ren, t);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, 0, 0, 0, 0);
  SDL_RenderClear(G->ren);
  SDL_SetRenderTarget(G->ren, prev);
  Surface s; s.tex = t; s.w = w; s.h = h; s.used = true; s.dirty = false;
  for (size_t i = 0; i < G->surfaces.size(); i++)
    if (!G->surfaces[i].used) { G->surfaces[i] = s; return (double)i; }
  G->surfaces.push_back(s);
  return (double)(G->surfaces.size() - 1);
}
static Surface* surf(int id) {
  if (id < 0 || id >= (int)G->surfaces.size() || !G->surfaces[id].used) return nullptr;
  return &G->surfaces[id];
}
FN(surface_exists) { return surf((int)N(0)) ? 1 : 0; }
FN(surface_free) {
  Surface* s = surf((int)N(0));
  if (!s) return 0;
  if (G->cur_target == (int)N(0)) return 0;
  SDL_DestroyTexture(s->tex);
  s->tex = nullptr; s->used = false;
  return 0;
}
FN(surface_get_width) { Surface* s = surf((int)N(0)); return s ? s->w : 0; }
FN(surface_get_height) { Surface* s = surf((int)N(0)); return s ? s->h : 0; }
FN(surface_get_texture) { return N(0); }
FN(surface_set_target) {
  Surface* s = surf((int)N(0));
  if (!s) return 0;
  G->target_stack.push_back(G->cur_target);
  pstack.push_back(P);
  G->cur_target = (int)N(0);
  s->base = g_ops; s->cleared = false;
  SDL_SetRenderTarget(G->ren, s->tex);
  SDL_RenderSetClipRect(G->ren, nullptr);
  P = ProjX();
  return 0;
}
FN(surface_reset_target) {
  if (G->target_stack.empty()) return 0;
  int prev = G->target_stack.back();
  G->target_stack.pop_back();
  if (Surface* cs = G->cur_target >= 0 ? surf(G->cur_target) : nullptr) {
    if (g_ops != cs->base) cs->dirty = true;
    if (cs->cleared && g_ops == cs->clearbase) cs->dirty = false;
  }
  G->cur_target = prev;
  Surface* s = prev >= 0 ? surf(prev) : nullptr;
  SDL_SetRenderTarget(G->ren, s ? s->tex : G->frame);
  if (!pstack.empty()) { P = pstack.back(); pstack.pop_back(); }
  return 0;
}
static void surf_draw(Surface* s, const SDL_Rect* src, double x, double y, double xs, double ys, double rot, uint32_t c, double al) {
  if (!s->dirty && !(G->cur_target >= 0 && &G->surfaces[G->cur_target] == s) && !g_noskip) return;
  draw_tex(s->tex, s->w, s->h, src, x, y, 0, 0, xs, ys, rot, c, al);
}
FN(draw_surface) { Surface* s = surf((int)N(0)); if (s) surf_draw(s, nullptr, N(1), N(2), 1, 1, 0, 0xFFFFFF, G->ds.alpha); return 0; }
FN(draw_surface_ext) {
  Surface* s = surf((int)N(0));
  if (s) surf_draw(s, nullptr, N(1), N(2), N(3), N(4), N(5), (uint32_t)(long long)N(6) & 0xFFFFFF, N(7));
  return 0;
}
FN(draw_surface_stretched) {
  Surface* s = surf((int)N(0));
  if (s && s->w && s->h) surf_draw(s, nullptr, N(1), N(2), N(3) / s->w, N(4) / s->h, 0, 0xFFFFFF, G->ds.alpha);
  return 0;
}
FN(draw_surface_part_ext) {
  // (id, left, top, w, h, x, y, xscale, yscale, colour, alpha)
  Surface* s = surf((int)N(0));
  if (!s) return 0;
  SDL_Rect src = {(int)N(1), (int)N(2), (int)N(3), (int)N(4)};
  surf_draw(s, &src, N(5), N(6), N(7), N(8), 0, (uint32_t)(long long)N(9) & 0xFFFFFF, N(10));
  return 0;
}
FN(draw_clear) {
  uint32_t c = (uint32_t)(long long)N(0) & 0xFFFFFF;
  SDL_RenderSetClipRect(G->ren, nullptr);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, c & 255, (c >> 8) & 255, (c >> 16) & 255, 255);
  SDL_RenderClear(G->ren);
  if (G->cur_target >= 0) if (Surface* cs = surf(G->cur_target)) { cs->dirty = true; cs->cleared = false; }
  return 0;
}
FN(draw_clear_alpha) {
  uint32_t c = (uint32_t)(long long)N(0) & 0xFFFFFF;
  SDL_RenderSetClipRect(G->ren, nullptr);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, c & 255, (c >> 8) & 255, (c >> 16) & 255, (Uint8)(N(1) * 255));
  SDL_RenderClear(G->ren);
  if (G->cur_target >= 0) if (Surface* cs = surf(G->cur_target)) { if (N(1) < 0.004) { cs->cleared = true; cs->clearbase = g_ops; cs->dirty = false; } else cs->dirty = true; }
  return 0;
}
static bool save_target(const std::string& path, SDL_Texture* t, int w, int h) {
  SDL_Texture* prev = SDL_GetRenderTarget(G->ren);
  SDL_SetRenderTarget(G->ren, t);
  std::vector<uint8_t> px((size_t)w * h * 4);
  SDL_RenderReadPixels(G->ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), w * 4);
  SDL_SetRenderTarget(G->ren, prev);
  return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
}
FN(surface_save) { Surface* s = surf((int)N(0)); if (s) save_target(gm_path(Str(a[1])), s->tex, s->w, s->h); return 0; }
FN(screen_save) { save_target(gm_path(Str(a[0])), G->frame, G->rw, G->rh); return 0; }
FN(sprite_create_from_surface) {
  // (id, x, y, w, h, removeback, smooth, xorig, yorig)
  Surface* s = surf((int)N(0));
  if (!s) return -1;
  int w = (int)N(3), h = (int)N(4);
  SDL_Texture* t = SDL_CreateTexture(G->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
  SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
  SDL_Texture* prev = SDL_GetRenderTarget(G->ren);
  SDL_SetRenderTarget(G->ren, t);
  SDL_SetRenderDrawBlendMode(G->ren, SDL_BLENDMODE_NONE);
  SDL_SetRenderDrawColor(G->ren, 0, 0, 0, 0);
  SDL_RenderClear(G->ren);
  SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_NONE);
  SDL_Rect src = {(int)N(1), (int)N(2), w, h};
  (g_ops++, SDL_RenderCopy)(G->ren, s->tex, &src, nullptr);
  SDL_SetTextureBlendMode(s->tex, SDL_BLENDMODE_BLEND);
  SDL_SetRenderTarget(G->ren, prev);
  Sprite sp;
  sp.dynamic = true; sp.loaded = true; sp.w = w; sp.h = h; sp.xorig = (int)N(7); sp.yorig = (int)N(8);
  sp.bbl = 0; sp.bbt = 0; sp.bbr = w - 1; sp.bbb = h - 1;
  sp.frames.push_back(""); sp.tex.push_back(t); sp.white.push_back(nullptr);
  G->spr.push_back(sp);
  return (double)(G->spr.size() - 1);
}

// ------------------------------------------------------------------ primitives
FN(draw_rectangle) {
  double x1 = N(0), y1 = N(1), x2 = N(2), y2 = N(3);
  uint32_t c = G->ds.color;
  if (T(a[4])) {
    line_quad(x1, y1, x2, y1, 1, c, c, G->ds.alpha);
    line_quad(x2, y1, x2, y2, 1, c, c, G->ds.alpha);
    line_quad(x2, y2, x1, y2, 1, c, c, G->ds.alpha);
    line_quad(x1, y2, x1, y1, 1, c, c, G->ds.alpha);
  } else {
    quad_fill(x1, y1, x2 + 1, y2 + 1, c, c, c, c, G->ds.alpha);
  }
  return 0;
}
FN(draw_rectangle_color) {
  double x1 = N(0), y1 = N(1), x2 = N(2), y2 = N(3);
  uint32_t c1 = (uint32_t)(long long)N(4) & 0xFFFFFF, c2 = (uint32_t)(long long)N(5) & 0xFFFFFF,
           c3 = (uint32_t)(long long)N(6) & 0xFFFFFF, c4 = (uint32_t)(long long)N(7) & 0xFFFFFF;
  if (T(a[8])) {
    line_quad(x1, y1, x2, y1, 1, c1, c2, G->ds.alpha);
    line_quad(x2, y1, x2, y2, 1, c2, c3, G->ds.alpha);
    line_quad(x2, y2, x1, y2, 1, c3, c4, G->ds.alpha);
    line_quad(x1, y2, x1, y1, 1, c4, c1, G->ds.alpha);
  } else {
    quad_fill(x1, y1, x2 + 1, y2 + 1, c1, c2, c3, c4, G->ds.alpha);
  }
  return 0;
}
static void circle(double x, double y, double r, uint32_t cc, uint32_t ce, bool outline) {
  int seg = std::max(12, std::min(96, (int)(r * std::max(P.sx, 0.01) * 0.6)));
  std::vector<SDL_Vertex> v;
  for (int i = 0; i < seg; i++) {
    double a0 = 2 * M_PI * i / seg, a1 = 2 * M_PI * (i + 1) / seg;
    double x0 = x + r * std::cos(a0), y0 = y + r * std::sin(a0), x1 = x + r * std::cos(a1), y1 = y + r * std::sin(a1);
    if (outline) line_quad(x0, y0, x1, y1, 1, ce, ce, G->ds.alpha);
    else tri(v, vtx(x, y, cc, G->ds.alpha), vtx(x0, y0, ce, G->ds.alpha), vtx(x1, y1, ce, G->ds.alpha));
  }
  if (!outline) geom(v);
}
FN(draw_circle) { circle(N(0), N(1), N(2), G->ds.color, G->ds.color, T(a[3])); return 0; }
FN(draw_circle_color) {
  circle(N(0), N(1), N(2), (uint32_t)(long long)N(3) & 0xFFFFFF, (uint32_t)(long long)N(4) & 0xFFFFFF, T(a[5]));
  return 0;
}
FN(draw_triangle_color) {
  uint32_t c1 = (uint32_t)(long long)N(6) & 0xFFFFFF, c2 = (uint32_t)(long long)N(7) & 0xFFFFFF, c3 = (uint32_t)(long long)N(8) & 0xFFFFFF;
  if (T(a[9])) {
    line_quad(N(0), N(1), N(2), N(3), 1, c1, c2, G->ds.alpha);
    line_quad(N(2), N(3), N(4), N(5), 1, c2, c3, G->ds.alpha);
    line_quad(N(4), N(5), N(0), N(1), 1, c3, c1, G->ds.alpha);
  } else {
    std::vector<SDL_Vertex> v;
    tri(v, vtx(N(0), N(1), c1, G->ds.alpha), vtx(N(2), N(3), c2, G->ds.alpha), vtx(N(4), N(5), c3, G->ds.alpha));
    geom(v);
  }
  return 0;
}
FN(draw_line_width) { line_quad(N(0), N(1), N(2), N(3), N(4), G->ds.color, G->ds.color, G->ds.alpha); return 0; }
FN(draw_line_color) {
  line_quad(N(0), N(1), N(2), N(3), 1, (uint32_t)(long long)N(4) & 0xFFFFFF, (uint32_t)(long long)N(5) & 0xFFFFFF, G->ds.alpha);
  return 0;
}
FN(draw_line_width_color) {
  line_quad(N(0), N(1), N(2), N(3), N(4), (uint32_t)(long long)N(5) & 0xFFFFFF, (uint32_t)(long long)N(6) & 0xFFFFFF, G->ds.alpha);
  return 0;
}

// textured primitives (distortion effect)
static std::vector<SDL_Vertex> prim_v;
static int prim_kind = 4, prim_surf = -1;
FN(draw_primitive_begin_texture) { prim_kind = (int)N(0); prim_surf = (int)N(1); prim_v.clear(); return 0; }
FN(draw_vertex_texture) {
  SDL_Vertex v;
  v.position = {(float)tx(N(0)), (float)ty(N(1))};
  v.color = col(G->ds.color, G->ds.alpha);
  v.tex_coord = {(float)N(2), (float)N(3)};
  prim_v.push_back(v);
  return 0;
}
FN(draw_primitive_end) {
  Surface* s = surf(prim_surf);
  std::vector<SDL_Vertex> out;
  if (prim_kind == 4) out = prim_v;
  else if (prim_kind == 5) {
    for (size_t i = 2; i < prim_v.size(); i++) {
      if (i & 1) tri(out, prim_v[i - 1], prim_v[i - 2], prim_v[i]);
      else tri(out, prim_v[i - 2], prim_v[i - 1], prim_v[i]);
    }
  } else if (prim_kind == 6) {
    for (size_t i = 2; i < prim_v.size(); i++) tri(out, prim_v[0], prim_v[i - 1], prim_v[i]);
  }
  out.resize(out.size() / 3 * 3);
  if (s && !out.empty()) {
    SDL_SetTextureBlendMode(s->tex, blend_mode());
    SDL_SetRenderDrawBlendMode(G->ren, blend_mode());
    (g_ops++, SDL_RenderGeometry)(G->ren, s->tex, out.data(), (int)out.size(), nullptr, 0);
  }
  prim_v.clear();
  return 0;
}

// ------------------------------------------------------------------ text
static std::vector<std::string> split_lines(const std::string& s) {
  std::vector<std::string> out(1);
  for (size_t i = 0; i < s.size(); i++) {
    char c = s[i];
    if (c == '#' && !(i > 0 && s[i - 1] == '\\')) out.emplace_back();
    else if (c == '\\' && i + 1 < s.size() && s[i + 1] == '#') {}
    else if (c == '\r') {}
    else if (c == '\n') out.emplace_back();
    else out.back().push_back(c);
  }
  return out;
}
static double line_w(const Font& f, const std::string& s) {
  double w = 0;
  for (unsigned char ch : s) {
    auto it = f.glyphs.find(ch);
    if (it != f.glyphs.end()) w += it->second.shift;
  }
  return w;
}
static void text_impl(double x, double y, const std::string& str, double xs, double ys, double rot, uint32_t c, double al) {
  int fi = G->ds.font;
  if (fi < 0 || fi >= (int)G->fnt.size()) return;
  ensure_font(fi);
  Font& f = G->fnt[fi];
  if (!f.tex) return;
  auto lines = split_lines(str);
  double lh = f.lineh;
  double totalh = lh * lines.size();
  double ca = std::cos(rot * DEG), sa = std::sin(rot * DEG);
  double oy = G->ds.valign == 1 ? -totalh / 2 : (G->ds.valign == 2 ? -totalh : 0);
  for (size_t li = 0; li < lines.size(); li++) {
    double lw = line_w(f, lines[li]);
    double ox = G->ds.halign == 1 ? -lw / 2 : (G->ds.halign == 2 ? -lw : 0);
    double pen = ox;
    double ly = oy + li * lh;
    for (unsigned char ch : lines[li]) {
      auto it = f.glyphs.find(ch);
      if (it == f.glyphs.end()) continue;
      const Glyph& g = it->second;
      SDL_Rect src = {g.x, g.y, g.w, g.h};
      double gx = (pen + g.offset) * xs, gy = ly * ys;
      double wx = x + gx * ca + gy * sa, wy = y - gx * sa + gy * ca;
      draw_tex(f.tex, 0, 0, &src, wx, wy, 0, 0, xs, ys, rot, c, al);
      pen += g.shift;
    }
  }
}
FN(draw_text) { text_impl(N(0), N(1), Str(a[2]), 1, 1, 0, G->ds.color, G->ds.alpha); return 0; }
FN(draw_text_transformed) { text_impl(N(0), N(1), Str(a[2]), N(3), N(4), N(5), G->ds.color, G->ds.alpha); return 0; }
FN(draw_text_color) {
  text_impl(N(0), N(1), Str(a[2]), 1, 1, 0, (uint32_t)(long long)N(3) & 0xFFFFFF, N(7));
  return 0;
}
FN(draw_text_colour) {
  text_impl(N(0), N(1), Str(a[2]), 1, 1, 0, (uint32_t)(long long)N(3) & 0xFFFFFF, N(7));
  return 0;
}
FN(string_width) {
  int fi = G->ds.font;
  if (fi < 0 || fi >= (int)G->fnt.size()) return 0;
  Font& f = G->fnt[fi];
  double w = 0;
  for (auto& l : split_lines(Str(a[0]))) w = std::max(w, line_w(f, l));
  return w;
}
FN(string_height) {
  int fi = G->ds.font;
  if (fi < 0 || fi >= (int)G->fnt.size()) return 0;
  return (double)(G->fnt[fi].lineh * split_lines(Str(a[0])).size());
}
FN(mp_grid_draw) { return 0; }
