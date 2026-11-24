#pragma once
#include "scene/World.h"

struct GLFWwindow;

namespace og {

struct FrameInfo {
  float fps = 0.f;
  float frameMs = 0.f;
  int renderW = 0, renderH = 0;
  RenderStats stats;
};

class Hud {
 public:
  bool init(GLFWwindow* window);
  void begin();
  void draw(World& world, const FrameInfo& info);
  void end();
  void shutdown();

 private:
  char roomAddress_[128] = "127.0.0.1:7777";
  char playerName_[24] = "Frog";
  char midiPath_[512] = "assets/samples/demo.mid";
  char audioPath_[512] = "assets/samples/demo.wav";
  char cueName_[32] = "Cue 1";
  float cueFade_ = 3.f;
  std::string credits_;
  bool showHelp_ = true;

  void drawRecBanner(World& w);
  void drawRoles(World& w);
  void drawMusician(World& w);
  void drawSinger(World& w);
  void drawLighting(World& w);
  void drawCamera(World& w);
  void drawViewfinder(World& w);
  void drawFrogSpots(World& w);
  void drawRoom(World& w);
  void drawGraphics(World& w, const FrameInfo& info);
  void drawHelp(World& w);
};

}  // namespace og
