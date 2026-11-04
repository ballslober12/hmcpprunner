#include "game.h"
#include "pad.h"
#include <filesystem>
#include <cstring>
namespace fs = std::filesystem;

void game_init_video() {
  if (G->headless) {
    G->win = SDL_CreateWindow("Hotline Miami", 0, 0, G->fbw, G->fbh, SDL_WINDOW_HIDDEN);
    G->ren = SDL_CreateRenderer(G->win, -1, getenv("HM_GPU") ? (SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE) : (SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE));
  } else {
    G->win = SDL_CreateWindow("Hotline Miami", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, G->winw, G->winh, SDL_WINDOW_RESIZABLE);
    G->ren = SDL_CreateRenderer(G->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);
    if (!G->ren) G->ren = SDL_CreateRenderer(G->win, -1, SDL_RENDERER_SOFTWARE | SDL_RENDERER_TARGETTEXTURE);
  }
  if (!G->win || !G->ren) { fprintf(stderr, "video init failed: %s\n", SDL_GetError()); exit(1); }
  G->frame = SDL_CreateTexture(G->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, G->rw, G->rh);
  SDL_ShowCursor(SDL_DISABLE);
}

static void prepare_workdir(const std::string& root, const std::string& work) {
  fs::create_directories(work);
  fs::path src = fs::path(root) / "datafiles";
  if (!fs::exists(src)) return;
  for (auto& e : fs::recursive_directory_iterator(src)) {
    fs::path rel = fs::relative(e.path(), src);
    fs::path dst = fs::path(work) / rel;
    if (e.is_directory()) { fs::create_directories(dst); continue; }
    if (fs::exists(dst) || fs::is_symlink(dst)) continue;
    std::string ext = e.path().extension().string();
    if (ext == ".dll" || ext == ".ogg" || ext == ".wav") continue;  // dlls are Windows-only; audio is read from the assets (gm_path)
    std::error_code ec;
    if (e.file_size() > 1 << 20) fs::create_symlink(fs::absolute(e.path()), dst, ec);
    if (!e.file_size() || e.file_size() <= (1 << 20) || ec) { ec.clear(); fs::copy_file(e.path(), dst, fs::copy_options::overwrite_existing, ec); }
  }
}

// The game switches to its XInput code path when a file named 'xbox' exists in the working directory.
// Create it automatically when a gamepad is present (NetCast: the hotplug callback may take a moment).
static void prepare_pad_mode(int autopad) {
  fs::path flag = fs::path(G->workdir) / "xbox";
  if (autopad == 0 || fs::exists(flag)) return;
  bool want = autopad == 1;
  pad_update(0);
  if (pad_connected(0)) want = true;
#ifdef HM_NC4
  // the LGNC hotplug callback arrives asynchronously: give it a moment (the pad must send its first event or hotplug)
  for (int i = 0; i < 150 && !want; i++) { SDL_Delay(10); if (pad_connected(0)) want = true; }
#endif
  if (want) { FILE* f = fopen(flag.string().c_str(), "w"); if (f) fclose(f); }
}

int main(int argc, char** argv) {
  std::string root = "assets", work = "hm_work", dat = "data/game.dat";
  int frames = -1, startroom = 0;
  bool headless = false;
  std::string shot;
  int resw = 0, resh = 0, autopad = -1;
#ifdef HM_NC4
  resw = 960; resh = 540;  // the TV's GPU/CPU can't afford 1366x768; the GPU scales the frame up to the 1280x720 window
#endif
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--assets") && i + 1 < argc) root = argv[++i];
    else if (!strcmp(argv[i], "--work") && i + 1 < argc) work = argv[++i];
    else if (!strcmp(argv[i], "--data") && i + 1 < argc) dat = argv[++i];
    else if (!strcmp(argv[i], "--headless")) headless = true;
    else if (!strcmp(argv[i], "--frames") && i + 1 < argc) frames = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--room") && i + 1 < argc) startroom = atoi(argv[++i]);
    else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
    else if (!strcmp(argv[i], "--res") && i + 1 < argc) { if (sscanf(argv[++i], "%dx%d", &resw, &resh) != 2) resw = resh = 0; }
    else if (!strcmp(argv[i], "--pad")) autopad = 1;
    else if (!strcmp(argv[i], "--no-pad")) autopad = 0;
    else if (!strncmp(argv[i], "--service", 9) || !strncmp(argv[i], "--auid", 6)) { if (i + 1 < argc) i++; }  // NetCast app manager arguments (SDL's nc4 driver reads them itself)
    else { fprintf(stderr, "usage: %s --assets <GameMaker Studio 1.4 dir> [--work dir] [--data game.dat]\n", argv[0]); return 1; }
  }
  if (const char* e = getenv("HM_RES")) { if (sscanf(e, "%dx%d", &resw, &resh) != 2) resw = resh = 0; }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, resw > 0 && resw != 1366 ? "1" : "0");  // smooth when the frame is downscaled
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
  G = new Game();
  G->headless = headless;
  if (resw > 0 && resh > 0) { G->rw = resw; G->rh = resh; G->rscale = (double)resw / G->fbw; }
  G->root = fs::path(root).generic_string();  // forward slashes everywhere (Windows too)
  SDL_SetMainReady();
  prepare_workdir(root, work);
  G->workdir = fs::absolute(work).generic_string();
  G->gv.resize(VAR_COUNT + 1);
  load_game_data(dat);
  G->spr.reserve(SPR__COUNT + 4096);
  game_init_video();
  audio_init();
  pad_init();
  prepare_pad_mode(autopad);
  G->gb[GB_working_directory][0] = Value(G->workdir + "/");
  G->gb[GB_program_directory][0] = Value(G->workdir + "/");
  G->gb[GB_temp_directory][0] = Value(G->workdir + "/");
  G->gb[GB_room_speed][0] = 60.0;
  G->gb[GB_os_type][0] = 5.0;
  G->gb[GB_game_id][0] = 762421906.0;
  G->gb[GB_view_enabled][0] = 1.0;
  start_game(0);
  if (startroom != 0) {
    // pass through the main menu first so global state (key bindings, unlocks) is initialised like in a real run
    room_goto_index(G->rm_by_name["rmMenu"]);
    for (int k = 0; k < 4; k++) run_frame();
    room_goto_index(startroom);
  }
  int n = 0;
  if (frames >= 0) {
    while (!G->quit && n < frames) { run_frame(); n++; }
    if (!shot.empty()) {
      SDL_SetRenderTarget(G->ren, G->frame);
      std::vector<uint8_t> px((size_t)G->rw * G->rh * 4);
      if (SDL_RenderReadPixels(G->ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), G->rw * 4) != 0) fprintf(stderr, "readpixels: %s\n", SDL_GetError());
      extern int save_png_rgba(const char*, int, int, const void*);
      save_png_rgba(shot.c_str(), G->rw, G->rh, px.data());
    }
    { extern void draw_debug_report(); draw_debug_report(); }
    if (getenv("HM_DUMP_SURFACES")) {
      for (size_t i = 0; i < G->surfaces.size(); i++) {
        if (!G->surfaces[i].used) continue;
        Surface& sf = G->surfaces[i];
        SDL_SetRenderTarget(G->ren, sf.tex);
        std::vector<uint8_t> px((size_t)sf.w * sf.h * 4);
        SDL_RenderReadPixels(G->ren, nullptr, SDL_PIXELFORMAT_ABGR8888, px.data(), sf.w * 4);
        size_t nz = 0;
        for (size_t k = 3; k < px.size(); k += 4) if (px[k]) nz++;
        printf("surface %zu %dx%d opaque_px=%zu\n", i, sf.w, sf.h, nz);
        if (getenv("HM_DUMP_SURFACES")[0] == '2') {
          extern int save_png_rgba(const char*, int, int, const void*);
          save_png_rgba((std::string("surf") + std::to_string(i) + ".png").c_str(), sf.w, sf.h, px.data());
        }
      }
    }
    if (const char* ob = getenv("HM_PRINT_OBJ")) {
      std::string list = ob; size_t p0 = 0;
      while (p0 < list.size()) {
        size_t q = list.find(',', p0); if (q == std::string::npos) q = list.size();
        std::string nm = list.substr(p0, q - p0); p0 = q + 1;
        auto it = G->obj_by_name.find(nm);
        if (it == G->obj_by_name.end()) { printf("no object %s\n", nm.c_str()); continue; }
        int n = 0;
        for (int o : G->desc[it->second]) for (Inst* i : G->byobj[o]) {
          if (!i->alive) continue;
          if (n++ < 3) {
            printf("%s#%d x=%.1f y=%.1f spd=%.2f dir=%.0f spr=%s idx=%.1f vars:", g_objs[i->obj].name, i->id, i->x, i->y, i->speed, i->direction,
                   i->sprite_index >= 0 && i->sprite_index < SPR__COUNT ? g_spr_names[i->sprite_index] : "-", i->image_index);
            int shown = 0;
            for (auto& kv : i->vars) { if (shown++ >= 80) break; printf(" %s=%s", g_var_names[kv.first], Str(kv.second).c_str()); }
            printf("\n");
          }
        }
        printf("  [%s count=%d]\n", nm.c_str(), n);
      }
    }
    if (const char* ob = getenv("HM_COLLIDE")) {
      auto it = G->obj_by_name.find(ob);
      if (it != G->obj_by_name.end()) {
        for (int o : G->desc[it->second]) for (Inst* i : G->byobj[o]) {
          double l, t, r, b; bbox_of(i, i->x, i->y, l, t, r, b);
          printf("%s#%d at (%.0f,%.0f) bbox=[%.0f,%.0f,%.0f,%.0f] mask=%d spr=%s\n", g_objs[i->obj].name, i->id, i->x, i->y, l, t, r, b, i->mask_index,
                 i->sprite_index >= 0 ? g_spr_names[i->sprite_index] : "-");
          for (Inst* c : G->insts) {
            if (c == i || !c->alive || !c->solid) continue;
            if (inst_meets(i, i->x + 3, i->y, c)) {
              double l2, t2, r2, b2; bbox_of(c, c->x, c->y, l2, t2, r2, b2);
              printf("  blocks +3: %s#%d at (%.0f,%.0f) bbox=[%.0f,%.0f,%.0f,%.0f] spr=%s mask=%d\n", g_objs[c->obj].name, c->id, c->x, c->y, l2, t2, r2, b2,
                     c->sprite_index >= 0 ? g_spr_names[c->sprite_index] : "-", c->mask_index);
            }
          }
        }
      }
    }
    if (const char* gl = getenv("HM_PRINT_GLOBALS")) {
      std::string list = gl; size_t p0 = 0;
      while (p0 < list.size()) {
        size_t q = list.find(',', p0); if (q == std::string::npos) q = list.size();
        std::string nm = list.substr(p0, q - p0); p0 = q + 1;
        for (int v = 0; v < VAR_COUNT; v++) if (nm == g_var_names[v]) printf("global.%s = '%s'\n", nm.c_str(), Str(GV(v)).c_str());
      }
    }
    printf("ran %d frames, room=%s, instances=%zu\n", n, g_rm_names[G->room], G->insts.size());
  } else {
    engine_run();
  }
  SDL_Quit();
  return 0;
}
