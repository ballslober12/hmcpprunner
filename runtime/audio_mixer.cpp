// Audio through SDL_mixer (like the cube2005 port): samples are Mix_Chunks, music streams from disk.
// This is the backend for the NetCast build: the TV has ~700 MB of RAM and one slow core, so whole-file
// decoded OGG tracks (audio.cpp) are not an option there.
#include "game.h"
#include <SDL_mixer.h>
#include <atomic>
#include <mutex>

static std::recursive_mutex amtx;
static const int STREAM_BASE = 200000;
static const int HANDLE_BASE = 1000000;
static const int MAXCH = 32;

struct MVoice { int handle = 0, snd = -1, channel = -1; bool music = false, active = false; float vol = 1, base = 1; };
static std::vector<MVoice> mv;
static std::vector<Mix_Chunk*> chunks;                 // per sound id
static std::vector<std::string> stream_paths;
static std::vector<Mix_Music*> stream_music;
static std::vector<Mix_Chunk*> stream_chunk;           // fallback when the single music slot is busy
static std::atomic<bool> ch_done[MAXCH];
static int music_handle = 0, music_snd = -1;

static void on_channel_finished(int ch) { if (ch >= 0 && ch < MAXCH) ch_done[ch] = true; }
static void on_music_finished() { music_snd = -1; }

// called with amtx held
static void reap() {
  for (auto& v : mv) {
    if (!v.active) continue;
    if (v.music) { if (!Mix_PlayingMusic() || v.handle != music_handle) v.active = false; }
    else if (v.channel >= 0 && ch_done[v.channel].load()) v.active = false;
  }
}
static MVoice& new_voice() {
  for (auto& v : mv) if (!v.active) return v;
  mv.emplace_back();
  return mv.back();
}

void audio_init() {
  int rate = getenv("HM_AUDIO_RATE") ? atoi(getenv("HM_AUDIO_RATE")) : 44100;
  int buf = getenv("HM_AUDIO_BUF") ? atoi(getenv("HM_AUDIO_BUF")) : 2048;
  Mix_Init(MIX_INIT_OGG);
  if (Mix_OpenAudio(rate, MIX_DEFAULT_FORMAT, 2, buf) < 0) { fprintf(stderr, "audio unavailable (SDL_mixer): %s\n", Mix_GetError()); G->adev = 0; return; }
  G->adev = 1;
  Mix_AllocateChannels(MAXCH);
  for (auto& d : ch_done) d = true;
  Mix_ChannelFinished(on_channel_finished);
  Mix_HookMusicFinished(on_music_finished);
}

void ensure_sound(int s) {
  if ((int)chunks.size() < (int)G->snd.size()) chunks.resize(G->snd.size(), nullptr);
  Sound& sd = G->snd[s];
  if (sd.loaded) return;
  sd.loaded = true;
  std::string path = G->root + "/sound/audio/" + sd.file;
  chunks[s] = Mix_LoadWAV(path.c_str());
  if (!chunks[s]) fprintf(stderr, "cannot load sound %s: %s\n", path.c_str(), Mix_GetError());
}

static int play_chunk(Mix_Chunk* c, int snd, bool loop, float vol, float base) {
  int ch = Mix_PlayChannel(-1, c, loop ? -1 : 0);
  if (ch < 0 || ch >= MAXCH) return -1;
  ch_done[ch] = false;
  Mix_Volume(ch, (int)(std::min(1.0f, vol * base) * MIX_MAX_VOLUME));
  MVoice& v = new_voice();
  v = MVoice();
  v.snd = snd; v.channel = ch; v.active = true; v.vol = vol; v.base = base; v.handle = HANDLE_BASE + G->next_voice++;
  return v.handle;
}

int audio_play(int snd, bool loop, float vol) {
  if (!G->adev) return -1;
  std::lock_guard<std::recursive_mutex> lk(amtx);
  reap();
  if (snd >= STREAM_BASE) {
    int si = snd - STREAM_BASE;
    if (si < 0 || si >= (int)stream_paths.size()) return -1;
    if (!Mix_PlayingMusic()) {
      if (!stream_music[si]) stream_music[si] = Mix_LoadMUS(stream_paths[si].c_str());
      if (!stream_music[si]) { fprintf(stderr, "cannot open music %s: %s\n", stream_paths[si].c_str(), Mix_GetError()); return -1; }
      if (Mix_PlayMusic(stream_music[si], loop ? -1 : 1) < 0) return -1;
      Mix_VolumeMusic((int)(std::min(1.0f, vol) * MIX_MAX_VOLUME));
      MVoice& v = new_voice();
      v = MVoice();
      v.snd = snd; v.music = true; v.active = true; v.vol = vol; v.handle = HANDLE_BASE + G->next_voice++;
      music_handle = v.handle; music_snd = snd;
      return v.handle;
    }
    // music slot busy: decode this one as a sample
    if (!stream_chunk[si]) stream_chunk[si] = Mix_LoadWAV(stream_paths[si].c_str());
    if (!stream_chunk[si]) return -1;
    return play_chunk(stream_chunk[si], snd, loop, vol, 1.0f);
  }
  if (snd < 0 || snd >= (int)G->snd.size()) return -1;
  ensure_sound(snd);
  if (!chunks[snd]) return -1;
  return play_chunk(chunks[snd], snd, loop, vol, (float)G->snd[snd].volume);
}

void audio_stop_voice(int handle) {
  std::lock_guard<std::recursive_mutex> lk(amtx);
  for (auto& v : mv) if (v.active && v.handle == handle) {
    v.active = false;
    if (v.music) Mix_HaltMusic(); else Mix_HaltChannel(v.channel);
  }
}
void audio_stop_snd(int snd) {
  std::lock_guard<std::recursive_mutex> lk(amtx);
  for (auto& v : mv) if (v.active && v.snd == snd) {
    v.active = false;
    if (v.music) Mix_HaltMusic(); else Mix_HaltChannel(v.channel);
  }
}
bool audio_playing(int x) {
  std::lock_guard<std::recursive_mutex> lk(amtx);
  reap();
  for (auto& v : mv) if (v.active && (v.handle == x || v.snd == x)) return true;
  return false;
}
void audio_gain(int x, float vol) {
  std::lock_guard<std::recursive_mutex> lk(amtx);
  for (auto& v : mv) if (v.active && (v.handle == x || v.snd == x)) {
    v.vol = vol;
    int iv = (int)(std::max(0.0f, std::min(1.0f, vol * v.base)) * MIX_MAX_VOLUME);
    if (v.music) Mix_VolumeMusic(iv); else Mix_Volume(v.channel, iv);
  }
}
int audio_play_file(const std::string&, bool, float) { return -1; }

#define FN(n) Value F_##n(Inst* self, Inst* other, Args a)
#define N(i) Num(a[i])

FN(audio_play_sound) { return audio_play((int)N(0), T(a[2]), 1.0f); }
FN(sound_play) { return audio_play((int)N(0), false, 1.0f); }
FN(audio_stop_sound) {
  int x = (int)N(0);
  if (x >= HANDLE_BASE) audio_stop_voice(x); else audio_stop_snd(x);
  return 0;
}
FN(audio_is_playing) { return audio_playing((int)N(0)) ? 1 : 0; }
FN(audio_sound_gain) { audio_gain((int)N(0), (float)N(1)); return 0; }
FN(audio_create_stream) {
  stream_paths.push_back(gm_path(Str(a[0])));
  stream_music.push_back(nullptr);
  stream_chunk.push_back(nullptr);
  return (double)(STREAM_BASE + stream_paths.size() - 1);
}
FN(audio_destroy_stream) {
  int si = (int)N(0) - STREAM_BASE;
  if (si < 0 || si >= (int)stream_paths.size()) return 0;
  audio_stop_snd((int)N(0));
  std::lock_guard<std::recursive_mutex> lk(amtx);
  if (stream_music[si]) { Mix_FreeMusic(stream_music[si]); stream_music[si] = nullptr; }
  if (stream_chunk[si]) { Mix_FreeChunk(stream_chunk[si]); stream_chunk[si] = nullptr; }
  return 0;
}
FN(sound_exists) { int s = (int)N(0); return s >= 0 && s < (int)G->snd.size() ? 1 : 0; }
FN(sound_get_name) { int s = (int)N(0); return s >= 0 && s < SND__COUNT ? Value(g_snd_names[s]) : Value("<undefined>"); }
