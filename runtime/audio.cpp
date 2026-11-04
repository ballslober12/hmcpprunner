#include "game.h"
#include <atomic>
#include <mutex>
#include <thread>

extern "C" {
#include "stb_vorbis.h"
}

static std::mutex amtx;
static const int RATE = 44100;
static std::vector<std::string> stream_paths;
static std::vector<std::shared_ptr<std::vector<int16_t>>> stream_pcm;
static const int STREAM_BASE = 200000;
static const int HANDLE_BASE = 1000000;

static void mix_cb(void*, Uint8* out, int len) {
  int16_t* o = (int16_t*)out;
  int frames = len / 4;
  memset(out, 0, len);
  std::lock_guard<std::mutex> lk(amtx);
  for (auto& v : G->voices) {
    if (!v.active || !v.data || v.frames == 0) continue;
    for (int f = 0; f < frames; f++) {
      if (v.pos >= v.frames) {
        if (v.loop) v.pos = 0; else { v.active = false; break; }
      }
      int l = o[f * 2] + (int)(v.data[v.pos * 2] * v.vol);
      int r = o[f * 2 + 1] + (int)(v.data[v.pos * 2 + 1] * v.vol);
      o[f * 2] = (int16_t)std::max(-32768, std::min(32767, l));
      o[f * 2 + 1] = (int16_t)std::max(-32768, std::min(32767, r));
      v.pos++;
    }
  }
}

void audio_init() {
  SDL_AudioSpec want, have;
  SDL_zero(want);
  want.freq = RATE; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = mix_cb;
  G->adev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
  if (G->adev) SDL_PauseAudioDevice(G->adev, 0);
  else fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
}

void ensure_sound(int s) {
  Sound& sd = G->snd[s];
  if (sd.loaded) return;
  sd.loaded = true;
  std::string path = G->root + "/sound/audio/" + sd.file;
  SDL_AudioSpec spec;
  Uint8* buf; Uint32 len;
  if (!SDL_LoadWAV(path.c_str(), &spec, &buf, &len)) { fprintf(stderr, "cannot load sound %s\n", path.c_str()); return; }
  SDL_AudioCVT cvt;
  if (SDL_BuildAudioCVT(&cvt, spec.format, spec.channels, spec.freq, AUDIO_S16SYS, 2, RATE) < 0) { SDL_FreeWAV(buf); return; }
  cvt.len = (int)len;
  std::vector<uint8_t> tmp((size_t)len * cvt.len_mult);
  memcpy(tmp.data(), buf, len);
  cvt.buf = tmp.data();
  SDL_ConvertAudio(&cvt);
  sd.pcm.resize(cvt.len_cvt / 2);
  memcpy(sd.pcm.data(), tmp.data(), cvt.len_cvt);
  SDL_FreeWAV(buf);
}

static SoundVoice& new_voice() {
  for (auto& v : G->voices) if (!v.active && v.snd == -2) { return v; }
  for (auto& v : G->voices) if (!v.active) return v;
  G->voices.emplace_back();
  return G->voices.back();
}

int audio_play(int snd, bool loop, float vol) {
  if (snd >= STREAM_BASE) {
    int si = snd - STREAM_BASE;
    if (si < 0 || si >= (int)stream_paths.size()) return -1;
    std::lock_guard<std::mutex> lk(amtx);
    SoundVoice& v = new_voice();
    v = SoundVoice();
    v.snd = snd; v.loop = loop; v.vol = vol; v.handle = HANDLE_BASE + G->next_voice++;
    int h = v.handle;
    if (stream_pcm[si]) {
      v.own = stream_pcm[si]; v.data = v.own->data(); v.frames = v.own->size() / 2; v.active = true;
    } else {
      v.active = true; v.data = nullptr; v.frames = 0;
      std::string path = stream_paths[si];
      std::thread([si, path, h]() {
        int ch, rate;
        short* out;
        int n = stb_vorbis_decode_filename(path.c_str(), &ch, &rate, &out);
        if (n <= 0) { fprintf(stderr, "cannot decode %s\n", path.c_str()); return; }
        auto pcm = std::make_shared<std::vector<int16_t>>();
        // convert to stereo 44100 (linear resample)
        size_t outn = (size_t)((double)n * RATE / rate);
        pcm->resize(outn * 2);
        for (size_t i = 0; i < outn; i++) {
          double src = (double)i * rate / RATE;
          size_t i0 = (size_t)src; double fr = src - i0;
          size_t i1 = std::min((size_t)n - 1, i0 + 1);
          for (int c = 0; c < 2; c++) {
            int cc = ch == 1 ? 0 : c;
            double a = out[i0 * ch + cc], b = out[i1 * ch + cc];
            (*pcm)[i * 2 + c] = (int16_t)(a + (b - a) * fr);
          }
        }
        free(out);
        std::lock_guard<std::mutex> lk2(amtx);
        stream_pcm[si] = pcm;
        for (auto& v2 : G->voices)
          if (v2.handle == h && v2.active) { v2.own = pcm; v2.data = pcm->data(); v2.frames = pcm->size() / 2; }
      }).detach();
    }
    return h;
  }
  if (snd < 0 || snd >= (int)G->snd.size()) return -1;
  ensure_sound(snd);
  Sound& sd = G->snd[snd];
  if (sd.pcm.empty()) return -1;
  std::lock_guard<std::mutex> lk(amtx);
  SoundVoice& v = new_voice();
  v = SoundVoice();
  v.snd = snd; v.data = sd.pcm.data(); v.frames = sd.pcm.size() / 2; v.pos = 0;
  v.vol = vol * (float)sd.volume; v.loop = loop; v.active = true; v.handle = HANDLE_BASE + G->next_voice++;
  return v.handle;
}
void audio_stop_voice(int handle) {
  std::lock_guard<std::mutex> lk(amtx);
  for (auto& v : G->voices) if (v.handle == handle) v.active = false;
}
void audio_stop_snd(int snd) {
  std::lock_guard<std::mutex> lk(amtx);
  for (auto& v : G->voices) if (v.active && v.snd == snd) v.active = false;
}
bool audio_playing(int x) {
  std::lock_guard<std::mutex> lk(amtx);
  for (auto& v : G->voices) if (v.active && (v.handle == x || v.snd == x)) return true;
  return false;
}
void audio_gain(int x, float vol) {
  std::lock_guard<std::mutex> lk(amtx);
  for (auto& v : G->voices) if (v.active && (v.handle == x || v.snd == x)) {
    float base = 1;
    if (v.snd >= 0 && v.snd < (int)G->snd.size()) base = (float)G->snd[v.snd].volume;
    v.vol = vol * base;
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
  std::string p = gm_path(Str(a[0]));
  stream_paths.push_back(p);
  stream_pcm.push_back(nullptr);
  return (double)(STREAM_BASE + stream_paths.size() - 1);
}
FN(audio_destroy_stream) {
  int si = (int)N(0) - STREAM_BASE;
  if (si < 0 || si >= (int)stream_paths.size()) return 0;
  audio_stop_snd((int)N(0));
  std::lock_guard<std::mutex> lk(amtx);
  stream_pcm[si].reset();
  return 0;
}
FN(sound_exists) { int s = (int)N(0); return s >= 0 && s < (int)G->snd.size() ? 1 : 0; }
FN(sound_get_name) { int s = (int)N(0); return s >= 0 && s < SND__COUNT ? Value(g_snd_names[s]) : Value("<undefined>"); }
