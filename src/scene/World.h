#pragma once
#include "actors/Frog.h"
#include "audio/AudioEngine.h"
#include "camera/OrbitCamera.h"
#include "camera/RailCamera.h"
#include "camera/Recorder.h"
#include "instruments/Orchestra.h"
#include "lighting/LightingRig.h"
#include "net/NetClient.h"
#include "render/Renderer.h"
#include "scene/Primitives.h"
#include <memory>
#include <string>
#include <vector>

namespace og {

enum class Zone { City, FadeOut, Establishing, Auditorium, FadeToCity };
enum class Quality { Low = 0, Medium = 1, High = 2 };
enum class LightView { Walk = 0, TopDown = 1, FromLight = 2 };

struct World {
  // ---- state
  Zone zone = Zone::City;
  Quality quality = Quality::High;
  float time = 0.f;
  float fade = 0.f;
  float establishT = 0.f;
  float timeOfDay = 0.72f;  // 0 afternoon .. 1 blue hour (W5)
  int toneMap = 0;          // 0 AgX, 1 ACES
  bool fxaa = true;
  float houseEV = 8.0f;     // exposure for the auditorium (EV100)

  SceneData city, house;
  std::vector<SeatSpot> audienceSeats;

  FrogActor localPlayer;
  std::vector<FrogActor> remotePlayers;
  std::vector<FrogActor> npcs;  // idle orchestra fillers (R4)
  Orchestra orchestra;
  LightingRig rig;

  OrbitCamera orbit;
  RailCamera rails;
  LightView lightView = LightView::Walk;
  glm::vec2 topDownCursor{0.f, -5.f};
  bool mouseLook = false;

  AudioEngine audio;
  SongPlayer song;
  NetClient net;
  std::unique_ptr<Recorder> recorder;
  bool recording = false;
  double recordStartWall = 0.0;  // glfwGetTime() when the audio tap opened: video t=0
  bool remoteRecording = false;   // someone else in the room is recording (C6)
  std::string remoteRecorderName;
  std::string lastSavedPath;

  int octaveBase = 60;
  bool spaceVelocity = false;
  bool pushToSing = false;
  bool micMuted = false;
  bool hideHud = false;

  int lightingOwner = -1;  // M4: player id with the lighting desk, -1 = free
  uint32_t sentRigRevision = 0;
  float netSendTimer = 0.f;

  std::string status;
  float statusTimer = 0.f;
  bool copyrightAck = false;

  // ---- lifecycle
  bool init();
  void shutdown();
  void handleInput(float dt);
  void update(float dt);
  CameraView cameraView(float aspect) const;
  void collectDraw(std::vector<SceneObject>& dynamicObjs) const;
  bool outdoors() const { return zone == Zone::City || zone == Zone::FadeOut; }
  const SceneData& activeScene() const { return outdoors() ? city : house; }

  // ---- actions (shared by keyboard and HUD buttons)
  void setRole(Role r);
  void enterOpera();
  void returnToPlaza();
  void claimSeat(int seat);
  void leaveSeat();
  void takeAudienceSeat();
  void toggleSit();
  void startRecording();
  void stopRecording();
  bool canEditLights() const;
  void requestLightingDesk();
  void releaseLightingDesk();
  void playLocalNote(int midi, float vel, bool on);
  void startSong();
  void stopSong();
  void setStatus(const std::string& msg, float seconds = 3.f);
  std::vector<glm::vec3> performerPositions() const;
  std::vector<std::string> performerNames() const;
  FrogActor* frogById(uint32_t id);
  FrogActor* frogAtSeat(int seat);
  void refreshNpcs();

 private:
  void walk(float dt);
  void clampToVenue(glm::vec3& p) const;
  float floorHeight(float x, float z) const;
  void handleNet(float dt);
  void onSongNote(const NoteEvent& e);
};

}  // namespace og
