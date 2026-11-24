#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace og {

enum class InstrumentId { Piano = 0, Violin, Cello, Flute, Trumpet, Timpani, Count };
const char* instrumentName(InstrumentId id);

struct NoteEvent {
  InstrumentId instrument = InstrumentId::Piano;
  int midiNote = 60;
  float velocity = 0.8f;
  bool on = true;
  double time = 0.0;     // seconds (song time or audio clock)
  uint32_t playerId = 0;
  int part = -1;         // loaded-song part (track/channel); -1 = live playing
};

enum class StreamKind : int { Voice = 0, Song = 1 };

// Fixed-capacity single-producer/single-consumer style ring (guarded by a mutex
// held only for a memcpy; never allocates after construction).
template <typename T>
class Ring {
 public:
  explicit Ring(size_t cap = 0) : buf_(cap) {}
  void reset(size_t cap) { buf_.assign(cap, T{}); r_ = w_ = n_ = 0; }
  size_t size() const { return n_; }
  size_t capacity() const { return buf_.size(); }
  void push(const T* d, size_t k) {
    for (size_t i = 0; i < k; ++i) {
      if (n_ == buf_.size()) { r_ = (r_ + 1) % buf_.size(); --n_; }  // drop oldest
      buf_[w_] = d[i];
      w_ = (w_ + 1) % buf_.size();
      ++n_;
    }
  }
  size_t pop(T* d, size_t k) {
    size_t m = std::min(k, n_);
    for (size_t i = 0; i < m; ++i) { d[i] = buf_[r_]; r_ = (r_ + 1) % buf_.size(); }
    n_ -= m;
    return m;
  }
 private:
  std::vector<T> buf_;
  size_t r_ = 0, w_ = 0, n_ = 0;
};

class AudioEngine {
 public:
  static constexpr int kRate = 48000;
  static constexpr int kNetRate = 24000;  // voice / song streams over the network

  AudioEngine();
  ~AudioEngine();
  bool init(const std::string& sampleRoot);
  void shutdown();
  bool deviceOk() const { return deviceOk_; }

  // Notes. atClock < 0 = as soon as possible; otherwise audio-clock seconds.
  void noteOn(InstrumentId id, int midi, float vel, uint32_t source, double atClock = -1.0);
  void noteOff(InstrumentId id, int midi, uint32_t source, double atClock = -1.0);
  void allNotesOff(uint32_t source);
  double clock() const { return (double)samplesRendered_.load() / kRate; }
  bool usingSamples(InstrumentId id) const;

  // Loaded audio file (S3): plays as one mixed track; drives loudness/beat animation.
  bool loadAudioFile(const std::string& path, std::string& error);
  void playAudioFile(bool play);
  bool audioFilePlaying() const { return filePlaying_.load(); }
  bool audioFileLoaded() const { return !file_.empty(); }
  double audioFileDuration() const { return (double)file_.size() / 2 / kRate; }
  double audioFilePosition() const { return (double)filePos_.load() / kRate; }
  float songLevel() const { return songLevel_.load(); }
  int songBeats() const { return songBeats_.load(); }

  // Microphone (V1-V4)
  bool micAvailable() const { return micOk_; }
  void setPushToSing(bool down) { pushToSing_.store(down); }
  void setMuted(bool m) { muted_.store(m); }
  void setMonitor(bool m) { monitor_.store(m); }
  bool monitor() const { return monitor_.load(); }
  float micLevel() const { return micLevel_.load(); }
  float outputLevel() const { return outLevel_.load(); }

  // Network streams: 24 kHz mono int16
  void drainOutgoing(StreamKind kind, std::vector<int16_t>& out);
  void pushRemote(uint32_t player, StreamKind kind, const int16_t* pcm, size_t n);
  float remoteLevel(uint32_t player, StreamKind kind) const;
  void dropRemote(uint32_t player);

  // Recording tap: exactly what the room hears (instruments, song, all voices, reverb)
  void setRecordTap(bool on);
  size_t drainRecordTap(std::vector<float>& interleavedStereo);

  std::atomic<float> masterGain{0.9f};
  std::atomic<float> reverbSend{0.22f};

  static int keyToMidi(int glfwKey, int octaveBase);

  // internal, called on the audio thread
  void process(float* out, const float* in, uint32_t frames);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  bool deviceOk_ = false;
  bool micOk_ = false;
  std::atomic<uint64_t> samplesRendered_{0};
  std::atomic<bool> pushToSing_{false}, muted_{false}, monitor_{false};
  std::atomic<float> micLevel_{0.f}, songLevel_{0.f}, outLevel_{0.f};
  std::atomic<int> songBeats_{0};
  std::atomic<bool> filePlaying_{false};
  std::atomic<size_t> filePos_{0};
  std::vector<float> file_;  // stereo interleaved, 48 kHz
};

// Loaded MIDI song playback (S1/S2): parts are assigned to orchestra seats and
// scheduled sample-accurately on the audio clock with a short look-ahead.
struct MidiPart {
  int key = 0;            // track * 16 + channel
  std::string name;
  int program = 0;
  InstrumentId instrument = InstrumentId::Piano;
  int seat = -1;          // orchestra seat that plays it; -1 = muted
  int noteCount = 0;
};

struct MidiSong {
  std::vector<NoteEvent> notes;  // sorted by time; part = index into parts
  std::vector<MidiPart> parts;
  double duration = 0.0;
  std::string title;
  bool load(const std::string& path, std::string& error);
};

class SongPlayer {
 public:
  MidiSong song;
  bool loaded = false;
  bool playing = false;
  double startClock = 0.0;
  size_t cursor = 0;

  void start(double audioClock);
  void stop();
  double position(double audioClock) const { return playing ? audioClock - startClock : 0.0; }
  // Emits every event due before audioClock + lookahead; event.time is rewritten to audio-clock seconds.
  template <typename F>
  void pump(double audioClock, double lookahead, F&& emit) {
    if (!playing) return;
    while (cursor < song.notes.size() && startClock + song.notes[cursor].time <= audioClock + lookahead) {
      NoteEvent e = song.notes[cursor++];
      e.time += startClock;
      emit(e);
    }
    if (cursor >= song.notes.size() && audioClock > startClock + song.duration + 1.0) playing = false;
  }
};

}  // namespace og
