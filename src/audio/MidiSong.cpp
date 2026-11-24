// Standard MIDI File parser (format 0/1) with a global tempo map.
// Notes are grouped into parts by (track, channel); each part maps to an
// orchestra instrument via its General MIDI program (S1).
#include "audio/AudioEngine.h"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>

namespace og {
namespace {

struct Reader {
  const uint8_t* p;
  const uint8_t* end;
  bool ok() const { return p < end; }
  uint32_t be32() { if (end - p < 4) { p = end; return 0; } uint32_t v = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; p += 4; return v; }
  uint16_t be16() { if (end - p < 2) { p = end; return 0; } uint16_t v = (uint16_t)((p[0] << 8) | p[1]); p += 2; return v; }
  uint32_t vlq() {
    uint32_t v = 0;
    for (int i = 0; i < 4 && p < end; ++i) {
      uint8_t b = *p++;
      v = (v << 7) | (b & 0x7f);
      if (!(b & 0x80)) break;
    }
    return v;
  }
};

InstrumentId gmToInstrument(int program, int channel) {
  if (channel == 9) return InstrumentId::Timpani;          // GM percussion channel
  if (program <= 7) return InstrumentId::Piano;            // pianos
  if (program <= 15) return InstrumentId::Piano;           // chromatic percussion
  if (program <= 23) return InstrumentId::Piano;           // organs
  if (program == 40 || program == 41) return InstrumentId::Violin;  // violin, viola
  if (program == 42 || program == 43) return InstrumentId::Cello;   // cello, contrabass
  if (program >= 44 && program <= 46) return InstrumentId::Violin;  // tremolo/pizz/harp
  if (program == 47) return InstrumentId::Timpani;
  if (program >= 48 && program <= 55) return InstrumentId::Violin;  // string ensembles, choir
  if (program >= 56 && program <= 63) return InstrumentId::Trumpet; // brass
  if (program >= 64 && program <= 79) return InstrumentId::Flute;   // reeds, pipes
  if (program >= 32 && program <= 39) return InstrumentId::Cello;   // basses
  return InstrumentId::Piano;
}

struct RawNote { uint32_t tick; int key; int note; int vel; bool on; };
struct TempoChange { uint32_t tick; double usPerQuarter; };

}  // namespace

bool MidiSong::load(const std::string& path, std::string& error) {
  std::ifstream f(path, std::ios::binary);
  if (!f) { error = "cannot open " + path; return false; }
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
  Reader r{data.data(), data.data() + data.size()};
  if (data.size() < 14 || std::string((const char*)data.data(), 4) != "MThd") { error = "not a MIDI file"; return false; }
  r.p += 4;
  uint32_t hdrLen = r.be32();
  uint16_t format = r.be16();
  uint16_t nTracks = r.be16();
  uint16_t division = r.be16();
  r.p = data.data() + 8 + hdrLen;
  if (division & 0x8000) { error = "SMPTE time division not supported"; return false; }
  (void)format;
  double tpq = division ? division : 480;

  std::vector<RawNote> raw;
  std::vector<TempoChange> tempos{{0, 500000.0}};
  std::map<int, int> programOf;  // part key -> program
  std::map<int, std::string> trackName;

  for (int t = 0; t < nTracks && r.p + 8 <= r.end; ++t) {
    if (std::string((const char*)r.p, 4) != "MTrk") break;
    r.p += 4;
    uint32_t len = r.be32();
    Reader tr{r.p, std::min(r.p + len, r.end)};
    r.p = tr.end;
    uint32_t tick = 0;
    uint8_t running = 0;
    int chanProgram[16] = {0};
    while (tr.ok()) {
      tick += tr.vlq();
      if (!tr.ok()) break;
      uint8_t status = *tr.p;
      if (status & 0x80) { ++tr.p; if (status < 0xF0) running = status; }
      else status = running;
      if (status == 0xFF) {
        if (!tr.ok()) break;
        uint8_t meta = *tr.p++;
        uint32_t l = tr.vlq();
        if (meta == 0x51 && l == 3 && tr.end - tr.p >= 3)
          tempos.push_back({tick, (double)((tr.p[0] << 16) | (tr.p[1] << 8) | tr.p[2])});
        if (meta == 0x03 && tr.end - tr.p >= (long)l) trackName[t] = std::string((const char*)tr.p, l);
        tr.p += std::min<long>(l, tr.end - tr.p);
      } else if (status == 0xF0 || status == 0xF7) {
        uint32_t l = tr.vlq();
        tr.p += std::min<long>(l, tr.end - tr.p);
      } else {
        int type = status & 0xF0, ch = status & 0x0F;
        int need = (type == 0xC0 || type == 0xD0) ? 1 : 2;
        if (tr.end - tr.p < need) break;
        int a = tr.p[0], b = need > 1 ? tr.p[1] : 0;
        tr.p += need;
        int key = t * 16 + ch;
        if (type == 0x90 || type == 0x80) {
          bool on = type == 0x90 && b > 0;
          raw.push_back({tick, key, a, b, on});
          if (!programOf.count(key)) programOf[key] = chanProgram[ch];
        } else if (type == 0xC0) {
          chanProgram[ch] = a;
          programOf[key] = a;
        }
      }
    }
  }
  if (raw.empty()) { error = "no notes in file"; return false; }

  // tempo map: ticks -> seconds, honouring every tempo change in any track
  std::sort(tempos.begin(), tempos.end(), [](auto& a, auto& b) { return a.tick < b.tick; });
  auto toSeconds = [&](uint32_t tick) {
    double sec = 0.0;
    uint32_t last = 0;
    double us = 500000.0;
    for (auto& tc : tempos) {
      if (tc.tick >= tick) break;
      sec += (tc.tick - last) / tpq * us * 1e-6;
      last = tc.tick;
      us = tc.usPerQuarter;
    }
    return sec + (tick - last) / tpq * us * 1e-6;
  };

  parts.clear();
  std::map<int, int> partIndex;
  notes.clear();
  for (auto& n : raw) {
    if (!partIndex.count(n.key)) {
      MidiPart p;
      p.key = n.key;
      p.program = programOf.count(n.key) ? programOf[n.key] : 0;
      p.instrument = gmToInstrument(p.program, n.key % 16);
      int track = n.key / 16;
      p.name = (trackName.count(track) ? trackName[track] : "Track " + std::to_string(track + 1)) + " / ch " +
               std::to_string(n.key % 16 + 1);
      partIndex[n.key] = (int)parts.size();
      parts.push_back(p);
    }
    NoteEvent e;
    e.part = partIndex[n.key];
    e.instrument = parts[e.part].instrument;
    e.midiNote = n.note;
    e.velocity = n.vel / 127.f;
    e.on = n.on;
    e.time = toSeconds(n.tick);
    if (e.on) parts[e.part].noteCount++;
    notes.push_back(e);
  }
  std::stable_sort(notes.begin(), notes.end(), [](const NoteEvent& a, const NoteEvent& b) {
    if (a.time != b.time) return a.time < b.time;
    return !a.on && b.on;  // offs before ons at the same instant
  });
  duration = notes.empty() ? 0.0 : notes.back().time;
  title = std::filesystem::path(path).stem().string();
  return true;
}

void SongPlayer::start(double audioClock) {
  startClock = audioClock + 0.15;  // small pre-roll so the first notes aren't late
  cursor = 0;
  playing = loaded;
}

void SongPlayer::stop() { playing = false; }

}  // namespace og
