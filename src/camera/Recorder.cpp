// Non-Apple recorder: pipe BGRA frames to ffmpeg (H.264), write audio to a
// temporary WAV, then mux both into the final .mp4 when recording stops.
#include "camera/Recorder.h"
#include "core/Platform.h"
#ifndef __APPLE__
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <vector>

namespace og {
namespace {

class FfmpegRecorder : public Recorder {
 public:
  ~FfmpegRecorder() override {
    std::string e;
    if (active_) stop(e);
  }
  bool start(const std::string& path, int w, int h, int fps, int rate, int ch, std::string& error) override {
    path_ = path;
    w_ = w; h_ = h; fps_ = fps; rate_ = rate; ch_ = ch;
    videoTmp_ = path + ".video.mp4";
    audioTmp_ = path + ".audio.wav";
    std::string cmd = "ffmpeg -loglevel error -y -f rawvideo -pix_fmt bgra -s " + std::to_string(w) + "x" +
                      std::to_string(h) + " -r " + std::to_string(fps) +
                      " -i - -vf vflip -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \"" + videoTmp_ + "\"";
#ifdef _WIN32
    pipe_ = _popen(cmd.c_str(), "wb");
#else
    pipe_ = popen(cmd.c_str(), "w");
#endif
    if (!pipe_) { error = "could not start ffmpeg (is it installed and on PATH?)"; return false; }
    wav_.open(audioTmp_, std::ios::binary);
    writeWavHeader(0);
    audioFrames_ = 0;
    lastIdx_ = -1;
    active_ = true;
    return true;
  }
  void pushVideo(const unsigned char* bgra, int w, int h, int64_t idx) override {
    if (!active_ || w != w_ || h != h_ || idx <= lastIdx_) return;
    // constant frame rate: repeat the frame to fill any gap
    int64_t reps = lastIdx_ < 0 ? 1 : idx - lastIdx_;
    for (int64_t r = 0; r < reps; ++r) fwrite(bgra, 1, (size_t)w * h * 4, pipe_);
    lastIdx_ = idx;
  }
  void pushAudio(const float* d, size_t frames) override {
    if (!active_) return;
    std::vector<int16_t> s(frames * ch_);
    for (size_t i = 0; i < s.size(); ++i) s[i] = (int16_t)std::max(-32767.f, std::min(32767.f, d[i] * 32767.f));
    wav_.write((const char*)s.data(), s.size() * 2);
    audioFrames_ += frames;
  }
  bool stop(std::string& error) override {
    if (!active_) return false;
    active_ = false;
#ifdef _WIN32
    _pclose(pipe_);
#else
    pclose(pipe_);
#endif
    writeWavHeader(audioFrames_);
    wav_.close();
    std::string mux = "ffmpeg -loglevel error -y -i \"" + videoTmp_ + "\" -i \"" + audioTmp_ +
                      "\" -c:v copy -c:a aac -b:a 192k -shortest \"" + path_ + "\"";
    int rc = std::system(mux.c_str());
    std::error_code ec;
    std::filesystem::remove(videoTmp_, ec);
    std::filesystem::remove(audioTmp_, ec);
    if (rc != 0) { error = "ffmpeg mux failed"; return false; }
    return true;
  }
  bool active() const override { return active_; }
  const char* backend() const override { return "ffmpeg (H.264/AAC)"; }

 private:
  void writeWavHeader(size_t frames) {
    uint32_t dataBytes = (uint32_t)(frames * ch_ * 2);
    wav_.seekp(0);
    auto u32 = [&](uint32_t v) { wav_.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { wav_.write((const char*)&v, 2); };
    wav_.write("RIFF", 4); u32(36 + dataBytes); wav_.write("WAVEfmt ", 8); u32(16); u16(1); u16((uint16_t)ch_);
    u32((uint32_t)rate_); u32((uint32_t)(rate_ * ch_ * 2)); u16((uint16_t)(ch_ * 2)); u16(16);
    wav_.write("data", 4); u32(dataBytes);
    wav_.seekp(0, std::ios::end);
  }
  FILE* pipe_ = nullptr;
  std::ofstream wav_;
  std::string videoTmp_, audioTmp_;
  bool active_ = false;
  int w_ = 0, h_ = 0, fps_ = 30, rate_ = 48000, ch_ = 2;
  size_t audioFrames_ = 0;
  int64_t lastIdx_ = -1;
};

}  // namespace

std::unique_ptr<Recorder> Recorder::create() { return std::make_unique<FfmpegRecorder>(); }

std::string defaultRecordingPath() {
  const char* home = std::getenv("HOME");
#ifdef _WIN32
  if (!home) home = std::getenv("USERPROFILE");
#endif
  std::filesystem::path dir = std::filesystem::path(home ? home : ".") / "Videos" / "Opera Grenouille";
  std::error_code ec;
  std::filesystem::create_directories(dir, ec);
  char name[64];
  std::time_t t = std::time(nullptr);
  std::strftime(name, sizeof(name), "Take %Y-%m-%d %H.%M.%S.mp4", std::localtime(&t));
  return (dir / name).string();
}

bool askSaveLocation(const std::string& current, std::string& chosen) {
  chosen = current;  // no native dialog on this platform; file stays in ~/Videos
  return true;
}

bool openFileDialog(const std::string&, const std::vector<std::string>&, std::string&) { return false; }
bool saveFileDialog(const std::string&, const std::string&, std::string&) { return false; }

}  // namespace og
#endif
