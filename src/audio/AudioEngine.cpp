#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"
#include "audio/AudioEngine.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <regex>

namespace og {

const char* instrumentName(InstrumentId id) {
  switch (id) {
    case InstrumentId::Piano: return "Piano";
    case InstrumentId::Violin: return "Violin";
    case InstrumentId::Cello: return "Cello";
    case InstrumentId::Flute: return "Flute";
    case InstrumentId::Trumpet: return "Trumpet";
    case InstrumentId::Timpani: return "Timpani";
    default: return "Unknown";
  }
}

namespace {

constexpr float kTau = 6.28318530718f;
constexpr int kVoices = 64;
constexpr int kPartials = 12;
constexpr int kStreams = 16;

float midiToHz(float n) { return 440.f * std::pow(2.f, (n - 69.f) / 12.f); }

// ------------------------------------------------------------------ Freeverb
struct Comb {
  std::vector<float> b; size_t i = 0; float store = 0.f;
  float process(float in, float fb, float damp) {
    float o = b[i];
    store = o * (1.f - damp) + store * damp;
    b[i] = in + store * fb;
    i = (i + 1) % b.size();
    return o;
  }
};
struct Allpass {
  std::vector<float> b; size_t i = 0;
  float process(float in) {
    float bo = b[i];
    float o = -in + bo;
    b[i] = in + bo * 0.5f;
    i = (i + 1) % b.size();
    return o;
  }
};
struct Freeverb {
  Comb cl[8], cr[8];
  Allpass al[4], ar[4];
  float room = 0.86f, damp = 0.32f;
  void init(int rate) {
    const int combT[8] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
    const int apT[4] = {556, 441, 341, 225};
    float s = rate / 44100.f;
    for (int k = 0; k < 8; ++k) {
      cl[k].b.assign((size_t)(combT[k] * s), 0.f);
      cr[k].b.assign((size_t)((combT[k] + 23) * s), 0.f);
    }
    for (int k = 0; k < 4; ++k) {
      al[k].b.assign((size_t)(apT[k] * s), 0.f);
      ar[k].b.assign((size_t)((apT[k] + 23) * s), 0.f);
    }
  }
  void process(float in, float& L, float& R) {
    in *= 0.015f;
    L = R = 0.f;
    for (int k = 0; k < 8; ++k) { L += cl[k].process(in, room, damp); R += cr[k].process(in, room, damp); }
    for (int k = 0; k < 4; ++k) { L = al[k].process(L); R = ar[k].process(R); }
  }
};

// ------------------------------------------------------------------ samples
struct Sample {
  int root = 60;
  std::vector<float> data;  // mono 48 kHz
};

int parseNote(const std::string& stem) {
  // note names like C4, Cs4, C#4, Bb2 (upper-case letter so words in file names don't match)
  static const std::regex nameRe(R"(([A-G])(#|s|b)?(\d))");
  std::smatch m;
  if (std::regex_search(stem, m, nameRe)) {
    static const int base[7] = {9, 11, 0, 2, 4, 5, 7};  // A B C D E F G
    int n = base[m[1].str()[0] - 'A'];
    if (m[2].matched) n += (m[2].str() == "b") ? -1 : 1;
    return n + (std::stoi(m[3].str()) + 1) * 12;
  }
  static const std::regex numRe(R"((\d{2,3}))");
  if (std::regex_search(stem, m, numRe)) {
    int v = std::stoi(m[1].str());
    if (v >= 21 && v <= 108) return v;
  }
  return -1;
}

// ------------------------------------------------------------------ voices
struct Voice {
  bool active = false;
  bool released = false;
  InstrumentId inst = InstrumentId::Piano;
  int note = 60;
  uint32_t source = 0;
  float vel = 0.8f;
  float freq = 440.f;
  double age = 0.0;       // seconds since note on
  float env = 0.f;        // release envelope multiplier
  float relRate = 0.f;
  double phase[kPartials] = {};
  float noiseLP = 0.f;
  uint32_t rng = 1;
  const Sample* sample = nullptr;
  double spos = 0.0, sstep = 1.0;
  float pan = 0.5f;
  uint64_t order = 0;
};

float whiteNoise(uint32_t& s) {
  s ^= s << 13; s ^= s >> 17; s ^= s << 5;
  return (s & 0xffffff) / 8388608.f - 1.f;
}

// Additive models shaped after each instrument's spectrum and envelope.
float synth(Voice& v, float dt) {
  float t = (float)v.age;
  float f = v.freq;
  float s = 0.f;
  float vel = v.vel;
  auto partial = [&](int n, float ratio, float amp) {
    if (f * ratio > 15000.f || amp < 1e-5f) return;
    v.phase[n] += f * ratio * dt;
    v.phase[n] -= std::floor(v.phase[n]);
    s += amp * std::sin(kTau * (float)v.phase[n]);
  };
  switch (v.inst) {
    case InstrumentId::Piano: {
      float B = 0.00035f;
      float bright = 0.4f + 0.6f * vel;
      for (int n = 1; n <= 10; ++n) {
        float ratio = n * std::sqrt(1.f + B * n * n);
        float decay = (0.55f + 0.38f * n) * std::pow(f / 261.6f, 0.35f);
        float amp = std::pow((float)n, -1.4f + 0.6f * bright) * std::exp(-t * decay);
        partial(n - 1, ratio, amp);
      }
      float attack = std::min(1.f, t / 0.002f);
      float hammer = whiteNoise(v.rng) * std::exp(-t * 90.f) * 0.15f * vel;
      return (s * 0.32f * attack + hammer) * vel;
    }
    case InstrumentId::Violin:
    case InstrumentId::Cello: {
      bool cello = v.inst == InstrumentId::Cello;
      float vib = t > 0.25f ? std::sin(kTau * 5.6f * t) * 0.0045f * std::min(1.f, (t - 0.25f) * 3.f) : 0.f;
      float fv = 1.f + vib;
      float formant = cello ? 300.f : 900.f;
      for (int n = 1; n <= kPartials; ++n) {
        float fn = f * n;
        float body = 1.f / (1.f + std::pow((fn - formant) / (formant * 1.4f), 2.f)) + 0.35f;
        partial(n - 1, n * fv, body / n);
      }
      float attack = 1.f - std::exp(-t / (cello ? 0.07f : 0.05f));
      v.noiseLP += (whiteNoise(v.rng) - v.noiseLP) * 0.15f;
      float bow = v.noiseLP * 0.06f * (0.5f + 0.5f * attack);
      return (s * 0.2f * attack + bow) * (0.5f + 0.5f * vel);
    }
    case InstrumentId::Flute: {
      float vib = t > 0.2f ? std::sin(kTau * 5.0f * t) * 0.004f : 0.f;
      partial(0, 1.f + vib, 1.f);
      partial(1, 2.f + vib, 0.22f);
      partial(2, 3.f + vib, 0.08f);
      partial(3, 4.f + vib, 0.03f);
      float attack = 1.f - std::exp(-t / 0.05f);
      v.noiseLP += (whiteNoise(v.rng) - v.noiseLP) * 0.3f;
      float breath = v.noiseLP * (0.08f + 0.12f * std::exp(-t * 10.f));
      return (s * 0.3f * attack + breath * 0.5f) * (0.5f + 0.5f * vel);
    }
    case InstrumentId::Trumpet: {
      float attack = 1.f - std::exp(-t / 0.03f);
      float bright = std::min(1.f, attack * (0.5f + vel));
      for (int n = 1; n <= kPartials; ++n) partial(n - 1, (float)n, std::pow((float)n, -2.2f + 1.4f * bright));
      return s * 0.22f * attack * (0.45f + 0.55f * vel);
    }
    case InstrumentId::Timpani: {
      float drop = 1.f + 0.03f * std::exp(-t * 25.f);
      const float ratios[5] = {1.f, 1.504f, 1.742f, 2.0f, 2.245f};
      const float amps[5] = {1.f, 0.55f, 0.35f, 0.25f, 0.18f};
      for (int n = 0; n < 5; ++n) partial(n, ratios[n] * drop, amps[n] * std::exp(-t * (0.8f + n * 0.9f)));
      v.noiseLP += (whiteNoise(v.rng) - v.noiseLP) * 0.05f;
      float thump = v.noiseLP * std::exp(-t * 30.f) * 2.5f;
      return (s * 0.45f + thump) * vel;
    }
    default: return 0.f;
  }
}

float releaseTime(InstrumentId id) {
  switch (id) {
    case InstrumentId::Piano: return 0.35f;
    case InstrumentId::Timpani: return 1.2f;
    case InstrumentId::Violin: return 0.18f;
    case InstrumentId::Cello: return 0.25f;
    default: return 0.12f;
  }
}

float maxDuration(InstrumentId id) {
  switch (id) {
    case InstrumentId::Piano: return 7.f;
    case InstrumentId::Timpani: return 5.f;
    default: return 12.f;
  }
}

struct Event {
  enum Type { On, Off, AllOff } type;
  InstrumentId inst;
  int note;
  float vel;
  uint32_t source;
  uint64_t at;  // sample clock; 0 = immediately
};

struct Stream {
  bool used = false;
  uint32_t player = 0;
  StreamKind kind = StreamKind::Voice;
  Ring<int16_t> ring{AudioEngine::kNetRate * 2};
  bool primed = false;
  float last = 0.f, next = 0.f;
  double frac = 1.0;
  float level = 0.f;
};

}  // namespace

struct AudioEngine::Impl {
  ma_device device{};
  std::vector<Sample> samples[(int)InstrumentId::Count];
  Voice voices[kVoices];
  uint64_t voiceOrder = 0;

  std::mutex evMutex;
  std::vector<Event> incoming;
  std::vector<Event> pending;  // audio thread only

  Freeverb room, localRoom;

  std::mutex outMutex;
  Ring<int16_t> outVoice{kNetRate * 2}, outSong{kNetRate * 2};
  float decimVoice = 0.f, decimSong = 0.f;
  int decimPhase = 0;

  std::mutex streamMutex;
  Stream streams[kStreams];

  std::mutex tapMutex;
  bool tapOn = false;
  Ring<float> tap{kRate * 2 * 4};

  // song file analysis
  float songEnv = 0.f, energyAvg = 0.f;
  int beatHold = 0;
  float micEnv = 0.f, micDc = 0.f;
};

AudioEngine::AudioEngine() : impl_(std::make_unique<Impl>()) {}
AudioEngine::~AudioEngine() { shutdown(); }

static void dataCallback(ma_device* d, void* out, const void* in, ma_uint32 frames) {
  auto* self = (AudioEngine*)d->pUserData;
  self->process((float*)out, (const float*)in, frames);
}

bool AudioEngine::init(const std::string& sampleRoot) {
  auto& I = *impl_;
  I.room.init(kRate);
  I.localRoom.init(kRate);

  // ---- load instrument samples: assets/samples/<instrument>/<note>.wav|mp3|flac
  namespace fs = std::filesystem;
  for (int k = 0; k < (int)InstrumentId::Count; ++k) {
    std::string name = instrumentName((InstrumentId)k);
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    fs::path dir = fs::path(sampleRoot) / name;
    if (!fs::is_directory(dir)) continue;
    for (auto& e : fs::directory_iterator(dir)) {
      std::string ext = e.path().extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      if (ext != ".wav" && ext != ".mp3" && ext != ".flac") continue;
      int root = parseNote(e.path().stem().string());
      if (root < 0) continue;
      ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, kRate);
      ma_decoder dec;
      if (ma_decoder_init_file(e.path().string().c_str(), &cfg, &dec) != MA_SUCCESS) continue;
      Sample s;
      s.root = root;
      float buf[4096];
      ma_uint64 got = 0;
      while (ma_decoder_read_pcm_frames(&dec, buf, 4096, &got) == MA_SUCCESS && got > 0) {
        s.data.insert(s.data.end(), buf, buf + got);
        if (s.data.size() > (size_t)kRate * 8) break;  // cap at 8 s per sample
      }
      ma_decoder_uninit(&dec);
      if (!s.data.empty()) I.samples[k].push_back(std::move(s));
    }
    std::sort(I.samples[k].begin(), I.samples[k].end(), [](auto& a, auto& b) { return a.root < b.root; });
    // normalise the instrument so every set plays at a similar loudness
    float peak = 0.f;
    for (auto& smp : I.samples[k])
      for (float x : smp.data) peak = std::max(peak, std::abs(x));
    if (peak > 1e-4f)
      for (auto& smp : I.samples[k])
        for (float& x : smp.data) x *= 0.55f / peak;
    if (!I.samples[k].empty())
      std::cout << "Samples: " << instrumentName((InstrumentId)k) << " x" << I.samples[k].size() << "\n";
  }

  // ---- duplex device (mic in, stereo out); fall back to playback-only
  ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
  cfg.sampleRate = kRate;
  cfg.playback.format = ma_format_f32;
  cfg.playback.channels = 2;
  cfg.capture.format = ma_format_f32;
  cfg.capture.channels = 1;
  cfg.periodSizeInFrames = 256;  // ~5 ms: keypress-to-sound well under 30 ms
  cfg.dataCallback = dataCallback;
  cfg.pUserData = this;
  if (ma_device_init(nullptr, &cfg, &I.device) == MA_SUCCESS) {
    micOk_ = true;
  } else {
    cfg = ma_device_config_init(ma_device_type_playback);
    cfg.sampleRate = kRate;
    cfg.playback.format = ma_format_f32;
    cfg.playback.channels = 2;
    cfg.periodSizeInFrames = 256;
    cfg.dataCallback = dataCallback;
    cfg.pUserData = this;
    if (ma_device_init(nullptr, &cfg, &I.device) != MA_SUCCESS) {
      std::cerr << "Audio: no output device — running silent\n";
      return false;
    }
    micOk_ = false;
  }
  if (ma_device_start(&I.device) != MA_SUCCESS) {
    ma_device_uninit(&I.device);
    return false;
  }
  deviceOk_ = true;
  std::cout << "Audio: 48 kHz, mic " << (micOk_ ? "on" : "unavailable") << "\n";
  return true;
}

void AudioEngine::shutdown() {
  if (deviceOk_) {
    ma_device_uninit(&impl_->device);
    deviceOk_ = false;
  }
}

bool AudioEngine::usingSamples(InstrumentId id) const { return !impl_->samples[(int)id].empty(); }

void AudioEngine::noteOn(InstrumentId id, int midi, float vel, uint32_t source, double atClock) {
  uint64_t at = atClock < 0 ? 0 : (uint64_t)(atClock * kRate);
  std::lock_guard<std::mutex> l(impl_->evMutex);
  impl_->incoming.push_back({Event::On, id, midi, vel, source, at});
}
void AudioEngine::noteOff(InstrumentId id, int midi, uint32_t source, double atClock) {
  uint64_t at = atClock < 0 ? 0 : (uint64_t)(atClock * kRate);
  std::lock_guard<std::mutex> l(impl_->evMutex);
  impl_->incoming.push_back({Event::Off, id, midi, 0.f, source, at});
}
void AudioEngine::allNotesOff(uint32_t source) {
  std::lock_guard<std::mutex> l(impl_->evMutex);
  impl_->incoming.push_back({Event::AllOff, InstrumentId::Piano, 0, 0.f, source, 0});
}

bool AudioEngine::loadAudioFile(const std::string& path, std::string& error) {
  playAudioFile(false);
  ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, kRate);  // resample to 48 kHz stereo
  ma_decoder dec;
  if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
    error = "could not decode " + path;
    return false;
  }
  std::vector<float> data;
  float buf[8192];
  ma_uint64 got = 0;
  while (ma_decoder_read_pcm_frames(&dec, buf, 4096, &got) == MA_SUCCESS && got > 0) {
    data.insert(data.end(), buf, buf + got * 2);
    if (data.size() > (size_t)kRate * 2 * 60 * 15) break;  // 15 min cap
  }
  ma_decoder_uninit(&dec);
  if (data.empty()) {
    error = "file has no audio";
    return false;
  }
  // swap under the device lock: stop playback first so the callback never reads a resizing buffer
  if (deviceOk_) ma_device_stop(&impl_->device);
  file_ = std::move(data);
  filePos_ = 0;
  if (deviceOk_) ma_device_start(&impl_->device);
  return true;
}

void AudioEngine::playAudioFile(bool play) {
  if (play) filePos_ = 0;
  filePlaying_ = play && !file_.empty();
}

void AudioEngine::drainOutgoing(StreamKind kind, std::vector<int16_t>& out) {
  std::lock_guard<std::mutex> l(impl_->outMutex);
  auto& r = kind == StreamKind::Voice ? impl_->outVoice : impl_->outSong;
  size_t n = r.size();
  size_t base = out.size();
  out.resize(base + n);
  r.pop(out.data() + base, n);
}

void AudioEngine::pushRemote(uint32_t player, StreamKind kind, const int16_t* pcm, size_t n) {
  std::lock_guard<std::mutex> l(impl_->streamMutex);
  Stream* free = nullptr;
  for (auto& s : impl_->streams) {
    if (s.used && s.player == player && s.kind == kind) { s.ring.push(pcm, n); return; }
    if (!s.used && !free) free = &s;
  }
  if (!free) return;
  free->used = true;
  free->player = player;
  free->kind = kind;
  free->ring.reset(kNetRate * 2);
  free->primed = false;
  free->frac = 1.0;
  free->ring.push(pcm, n);
}

float AudioEngine::remoteLevel(uint32_t player, StreamKind kind) const {
  for (auto& s : impl_->streams)
    if (s.used && s.player == player && s.kind == kind) return s.level;
  return 0.f;
}

void AudioEngine::dropRemote(uint32_t player) {
  std::lock_guard<std::mutex> l(impl_->streamMutex);
  for (auto& s : impl_->streams)
    if (s.used && s.player == player) s.used = false;
}

void AudioEngine::setRecordTap(bool on) {
  std::lock_guard<std::mutex> l(impl_->tapMutex);
  if (on && !impl_->tapOn) impl_->tap.reset(kRate * 2 * 4);  // keep unread audio when stopping
  impl_->tapOn = on;
}

size_t AudioEngine::drainRecordTap(std::vector<float>& out) {
  std::lock_guard<std::mutex> l(impl_->tapMutex);
  size_t n = impl_->tap.size() & ~size_t(1);
  out.resize(n);
  impl_->tap.pop(out.data(), n);
  return n / 2;
}

// ------------------------------------------------------------------ audio thread
void AudioEngine::process(float* out, const float* in, uint32_t frames) {
  auto& I = *impl_;
  {
    std::unique_lock<std::mutex> l(I.evMutex, std::try_to_lock);
    if (l.owns_lock() && !I.incoming.empty()) {
      I.pending.insert(I.pending.end(), I.incoming.begin(), I.incoming.end());
      I.incoming.clear();
      std::stable_sort(I.pending.begin(), I.pending.end(), [](const Event& a, const Event& b) { return a.at < b.at; });
    }
  }
  // short blocking lock: the network thread only memcpys into the rings
  std::unique_lock<std::mutex> streamLock(I.streamMutex);
  uint64_t now = samplesRendered_.load();
  const float dt = 1.f / kRate;
  const float send = reverbSend.load();
  const float gain = masterGain.load();
  const bool sing = pushToSing_.load() && !muted_.load();
  const bool mon = monitor_.load();
  const bool playing = filePlaying_.load();
  size_t fpos = filePos_.load();
  size_t pi = 0;
  float micSum = 0.f;
  float voiceLevelAcc[kStreams] = {};
  std::vector<int16_t> netVoice, netSong;
  netVoice.reserve(frames / 2 + 2);
  netSong.reserve(frames / 2 + 2);
  bool tapping;
  {
    std::lock_guard<std::mutex> l(I.tapMutex);
    tapping = I.tapOn;
  }
  float tapBuf[4096];
  size_t tapN = 0;

  for (uint32_t f = 0; f < frames; ++f) {
    uint64_t clockNow = now + f;
    // ---- events due at this sample
    while (pi < I.pending.size() && I.pending[pi].at <= clockNow) {
      const Event& e = I.pending[pi++];
      if (e.type == Event::On) {
        Voice* v = nullptr;
        for (auto& c : I.voices) if (!c.active) { v = &c; break; }
        if (!v) {  // steal the oldest released voice, else the oldest
          for (auto& c : I.voices) if (c.released && (!v || c.order < v->order)) v = &c;
          if (!v) for (auto& c : I.voices) if (!v || c.order < v->order) v = &c;
        }
        *v = Voice{};
        v->active = true;
        v->inst = e.inst;
        v->note = e.note;
        v->source = e.source;
        v->vel = std::clamp(e.vel, 0.05f, 1.f);
        v->freq = midiToHz((float)e.note);
        v->env = 1.f;
        v->rng = 0x9E3779B9u ^ (uint32_t)(e.note * 7919 + clockNow);
        v->order = ++I.voiceOrder;
        v->pan = 0.5f + std::clamp((e.note - 60) / 60.f, -0.3f, 0.3f);
        auto& set = I.samples[(int)e.inst];
        if (!set.empty()) {
          const Sample* best = &set[0];
          for (auto& s : set) if (std::abs(s.root - e.note) < std::abs(best->root - e.note)) best = &s;
          v->sample = best;
          v->sstep = std::pow(2.0, (e.note - best->root) / 12.0);
        }
      } else if (e.type == Event::Off) {
        for (auto& c : I.voices)
          if (c.active && !c.released && c.inst == e.inst && c.note == e.note && c.source == e.source) {
            c.released = true;
            c.relRate = dt / releaseTime(c.inst);
          }
      } else {
        for (auto& c : I.voices)
          if (c.active && (e.source == 0 || c.source == e.source)) { c.released = true; c.relRate = dt / 0.1f; }
      }
    }

    // ---- instruments
    float L = 0.f, R = 0.f;
    for (auto& v : I.voices) {
      if (!v.active) continue;
      float s;
      if (v.sample) {
        size_t i0 = (size_t)v.spos;
        if (i0 + 1 >= v.sample->data.size()) { v.active = false; continue; }
        float fr = (float)(v.spos - i0);
        s = (v.sample->data[i0] * (1.f - fr) + v.sample->data[i0 + 1] * fr) * v.vel;
        float att = std::min(1.f, (float)v.age / 0.003f);
        s *= att;
        v.spos += v.sstep;
      } else {
        s = synth(v, dt);
      }
      if (v.released) {
        v.env -= v.relRate;
        if (v.env <= 0.f) { v.active = false; continue; }
      }
      v.age += dt;
      if (v.age > maxDuration(v.inst)) { v.released = true; v.relRate = dt / 0.3f; }
      s *= v.env;
      L += s * (1.f - v.pan) * 1.4f;
      R += s * v.pan * 1.4f;
    }

    // ---- loaded audio file
    float sl = 0.f, sr = 0.f;
    if (playing && fpos + 1 < file_.size()) {
      sl = file_[fpos];
      sr = file_[fpos + 1];
      fpos += 2;
    } else if (playing) {
      filePlaying_ = false;
    }
    float songMono = 0.5f * (sl + sr);

    // ---- microphone
    float mic = in ? in[f] : 0.f;
    I.micDc += (mic - I.micDc) * 0.001f;  // DC blocker
    mic -= I.micDc;
    micSum += mic * mic;
    float voice = sing ? mic * 1.6f : 0.f;

    // ---- remote streams (24 kHz -> 48 kHz linear interpolation, 100 ms jitter buffer)
    float remoteVoice = 0.f, remoteSong = 0.f;
    if (streamLock.owns_lock()) {
      for (int k = 0; k < kStreams; ++k) {
        Stream& st = I.streams[k];
        if (!st.used) continue;
        if (!st.primed) {
          if (st.ring.size() < (size_t)kNetRate / 10) continue;
          st.primed = true;
        }
        st.frac += 0.5;
        if (st.frac >= 1.0) {
          st.frac -= 1.0;
          st.last = st.next;
          int16_t x;
          if (st.ring.pop(&x, 1)) st.next = x / 32768.f;
          else { st.next = 0.f; st.primed = false; }
        }
        float y = st.last + (st.next - st.last) * (float)st.frac;
        voiceLevelAcc[k] += y * y;
        if (st.kind == StreamKind::Voice) remoteVoice += y;
        else remoteSong += y;
      }
    }

    // ---- hall reverb
    float wl, wr, vl, vr;
    I.room.process((L + R) * 0.5f * send * 2.f + remoteVoice * 0.5f + (songMono + remoteSong) * 0.06f, wl, wr);
    I.localRoom.process(voice * 0.5f, vl, vr);
    float baseL = L + sl + remoteSong + remoteVoice + wl;
    float baseR = R + sr + remoteSong + remoteVoice + wr;
    float recL = baseL + voice + vl, recR = baseR + voice + vr;
    float outL = mon ? recL : baseL, outR = mon ? recR : baseR;
    out[f * 2] = std::tanh(outL * gain);
    out[f * 2 + 1] = std::tanh(outR * gain);
    if (tapping) {
      tapBuf[tapN++] = std::tanh(recL * gain);
      tapBuf[tapN++] = std::tanh(recR * gain);
      if (tapN == 4096) {
        std::lock_guard<std::mutex> l(I.tapMutex);
        I.tap.push(tapBuf, tapN);
        tapN = 0;
      }
    }

    // ---- outgoing network streams, decimated to 24 kHz
    I.decimVoice += voice;
    I.decimSong += songMono;
    if (++I.decimPhase == 2) {
      I.decimPhase = 0;
      if (sing) netVoice.push_back((int16_t)std::clamp(I.decimVoice * 0.5f * 32767.f, -32767.f, 32767.f));
      if (playing) netSong.push_back((int16_t)std::clamp(I.decimSong * 0.5f * 32767.f, -32767.f, 32767.f));
      I.decimVoice = I.decimSong = 0.f;
    }

    // ---- song loudness + simple onset (beat) detector for audio-file animation
    float e = songMono * songMono + remoteSong * remoteSong;
    I.songEnv += (e - I.songEnv) * 0.0015f;
    I.energyAvg += (I.songEnv - I.energyAvg) * 0.00002f;
    if (I.beatHold > 0) --I.beatHold;
    else if (I.songEnv > I.energyAvg * 1.5f && I.songEnv > 1e-4f) {
      songBeats_.fetch_add(1);
      I.beatHold = kRate / 5;
    }
  }
  if (tapN) {
    std::lock_guard<std::mutex> l(I.tapMutex);
    I.tap.push(tapBuf, tapN);
  }
  if (pi > 0) I.pending.erase(I.pending.begin(), I.pending.begin() + pi);
  filePos_ = fpos;
  samplesRendered_ += frames;
  for (int k = 0; k < kStreams; ++k)
    if (I.streams[k].used) I.streams[k].level = std::sqrt(voiceLevelAcc[k] / std::max(1u, frames));
  float rms = std::sqrt(micSum / std::max(1u, frames));
  micLevel_ = micLevel_.load() * 0.7f + rms * 0.3f;
  songLevel_ = std::sqrt(I.songEnv);
  if (!netVoice.empty() || !netSong.empty()) {
    std::lock_guard<std::mutex> l(I.outMutex);
    I.outVoice.push(netVoice.data(), netVoice.size());
    I.outSong.push(netSong.data(), netSong.size());
  }
}

int AudioEngine::keyToMidi(int key, int base) {
  // Piano layout: A S D F G H J K white keys, W E T Y U black keys (GLFW key codes = ASCII)
  switch (key) {
    case 'A': return base + 0;
    case 'W': return base + 1;
    case 'S': return base + 2;
    case 'E': return base + 3;
    case 'D': return base + 4;
    case 'F': return base + 5;
    case 'T': return base + 6;
    case 'G': return base + 7;
    case 'Y': return base + 8;
    case 'H': return base + 9;
    case 'U': return base + 10;
    case 'J': return base + 11;
    case 'K': return base + 12;
    default: return -1;
  }
}

}  // namespace og
