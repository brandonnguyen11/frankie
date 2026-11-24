#include "core/Application.h"
#include "core/AssetPath.h"
#include "core/Input.h"
#include "imgui.h"
#include <glad/gl.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include <iostream>

namespace og {

static constexpr int kRecW = 1920, kRecH = 1080, kRecFps = 30;
bool writePng(const std::string& path, int w, int h, const std::vector<unsigned char>& rgba);  // Renderer.cpp

bool Application::init(const LaunchOptions& opts) {
  opts_ = opts;
  if (!glfwInit()) {
    std::cerr << "glfwInit failed\n";
    return false;
  }
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
  glfwWindowHint(GLFW_SCALE_TO_MONITOR, GL_TRUE);
  if (!opts.shotPath.empty()) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  window_ = glfwCreateWindow(1600, 900, "Opéra Grenouille", nullptr, nullptr);
  if (!window_) {
    std::cerr << "Could not create an OpenGL 4.1 window\n";
    glfwTerminate();
    return false;
  }
  glfwMakeContextCurrent(window_);
  glfwSwapInterval(1);
  if (!gladLoadGL(glfwGetProcAddress)) {
    std::cerr << "gladLoadGL failed\n";
    return false;
  }
  std::cout << "OpenGL " << glGetString(GL_VERSION) << " - " << glGetString(GL_RENDERER) << "\n";
  glfwGetFramebufferSize(window_, &fbW_, &fbH_);

  if (!renderer_.init()) {
    std::cerr << "Renderer init failed (shaders: " << shaderPath("") << ")\n";
    return false;
  }
  if (!hud_.init(window_)) return false;
  Input::instance().bind(window_);
  if (!world_.init()) return false;
  if (!opts.name.empty()) world_.localPlayer.name = opts.name;
  if (!opts.join.empty()) {
    std::string err;
    if (!world_.net.connect(opts.join, world_.localPlayer.name, err)) world_.setStatus("Join failed: " + err, 6.f);
  }
  if (!opts.shotPath.empty() || opts.recordTest > 0.f) setupShotScene();
  return true;
}

// Deterministic set-ups for --shot screenshots and --record-test.
void Application::setupShotScene() {
  auto& w = world_;
  const std::string& sc = opts_.shotScene;
  if (sc == "plaza") {
    w.localPlayer.position = {-4.f, 0.f, 30.f};
    w.localPlayer.yaw = 160.f;
    w.orbit.yaw = 95.f;
    w.orbit.pitch = 2.f;
    w.orbit.distance = 6.f;
    w.orbit.fov = 75.f;
    return;
  }
  if (sc == "city") {
    w.localPlayer.position = {2.5f, 0.f, 21.f};
    w.localPlayer.yaw = 200.f;
    w.orbit.yaw = 80.f;
    w.orbit.pitch = 4.f;
    w.orbit.distance = 7.f;
    w.orbit.fov = 62.f;
    return;
  }
  w.zone = Zone::Auditorium;
  w.fade = 0.f;
  // two guest singers on their marks so the followspots have someone to light
  for (int i = 0; i < 2; ++i) {
    FrogActor s;
    s.id = 9000 + i;
    s.name = i ? "Mezzo" : "Tenor";
    s.role = Role::Singer;
    s.inHouse = true;
    s.position = s.netPosition = {i ? 2.2f : -2.2f, 1.1f, -5.f};
    s.yaw = s.netYaw = i ? -12.f : 12.f;
    s.mouthOpen = 0.6f;
    s.suitColor = i ? glm::vec3(0.16f, 0.02f, 0.03f) : glm::vec3(0.02f, 0.02f, 0.03f);
    s.lastHeard = 1.0;
    w.remotePlayers.push_back(s);
  }
  w.localPlayer.position = {0.8f, 1.6f, 19.f};
  if (sc == "lighting") {
    w.localPlayer.role = Role::LightingDesigner;
    w.lightView = LightView::TopDown;
    w.lightingOwner = (int)w.localPlayer.id;
  } else if (sc == "spots" || sc == "rec") {
    // camera on the mid stalls rail; "spots" also gives every frog its own followspot
    w.localPlayer.role = Role::CameraOperator;
    w.rails.switchRail(1);
    w.rails.focalLengthMm = 26.f;
    w.rails.pitch = 0.f;
    w.rails.fStop = 8.f;
    w.rails.orientation = w.rails.targetOrientation();
    w.refreshNpcs();
    if (sc == "spots") {
      auto names = w.performerNames();
      auto pos = w.performerPositions();
      for (size_t i = 0; i < names.size(); ++i) w.rig.addFollowspot((int)i, pos[i], names[i]);
    } else {
      w.startRecording();
    }
    return;
  } else if (sc == "musician") {
    w.localPlayer.role = Role::Musician;
    w.claimSeat(2);
    w.orbit.yaw = 60.f;
    w.orbit.pitch = 16.f;
    w.orbit.distance = 4.f;
  } else if (sc == "orbit") {
    w.localPlayer.role = Role::Audience;
    w.localPlayer.position = {1.f, 0.6f, 9.f};
    w.orbit.yaw = 75.f;
    w.orbit.pitch = 12.f;
  } else {
    w.localPlayer.role = Role::CameraOperator;
    if (sc == "house") {
      w.rails.switchRail(2);
      w.rails.focalLengthMm = 20.f;
      w.rails.pitch = 2.f;
      w.rails.fStop = 8.f;
    } else if (sc == "pit") {
      w.rails.switchRail(0);
      w.rails.s = w.rails.rails[0].totalLength * 0.3f;
      w.rails.focalLengthMm = 30.f;
      w.rails.yaw = -105.f;
      w.rails.pitch = -10.f;
      w.rails.fStop = 4.f;
      w.rails.focusDistance = 4.f;
    } else if (sc == "balcony") {
      w.rails.switchRail(3);
      w.rails.s = w.rails.rails[3].totalLength * 0.18f;
      w.rails.focalLengthMm = 24.f;
      w.rails.yaw = -40.f;
      w.rails.pitch = -8.f;
      w.rails.fStop = 8.f;
    } else {  // "film": singers at 85 mm with shallow depth of field
      w.rails.switchRail(1);
      w.rails.focalLengthMm = 85.f;
      w.rails.yaw = -90.f;
      w.rails.pitch = -2.5f;
      w.rails.fStop = 2.f;
      w.rails.focusDistance = glm::distance(w.rails.position(), glm::vec3(-2.2f, 2.3f, -5.f));
    }
    w.rails.orientation = w.rails.targetOrientation();
  }
  w.refreshNpcs();
}

void Application::pumpRecording(double now) {
  auto& w = world_;
  if (w.recording && !capturing_) {
    capturing_ = true;
    captureStart_ = w.recordStartWall;
    lastQueued_ = -1;
    inFlight_.clear();
  }
  if (!capturing_) return;
  if (w.recording) {
    // fixed 30 fps timeline: queue a readback whenever a new frame slot is due
    int64_t idx = (int64_t)std::floor((now - captureStart_) * kRecFps);
    if (idx > lastQueued_) {
      renderer_.queueReadback();
      inFlight_.push_back(idx);
      lastQueued_ = idx;
    }
    int fw, fh;
    if (inFlight_.size() > 2 && renderer_.fetchReadback(pixels_, fw, fh)) {
      w.recorder->pushVideo(pixels_.data(), fw, fh, inFlight_.front());
      inFlight_.pop_front();
    }
    size_t frames = w.audio.drainRecordTap(audioScratch_);
    if (frames) w.recorder->pushAudio(audioScratch_.data(), frames);
  }
}

void Application::finishRecording(bool askDialog) {
  auto& w = world_;
  if (!capturing_) return;
  capturing_ = false;
  std::vector<std::vector<unsigned char>> rest;
  renderer_.flushReadbacks(rest);
  for (auto& f : rest) {
    if (inFlight_.empty()) break;
    w.recorder->pushVideo(f.data(), kRecW, kRecH, inFlight_.front());
    inFlight_.pop_front();
  }
  size_t frames = w.audio.drainRecordTap(audioScratch_);
  if (frames) w.recorder->pushAudio(audioScratch_.data(), frames);
  std::string err;
  std::string path = w.recorder->path();
  if (!w.recorder->stop(err)) {
    w.setStatus("Recording failed: " + err, 8.f);
    std::cerr << "Recording failed: " << err << "\n";
    return;
  }
  double secs = (lastQueued_ + 1) / (double)kRecFps;
  std::string finalPath = path;
  if (askDialog) {
    std::string chosen;
    if (askSaveLocation(path, chosen)) finalPath = chosen;
  }
  w.lastSavedPath = finalPath;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "Saved %.1f s take: ", secs);
  w.setStatus(buf + finalPath, 10.f);
  std::cout << buf << finalPath << "\n";
}

void Application::run() {
  auto& in = Input::instance();
  double last = glfwGetTime();
  double fpsTimer = 0.0;
  int fpsFrames = 0;
  FrameInfo info;
  bool shot = !opts_.shotPath.empty();
  int frame = 0;
  double recordTestStart = -1.0;

  while (!glfwWindowShouldClose(window_)) {
    double now = glfwGetTime();
    float dt = shot ? 1.f / 30.f : (float)std::min(now - last, 0.1);
    last = now;
    fpsTimer += dt;
    ++fpsFrames;
    if (fpsTimer >= 0.5) {
      info.fps = (float)(fpsFrames / fpsTimer);
      info.frameMs = (float)(fpsTimer * 1000.0 / fpsFrames);
      fpsTimer = 0;
      fpsFrames = 0;
    }

    ImGuiIO& io = ImGui::GetIO();
    in.state().uiWantsMouse = io.WantCaptureMouse && !world_.mouseLook;
    in.state().uiWantsKeyboard = io.WantTextInput;
    in.beginFrame();
    glfwPollEvents();
    glfwGetFramebufferSize(window_, &fbW_, &fbH_);

    if (!shot) world_.handleInput(dt);
    world_.update(dt);

    // --record-test: film a take with the demo song, then quit
    if (opts_.recordTest > 0.f) {
      if (recordTestStart < 0.0) {
        std::string err;
        MidiSong song;
        if (song.load(assetPath("samples/demo.mid"), err)) {
          for (auto& p : song.parts) p.seat = 1;
          world_.song.song = song;
          world_.song.loaded = true;
          world_.startSong();
        }
        world_.startRecording();
        recordTestStart = now;
      } else if (now - recordTestStart > opts_.recordTest && world_.recording) {
        world_.stopRecording();
      }
    }

    bool recordSize = world_.recording || capturing_;
    int rw = shot ? opts_.shotW : (recordSize ? kRecW : fbW_);
    int rh = shot ? opts_.shotH : (recordSize ? kRecH : fbH_);
    if (rw > 0 && rh > 0) {
      CameraView cam = world_.cameraView(rw / (float)rh);
      renderer_.render(world_, cam, rw, rh);
      info.renderW = rw;
      info.renderH = rh;
      info.stats = renderer_.stats();
    }
    pumpRecording(now);
    if (capturing_ && !world_.recording) {
      finishRecording(opts_.recordTest <= 0.f);
      if (opts_.recordTest > 0.f) break;
    }

    if (shot && !opts_.shotHud && ++frame >= opts_.shotFrames) {
      glFinish();
      bool ok = renderer_.saveScreenshot(opts_.shotPath);
      std::cout << (ok ? "Saved " : "FAILED to save ") << opts_.shotPath << "  (" << info.stats.drawCalls
                << " draws, " << info.stats.lights << " lights, " << info.stats.shadowMaps << " shadow maps)\n";
      break;
    }

    renderer_.present(fbW_, fbH_);
    hud_.begin();
    hud_.draw(world_, info);
    hud_.end();
    if (shot && opts_.shotHud && ++frame >= opts_.shotFrames) {
      // capture the window back buffer, control panel included
      std::vector<unsigned char> px((size_t)fbW_ * fbH_ * 4);
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
      glReadBuffer(GL_BACK);
      glPixelStorei(GL_PACK_ALIGNMENT, 1);
      glReadPixels(0, 0, fbW_, fbH_, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
      bool ok = writePng(opts_.shotPath, fbW_, fbH_, px);
      std::cout << (ok ? "Saved " : "FAILED to save ") << opts_.shotPath << " (with HUD)\n";
      break;
    }
    glfwSwapBuffers(window_);
  }
}

void Application::shutdown() {
  if (world_.recording) world_.stopRecording();
  if (capturing_) finishRecording(false);
  world_.shutdown();
  hud_.shutdown();
  renderer_.shutdown();
  if (window_) glfwDestroyWindow(window_);
  glfwTerminate();
}

}  // namespace og
