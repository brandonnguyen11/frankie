#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace og {

// Video + audio file writer for the camera operator (C4/C5).
// macOS: AVFoundation (H.264 + AAC .mp4, hardware encoder, no dependencies).
// Other platforms: pipes raw frames to an ffmpeg process.
class Recorder {
 public:
  virtual ~Recorder() = default;
  static std::unique_ptr<Recorder> create();

  virtual bool start(const std::string& path, int width, int height, int fps, int sampleRate, int channels,
                     std::string& error) = 0;
  // BGRA pixels, rows bottom-up (OpenGL order). frameIndex = presentation time in 1/fps units.
  virtual void pushVideo(const unsigned char* bgra, int width, int height, int64_t frameIndex) = 0;
  // Interleaved float PCM at the configured rate.
  virtual void pushAudio(const float* interleaved, size_t frames) = 0;
  // Finish writing; blocks until the file is complete.
  virtual bool stop(std::string& error) = 0;
  virtual bool active() const = 0;
  virtual const char* backend() const = 0;
  const std::string& path() const { return path_; }

 protected:
  std::string path_;
};

// ~/Movies/Opera Grenouille/Take 2026-11-23 20.15.04.mp4 (created if missing)
std::string defaultRecordingPath();
// Native save dialog. Returns false if the user cancelled (file stays at `current`).
bool askSaveLocation(const std::string& current, std::string& chosen);

}  // namespace og
