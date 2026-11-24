#pragma once
#include "render/Renderer.h"
#include "scene/World.h"
#include "ui/Hud.h"
#include <deque>
#include <string>

struct GLFWwindow;

namespace og {

struct LaunchOptions {
  std::string shotPath;      // --shot out.png : render N frames headless, save, exit
  std::string shotScene = "house";  // city | house | film | lighting | pit
  int shotFrames = 90;
  int shotW = 1920, shotH = 1080;
  bool shotHud = false;      // --hud : capture the window including the control panel
  std::string join;          // --join host:port
  std::string name;
  float recordTest = 0.f;    // --record-test seconds : film a take and exit (no dialog)
  std::string recordOut;
};

class Application {
 public:
  bool init(const LaunchOptions& opts);
  void run();
  void shutdown();

 private:
  GLFWwindow* window_ = nullptr;
  Renderer renderer_;
  World world_;
  Hud hud_;
  LaunchOptions opts_;
  int fbW_ = 1280, fbH_ = 720;

  // recording pipeline state
  bool capturing_ = false;
  double captureStart_ = 0.0;
  int64_t lastQueued_ = -1;
  std::deque<int64_t> inFlight_;
  std::vector<unsigned char> pixels_;
  std::vector<float> audioScratch_;

  void pumpRecording(double now);
  void finishRecording(bool askDialog);
  void setupShotScene();
};

}  // namespace og
