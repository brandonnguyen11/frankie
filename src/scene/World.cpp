#include "scene/World.h"
#include "core/AssetPath.h"
#include "core/Input.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace og {

namespace {

constexpr uint32_t kOfflineId = 1000;
constexpr uint32_t kNpcBase = 50000;
const glm::vec3 kSingerMarks[2] = {{-2.2f, 1.1f, -5.f}, {2.2f, 1.1f, -5.f}};
const glm::vec3 suitPalette[6] = {{0.05f, 0.06f, 0.11f}, {0.12f, 0.03f, 0.04f}, {0.03f, 0.08f, 0.06f},
                                  {0.09f, 0.08f, 0.07f}, {0.02f, 0.02f, 0.02f}, {0.1f, 0.06f, 0.12f}};

float smoothDamp(float cur, float target, float rate, float dt) {
  return cur + (target - cur) * (1.f - std::exp(-rate * dt));
}

// Optional assets/models/models.txt:  <city|house> <file> <scale> <x> <y> <z> <yawDeg> [replace]
void loadModelManifest(SceneData& city, SceneData& house) {
  std::ifstream f(assetPath("models/models.txt"));
  if (!f) return;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string zone, file, mode;
    float s = 1, x = 0, y = 0, z = 0, yaw = 0;
    if (!(ss >> zone >> file >> s >> x >> y >> z >> yaw)) continue;
    ss >> mode;
    SceneData& target = zone == "city" ? city : house;
    SceneData loaded;
    glm::mat4 xf = glm::rotate(glm::translate(glm::mat4(1), {x, y, z}), glm::radians(yaw), {0, 1, 0}) *
                   glm::scale(glm::mat4(1), glm::vec3(s));
    if (!loadModelInto(assetPath("models/" + file), loaded, xf)) continue;
    if (mode == "replace") target.objects.clear();
    for (auto& o : loaded.objects) target.objects.push_back(o);
  }
}

}  // namespace

// ====================================================================== lifecycle

bool World::init() {
  city = buildCity();
  house = buildAuditorium(&audienceSeats);
  loadModelManifest(city, house);
  orchestra.buildDefault();
  rig.buildMounts();
  rig.applyDefaultAgent327();
  rails.buildDefaultRails();

  localPlayer.id = kOfflineId;
  localPlayer.name = "You";
  localPlayer.local = true;
  localPlayer.position = {0, 0, 24};
  localPlayer.yaw = 180.f;
  localPlayer.suitColor = suitPalette[0];
  localPlayer.role = Role::Audience;
  orbit.setFollow(localPlayer.position);

  audio.init(assetPath("samples"));
  recorder = Recorder::create();
  refreshNpcs();
  setStatus("Walk up the steps to the Garnier doors (WASD, Shift to run, Tab or right-drag to look)", 8.f);
  return true;
}

void World::shutdown() {
  if (recording) {
    std::string err;
    recorder->stop(err);
    recording = false;
  }
  net.disconnect();
  audio.shutdown();
}

void World::setStatus(const std::string& msg, float seconds) {
  status = msg;
  statusTimer = seconds;
}

// ====================================================================== zones

void World::enterOpera() {
  if (zone != Zone::City) return;
  zone = Zone::FadeOut;
  fade = 0.f;
}

void World::returnToPlaza() {
  if (zone != Zone::Auditorium) return;
  leaveSeat();
  zone = Zone::FadeToCity;
  fade = 0.f;
}

// ====================================================================== roles

void World::setRole(Role r) {
  if (r == localPlayer.role) return;
  Role prev = localPlayer.role;
  if (prev == Role::Musician) leaveSeat();
  if (prev == Role::LightingDesigner && lightingOwner == (int)localPlayer.id) releaseLightingDesk();
  if (prev == Role::CameraOperator && recording) stopRecording();
  localPlayer.role = r;
  localPlayer.sitting = false;
  lightView = LightView::Walk;
  pushToSing = false;
  audio.setPushToSing(false);
  if (zone != Zone::Auditorium) {
    setStatus(std::string("Role: ") + roleName(r) + " (takes effect inside the opera house)");
    return;
  }
  switch (r) {
    case Role::Musician:
      setStatus("Musician: walk onto the stage and press E at a chair, or pick a seat in the panel", 6.f);
      break;
    case Role::Singer: {
      int taken[2] = {0, 0};
      for (auto& f : remotePlayers)
        if (f.role == Role::Singer)
          for (int m = 0; m < 2; ++m)
            if (glm::distance(f.position, kSingerMarks[m]) < 1.2f) taken[m] = 1;
      int mark = !taken[0] ? 0 : (!taken[1] ? 1 : -1);
      if (mark < 0) {
        localPlayer.role = prev;
        setStatus("Both singer spots are taken (max 2 singers)");
        return;
      }
      localPlayer.position = kSingerMarks[mark];
      localPlayer.yaw = 0.f;
      setStatus(audio.micAvailable() ? "Singer: hold G (or Option) to sing, M mute. Headphones recommended!"
                                     : "Singer: no microphone found - check System Settings > Privacy > Microphone",
                6.f);
      break;
    }
    case Role::LightingDesigner:
      if (lightingOwner < 0 || !net.connected()) requestLightingDesk();
      setStatus("Lighting: 1 walk / 2 top-down / 3 through the lens, [ ] select light, click stage to aim", 6.f);
      break;
    case Role::CameraOperator:
      rails.switchRail(rails.activeRail);
      setStatus("Camera: 1-5 rails, A/D dolly, mouse pan, Q/E zoom, R/F focus, Z/X aperture, V record", 6.f);
      break;
    case Role::Audience:
      setStatus("Audience: E near a seat (or the panel button) to sit; C to stand", 5.f);
      break;
    default: break;
  }
}

void World::claimSeat(int seat) {
  if (zone != Zone::Auditorium || localPlayer.role != Role::Musician) return;
  if (!orchestra.claim(seat, (int)localPlayer.id)) {
    setStatus("That chair is taken");
    return;
  }
  auto& s = orchestra.seats()[seat];
  localPlayer.seatIndex = seat;
  localPlayer.instrument = s.instrument;
  localPlayer.position = s.position;
  localPlayer.yaw = s.yaw;
  localPlayer.sitting = true;
  audio.allNotesOff(localPlayer.id);
  refreshNpcs();
  setStatus(std::string("Seated: ") + s.label + " - keys A S D F G H J K / W E T Y U, Z X octave, Space loud, Backspace to stand up",
            7.f);
}

void World::leaveSeat() {
  if (localPlayer.seatIndex < 0) return;
  audio.allNotesOff(localPlayer.id);
  orchestra.release((int)localPlayer.id);
  glm::vec3 p = orchestraLayout()[localPlayer.seatIndex].position;
  localPlayer.seatIndex = -1;
  localPlayer.sitting = false;
  localPlayer.playing = false;
  localPlayer.position = p + glm::vec3(0, 0, -0.6f);
  refreshNpcs();
}

void World::takeAudienceSeat() {
  if (zone != Zone::Auditorium || audienceSeats.empty()) return;
  const SeatSpot* best = nullptr;
  float bd = 1e9f;
  for (auto& s : audienceSeats) {
    bool occupied = false;
    for (auto& r : remotePlayers)
      if (r.sitting && glm::distance(r.position, s.position) < 0.3f) occupied = true;
    if (occupied) continue;
    float d = glm::distance(s.position, localPlayer.position);
    if (d < bd) { bd = d; best = &s; }
  }
  if (!best) return;
  localPlayer.position = best->position;
  localPlayer.yaw = best->yaw;
  localPlayer.sitting = true;
  setStatus("Enjoy the show (C to stand)");
}

void World::toggleSit() {
  if (localPlayer.role == Role::CameraOperator) return;
  localPlayer.sitting = !localPlayer.sitting;
}

// ====================================================================== camera rigs

void World::setCamMode(CamMode m) {
  if (m == camMode) return;
  CamMode prev = camMode;
  camMode = m;
  if (m == CamMode::Rails) return;
  // start the new rig where the current camera is, looking the same way
  glm::vec3 p = prev == CamMode::Rails ? rails.position() : fly.position;
  glm::vec3 f = prev == CamMode::Rails ? rails.orientation * glm::vec3(0, 0, -1) : fly.forward();
  fly.velocity = glm::vec3(0.f);
  fly.position = p;
  fly.yaw = glm::degrees(std::atan2(f.z, f.x));
  fly.pitch = glm::degrees(std::asin(std::clamp(f.y, -1.f, 1.f)));
  fly.orientation = lookRotation(f);
  // grab the mouse so looking works straight away (Esc releases it)
  mouseLook = true;
  if (GLFWwindow* win = glfwGetCurrentContext()) glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  if (m == CamMode::Drone) {
    fly.target = FlyCamera::stageCentre();
    droneAutoFocus = true;
    setStatus("Drone: WASD fly, Space/C up/down, mouse slides the lock-on point across the stage", 6.f);
  } else {
    setStatus("Free 360: Tab or right-drag to look around, WASD fly, Space/C up/down, Shift slow", 6.f);
  }
}

const char* World::camModeName() const {
  switch (camMode) {
    case CamMode::Free: return "Free 360";
    case CamMode::Drone: return "Drone - locked on stage";
    default: return rails.rails.empty() ? "Rails" : rails.rails[rails.activeRail].name.c_str();
  }
}

glm::vec3 World::operatorPosition() const { return camMode == CamMode::Rails ? rails.position() : fly.eye(); }
glm::vec3 World::operatorForward() const {
  return camMode == CamMode::Rails ? rails.orientation * glm::vec3(0, 0, -1) : fly.forward();
}

// Keep a flying camera inside the auditorium: horseshoe walls, dome, stage house,
// and the proscenium wall (you can only pass through the opening).
void World::clampCamera(glm::vec3& p, const glm::vec3& prev) const {
  const float wallZ = -3.2f;
  bool crossing = (prev.z >= wallZ) != (p.z >= wallZ);
  if (crossing && (std::abs(p.x) > 7.0f || p.y > 9.3f)) p.z = prev.z >= wallZ ? wallZ + 0.15f : wallZ - 0.15f;
  if (p.z >= wallZ) {
    glm::vec2 d = glm::vec2(p.x, p.z) - glm::vec2(0.f, 5.f);
    float r = glm::length(d);
    if (r > 14.0f) { d *= 14.0f / r; p.x = d.x; p.z = 5.f + d.y; }
    p.y = std::min(p.y, 17.5f);
  } else {
    p.x = std::clamp(p.x, -10.4f, 10.4f);
    p.z = std::max(p.z, -13.4f);
    p.y = std::min(p.y, 15.5f);
  }
  p.y = std::max(p.y, floorHeight(p.x, p.z) + 0.35f);
}

// ====================================================================== lighting desk (M4)

bool World::canEditLights() const {
  return localPlayer.role == Role::LightingDesigner &&
         (!net.connected() || lightingOwner < 0 || lightingOwner == (int)localPlayer.id);
}

void World::requestLightingDesk() {
  lightingOwner = (int)localPlayer.id;
  if (net.connected()) {
    int32_t id = lightingOwner;
    net.send(makePacket(MsgType::Desk, localPlayer.id, &id, 4));
    sentRigRevision = 0;  // push our view of the rig
  }
}

void World::releaseLightingDesk() {
  if (lightingOwner != (int)localPlayer.id) return;
  lightingOwner = -1;
  if (net.connected()) {
    int32_t none = -1;
    net.send(makePacket(MsgType::Desk, localPlayer.id, &none, 4));
  }
}

// ====================================================================== music

void World::playLocalNote(int midi, float vel, bool on) {
  InstrumentId inst = localPlayer.instrument;
  if (on) {
    audio.noteOn(inst, midi, vel, localPlayer.id);
    localPlayer.onNote(vel);
  } else {
    audio.noteOff(inst, midi, localPlayer.id);
  }
  if (net.connected()) {
    NotePayload n{(uint8_t)inst, (uint8_t)midi, (uint8_t)(vel * 127.f), (uint8_t)(on ? 1 : 0),
                  (int8_t)localPlayer.seatIndex};
    net.send(makePacket(MsgType::Note, localPlayer.id, n));
  }
}

void World::startSong() {
  if (song.loaded) {
    song.start(audio.clock());
    if (net.connected()) {
      SongPayload sp{1, 1, {}};
      std::strncpy(sp.title, song.song.title.c_str(), sizeof(sp.title) - 1);
      net.send(makePacket(MsgType::Song, localPlayer.id, sp));
    }
    setStatus("Playing " + song.song.title);
  } else if (audio.audioFileLoaded()) {
    audio.playAudioFile(true);
    if (net.connected()) {
      SongPayload sp{1, 0, {}};
      net.send(makePacket(MsgType::Song, localPlayer.id, sp));
    }
  }
}

void World::stopSong() {
  if (song.playing) {
    song.stop();
    for (auto& s : orchestra.seats()) audio.allNotesOff(kNpcBase + s.index);
  }
  audio.playAudioFile(false);
  if (net.connected()) {
    SongPayload sp{0, 0, {}};
    net.send(makePacket(MsgType::Song, localPlayer.id, sp));
  }
}

void World::onSongNote(const NoteEvent& e) {
  int seat = e.part >= 0 && e.part < (int)song.song.parts.size() ? song.song.parts[e.part].seat : -1;
  if (seat < 0) return;  // part muted
  uint32_t source = kNpcBase + seat;
  if (e.on) audio.noteOn(e.instrument, e.midiNote, e.velocity, source, e.time);
  else audio.noteOff(e.instrument, e.midiNote, source, e.time);
  if (e.on) {
    if (FrogActor* f = frogAtSeat(seat)) f->onNote(e.velocity);
  }
  if (net.connected()) {
    NotePayload n{(uint8_t)e.instrument, (uint8_t)e.midiNote, (uint8_t)(e.velocity * 127.f), (uint8_t)(e.on ? 1 : 0),
                  (int8_t)seat};
    net.send(makePacket(MsgType::Note, localPlayer.id, n));
  }
}

// ====================================================================== recording

void World::startRecording() {
  if (recording) return;
  std::string path = defaultRecordingPath();
  std::string err;
  if (!recorder->start(path, 1920, 1080, 30, AudioEngine::kRate, 2, err)) {
    setStatus("Recording failed: " + err, 8.f);
    return;
  }
  audio.setRecordTap(true);
  recordStartWall = glfwGetTime();  // audio and video timelines share this origin
  recording = true;
  if (net.connected()) {
    uint8_t on = 1;
    net.send(makePacket(MsgType::RecFlag, localPlayer.id, &on, 1));
  }
  setStatus(" REC - 1080p30 to " + path, 4.f);
}

void World::stopRecording() {
  if (!recording) return;
  recording = false;  // Application flushes pending frames, then calls finishRecording
  audio.setRecordTap(false);
  if (net.connected()) {
    uint8_t off = 0;
    net.send(makePacket(MsgType::RecFlag, localPlayer.id, &off, 1));
  }
}

// ====================================================================== performers

FrogActor* World::frogById(uint32_t id) {
  if (localPlayer.id == id) return &localPlayer;
  for (auto& r : remotePlayers)
    if (r.id == id) return &r;
  return nullptr;
}

FrogActor* World::frogAtSeat(int seat) {
  if (seat < 0) return nullptr;
  if (localPlayer.seatIndex == seat) return &localPlayer;
  for (auto& r : remotePlayers)
    if (r.seatIndex == seat) return &r;
  for (auto& n : npcs)
    if (n.seatIndex == seat) return &n;
  return nullptr;
}

void World::refreshNpcs() {
  std::vector<int> taken;
  if (localPlayer.seatIndex >= 0) taken.push_back(localPlayer.seatIndex);
  for (auto& r : remotePlayers)
    if (r.role == Role::Musician && r.seatIndex >= 0) taken.push_back(r.seatIndex);
  std::vector<FrogActor> next;
  for (auto& s : orchestra.seats()) {
    if (std::find(taken.begin(), taken.end(), s.index) != taken.end()) continue;
    FrogActor* existing = nullptr;
    for (auto& n : npcs)
      if (n.seatIndex == s.index) existing = &n;
    if (existing) { next.push_back(*existing); continue; }
    FrogActor f;
    f.npc = true;
    f.id = kNpcBase + s.index;
    f.name = s.label;
    f.role = Role::Musician;
    f.seatIndex = s.index;
    f.instrument = s.instrument;
    f.position = s.position;
    f.yaw = s.yaw;
    f.sitting = true;
    f.sitBlend = 1.f;
    f.anim = s.index * 1.3f;
    f.suitColor = suitPalette[(s.index * 5) % 6];
    f.skinColor = glm::vec3(0.24f, 0.52f, 0.16f) * (0.8f + 0.06f * (s.index % 5));
    next.push_back(f);
  }
  npcs = std::move(next);
}

std::vector<glm::vec3> World::performerPositions() const {
  std::vector<glm::vec3> out;
  auto add = [&](const FrogActor& f) {
    if (f.role == Role::Singer || (f.role == Role::Musician && f.seatIndex >= 0)) out.push_back(f.position);
  };
  // singers first so the warm followspots find them
  if (localPlayer.role == Role::Singer) out.push_back(localPlayer.position);
  for (auto& r : remotePlayers)
    if (r.role == Role::Singer) out.push_back(r.position);
  while (out.size() < 2) out.push_back(kSingerMarks[out.size()]);
  if (localPlayer.role == Role::Musician) add(localPlayer);
  for (auto& r : remotePlayers)
    if (r.role == Role::Musician) add(r);
  for (auto& n : npcs) add(n);
  return out;
}

std::vector<std::string> World::performerNames() const {
  std::vector<std::string> out;
  if (localPlayer.role == Role::Singer) out.push_back("You (singer)");
  for (auto& r : remotePlayers)
    if (r.role == Role::Singer) out.push_back(r.name + " (singer)");
  while (out.size() < 2) out.push_back(out.size() == 0 ? "Singer mark L" : "Singer mark R");
  if (localPlayer.role == Role::Musician && localPlayer.seatIndex >= 0) out.push_back("You (" + std::string(instrumentName(localPlayer.instrument)) + ")");
  for (auto& r : remotePlayers)
    if (r.role == Role::Musician && r.seatIndex >= 0) out.push_back(r.name);
  for (auto& n : npcs) out.push_back(n.name);
  return out;
}

// ====================================================================== movement

float World::floorHeight(float x, float z) const {
  if (outdoors()) {
    if (std::abs(x) > 21.f || z > 5.f) return 0.f;
    if (z <= 1.f) return 1.2f;
    int step = (int)std::floor((5.f - z) / 0.5f);
    return std::clamp(step + 1, 0, 8) * 0.15f;
  }
  if (z < -8.6f && z > -10.6f && std::abs(x) < 5.8f) return 1.5f;  // orchestra riser
  if (z < -1.f && std::abs(x) < 11.f) return 1.1f;
  if (z < 3.3f && std::abs(x) < 9.f) return -0.5f;
  return std::clamp((z - 3.5f) * 0.11f, 0.f, 1.95f);
}

void World::clampToVenue(glm::vec3& p) const {
  if (outdoors()) {
    p.x = std::clamp(p.x, -26.f, 26.f);
    p.z = std::clamp(p.z, 0.25f, 32.5f);
    return;
  }
  // stage house / proscenium
  if (p.z < -1.f) {
    p.x = std::clamp(p.x, -10.6f, 10.6f);
    p.z = std::max(p.z, -13.6f);
    if (p.z < -3.f && p.z > -3.5f && std::abs(p.x) > 7.1f) p.z = -3.f;
  }
  // horseshoe walls around the house
  glm::vec2 c{0.f, 5.f};
  glm::vec2 d = glm::vec2(p.x, p.z) - c;
  float r = glm::length(d);
  if (p.z > -1.f && r > 14.3f) {
    d *= 14.3f / r;
    p.x = c.x + d.x;
    p.z = c.y + d.y;
  }
  p.z = std::min(p.z, 21.5f);
}

void World::walk(float dt) {
  auto& in = Input::instance();
  bool run = in.down(GLFW_KEY_LEFT_SHIFT) || in.down(GLFW_KEY_RIGHT_SHIFT);
  float speed = run ? 5.f : 2.2f;
  glm::vec3 toCam = orbit.position() - orbit.target;
  toCam.y = 0.f;
  glm::vec3 fwd = glm::length(toCam) > 1e-3f ? -glm::normalize(toCam) : glm::vec3(0, 0, -1);
  glm::vec3 right = glm::normalize(glm::cross(fwd, glm::vec3(0, 1, 0)));
  glm::vec3 v{0.f};
  if (in.down(GLFW_KEY_W) || in.down(GLFW_KEY_UP)) v += fwd;
  if (in.down(GLFW_KEY_S) || in.down(GLFW_KEY_DOWN)) v -= fwd;
  if (in.down(GLFW_KEY_A) || in.down(GLFW_KEY_LEFT)) v -= right;
  if (in.down(GLFW_KEY_D) || in.down(GLFW_KEY_RIGHT)) v += right;
  float moving = 0.f;
  if (glm::length(v) > 1e-3f) {
    if (localPlayer.sitting) localPlayer.sitting = false;
    v = glm::normalize(v);
    glm::vec3 before = localPlayer.position;
    localPlayer.position += v * speed * dt;
    clampToVenue(localPlayer.position);
    moving = glm::length(glm::vec2(localPlayer.position.x - before.x, localPlayer.position.z - before.z)) / dt;
    float targetYaw = glm::degrees(std::atan2(v.x, v.z));
    float dy = std::remainder(targetYaw - localPlayer.yaw, 360.f);
    localPlayer.yaw += dy * (1.f - std::exp(-12.f * dt));
  }
  localPlayer.speed = smoothDamp(localPlayer.speed, moving, 10.f, dt);
  float fy = floorHeight(localPlayer.position.x, localPlayer.position.z);
  localPlayer.position.y = smoothDamp(localPlayer.position.y, fy, 14.f, dt);

  if (zone == Zone::City) {
    bool atDoors = std::abs(localPlayer.position.x) < 1.8f && localPlayer.position.z < 0.6f;
    bool nearDoors = std::abs(localPlayer.position.x) < 2.5f && localPlayer.position.z < 2.5f;
    if (atDoors || (nearDoors && (in.pressed(GLFW_KEY_E) || in.pressed(GLFW_KEY_ENTER)))) enterOpera();
  }
}

// ====================================================================== input

void World::handleInput(float dt) {
  auto& in = Input::instance();
  GLFWwindow* win = glfwGetCurrentContext();

  if (in.pressed(GLFW_KEY_TAB)) {
    mouseLook = !mouseLook;
    glfwSetInputMode(win, GLFW_CURSOR, mouseLook ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
  }
  if (in.pressed(GLFW_KEY_ESCAPE) && mouseLook) {
    mouseLook = false;
    glfwSetInputMode(win, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }
  bool looking = mouseLook || in.state().rightDown;
  float mdx = looking ? (float)in.state().mouseDX : 0.f;
  float mdy = looking ? (float)in.state().mouseDY : 0.f;

  if (in.pressed(GLFW_KEY_F1)) quality = Quality::Low;
  if (in.pressed(GLFW_KEY_F2)) quality = Quality::Medium;
  if (in.pressed(GLFW_KEY_F3)) quality = Quality::High;
  if (in.pressed(GLFW_KEY_F10)) hideHud = !hideHud;

  if (zone == Zone::Establishing) {
    for (int k = 32; k < 349; ++k)
      if (in.pressed(k)) { establishT = std::max(establishT, 10.f); break; }
    return;
  }
  if (zone == Zone::FadeOut || zone == Zone::FadeToCity) return;

  const bool inHouse = zone == Zone::Auditorium;
  const Role role = localPlayer.role;

  // ---------------- camera operator on the rails
  if (inHouse && role == Role::CameraOperator) {
    bool slow = in.down(GLFW_KEY_LEFT_SHIFT);
    for (int i = 0; i < 5; ++i)
      if (in.pressed(GLFW_KEY_1 + i)) { setCamMode(CamMode::Rails); rails.switchRail(i); }
    if (in.pressed(GLFW_KEY_6)) setCamMode(CamMode::Free);
    if (in.pressed(GLFW_KEY_7)) setCamMode(CamMode::Drone);
    if (camMode == CamMode::Rails) {
      float move = (in.down(GLFW_KEY_D) ? 1.f : 0.f) - (in.down(GLFW_KEY_A) ? 1.f : 0.f);
      rails.slowPan = slow;
      if (railLock) {
        // stage lock: the head turns to keep the aim point framed while the dolly slides;
        // the mouse moves the aim point instead of the head
        fly.target.x = std::clamp(fly.target.x + mdx * 0.012f * (slow ? 0.3f : 1.f), -8.f, 8.f);
        fly.target.y = std::clamp(fly.target.y - mdy * 0.008f * (slow ? 0.3f : 1.f), 0.8f, 7.f);
        rails.update(dt, move, 0.f, 0.f);
        glm::vec3 d = glm::normalize(fly.target - rails.position());
        rails.yaw = glm::degrees(std::atan2(d.z, d.x));
        rails.pitch = glm::degrees(std::asin(std::clamp(d.y, -1.f, 1.f)));
        rails.orientation = glm::slerp(rails.orientation, rails.targetOrientation(), 1.f - std::exp(-dt * 12.f));
        if (droneAutoFocus) {
          float fd = glm::distance(rails.position(), fly.target);
          rails.focusDistance += (fd - rails.focusDistance) * (1.f - std::exp(-dt * 6.f));
        }
      } else {
        rails.update(dt, move, mdx, mdy);
      }
    } else {
      glm::vec3 mv{(in.down(GLFW_KEY_D) ? 1.f : 0.f) - (in.down(GLFW_KEY_A) ? 1.f : 0.f),
                   (in.down(GLFW_KEY_SPACE) ? 1.f : 0.f) - (in.down(GLFW_KEY_C) ? 1.f : 0.f),
                   (in.down(GLFW_KEY_W) ? 1.f : 0.f) - (in.down(GLFW_KEY_S) ? 1.f : 0.f)};
      glm::vec3 prev = fly.position;
      fly.update(dt, camMode == CamMode::Free ? FlyCamera::Mode::Free : FlyCamera::Mode::Drone, mv,
                 mdx * (slow ? 0.3f : 1.f), mdy * (slow ? 0.3f : 1.f), slow);
      clampCamera(fly.position, prev);
      rails.update(0.f, 0.f, 0.f, 0.f);  // keeps lens values clamped
      if (camMode == CamMode::Drone && droneAutoFocus) {
        float d = glm::distance(fly.eye(), fly.target);
        rails.focusDistance += (d - rails.focusDistance) * (1.f - std::exp(-dt * 6.f));  // focus locked on the stage
      }
    }
    float zoomRate = rails.focalLengthMm * 0.8f;
    if (in.down(GLFW_KEY_Q)) rails.focalLengthMm -= zoomRate * dt;
    if (in.down(GLFW_KEY_E)) rails.focalLengthMm += zoomRate * dt;
    float pull = std::max(1.f, rails.focusDistance) * 0.9f;
    if (in.down(GLFW_KEY_R)) { rails.focusDistance += pull * dt; droneAutoFocus = false; }
    if (in.down(GLFW_KEY_F)) { rails.focusDistance -= pull * dt; droneAutoFocus = false; }
    if (in.pressed(GLFW_KEY_Z)) rails.fStop = std::max(1.4f, rails.fStop / 1.41421f);
    if (in.pressed(GLFW_KEY_X)) rails.fStop = std::min(16.f, rails.fStop * 1.41421f);
    if (in.pressed(GLFW_KEY_COMMA)) rails.evOffset += 0.33f;   // darker
    if (in.pressed(GLFW_KEY_PERIOD)) rails.evOffset -= 0.33f;  // brighter
    if (in.pressed(GLFW_KEY_T)) {
      // autofocus on the nearest performer in the centre of frame
      droneAutoFocus = true;
      glm::vec3 p = operatorPosition();
      glm::vec3 fwd = operatorForward();
      float best = 1e9f;
      for (auto& t : performerPositions()) {
        glm::vec3 d = t + glm::vec3(0, 1.3f, 0) - p;
        float along = glm::dot(d, fwd);
        if (along <= 0.f) continue;
        float off = glm::length(d - fwd * along) / along;
        if (off < best) { best = off; rails.focusDistance = along; }
      }
    }
    if (in.pressed(GLFW_KEY_V)) recording ? stopRecording() : startRecording();
  }
  // ---------------- seated musician: the keyboard is an instrument
  else if (inHouse && role == Role::Musician && localPlayer.seatIndex >= 0) {
    float vel = in.down(GLFW_KEY_SPACE) ? 1.f : 0.65f;
    bool any = false;
    for (int key : {'A', 'W', 'S', 'E', 'D', 'F', 'T', 'G', 'Y', 'H', 'U', 'J', 'K'}) {
      int midi = AudioEngine::keyToMidi(key, octaveBase);
      if (in.down(key)) any = true;
      if (in.pressed(key)) playLocalNote(midi, vel, true);
      if (in.released(key)) playLocalNote(midi, vel, false);
    }
    localPlayer.playing = any;
    if (in.pressed(GLFW_KEY_Z)) { octaveBase = std::max(24, octaveBase - 12); setStatus("Octave " + std::to_string(octaveBase / 12 - 1)); }
    if (in.pressed(GLFW_KEY_X)) { octaveBase = std::min(96, octaveBase + 12); setStatus("Octave " + std::to_string(octaveBase / 12 - 1)); }
    if (in.pressed(GLFW_KEY_BACKSPACE)) leaveSeat();
    if (looking) orbit.orbit(mdx, mdy);
  }
  // ---------------- lighting designer in a desk view
  else if (inHouse && role == Role::LightingDesigner && lightView != LightView::Walk) {
    if (!rig.lights.empty() && canEditLights()) {
      auto& L = rig.lights[std::clamp(rig.selected, 0, (int)rig.lights.size() - 1)];
      if (lightView == LightView::FromLight) {
        // pan/tilt the fixture with the mouse or arrow keys
        float yawD = mdx * 0.08f + ((in.down(GLFW_KEY_RIGHT) ? 40.f : 0.f) - (in.down(GLFW_KEY_LEFT) ? 40.f : 0.f)) * dt;
        float pitD = -mdy * 0.08f + ((in.down(GLFW_KEY_UP) ? 40.f : 0.f) - (in.down(GLFW_KEY_DOWN) ? 40.f : 0.f)) * dt;
        if (yawD != 0.f || pitD != 0.f) {
          glm::vec3 d = L.direction;
          float yaw = std::atan2(d.x, d.z) - glm::radians(yawD);
          float pit = std::clamp(std::asin(std::clamp(d.y, -1.f, 1.f)) + glm::radians(pitD), -1.55f, 1.2f);
          L.direction = {std::sin(yaw) * std::cos(pit), std::sin(pit), std::cos(yaw) * std::cos(pit)};
          L.followTarget = -1;
          rig.markDirty();
        }
      } else {
        float pan = 8.f * dt;
        if (in.down(GLFW_KEY_W) || in.down(GLFW_KEY_UP)) topDownCursor.y -= pan;
        if (in.down(GLFW_KEY_S) || in.down(GLFW_KEY_DOWN)) topDownCursor.y += pan;
        if (in.down(GLFW_KEY_A) || in.down(GLFW_KEY_LEFT)) topDownCursor.x -= pan;
        if (in.down(GLFW_KEY_D) || in.down(GLFW_KEY_RIGHT)) topDownCursor.x += pan;
        topDownCursor = glm::clamp(topDownCursor, glm::vec2(-12, -13), glm::vec2(12, 18));
        if (in.state().leftClicked && !in.state().uiWantsMouse) {
          // pick the point on the stage/pit floor under the cursor and aim there
          int ww, wh;
          glfwGetWindowSize(win, &ww, &wh);
          float aspect = ww / (float)std::max(1, wh);
          CameraView cv = cameraView(aspect);
          glm::vec2 ndc{(float)in.state().mouseX / ww * 2.f - 1.f, 1.f - (float)in.state().mouseY / wh * 2.f};
          glm::mat4 inv = glm::inverse(cv.proj * cv.view);
          glm::vec4 a = inv * glm::vec4(ndc, -1, 1), b = inv * glm::vec4(ndc, 1, 1);
          glm::vec3 ro = glm::vec3(a) / a.w, rd = glm::normalize(glm::vec3(b) / b.w - ro);
          float y = 1.1f;
          float t = (y - ro.y) / rd.y;
          glm::vec3 hit = ro + rd * t;
          float fy = floorHeight(hit.x, hit.z);
          hit = ro + rd * ((fy - ro.y) / rd.y);
          L.aimAt(hit + glm::vec3(0, 0.8f, 0));
          L.followTarget = -1;
          rig.markDirty();
        }
      }
    }
    if (in.pressed(GLFW_KEY_LEFT_BRACKET) && !rig.lights.empty())
      rig.selected = (rig.selected + (int)rig.lights.size() - 1) % (int)rig.lights.size();
    if (in.pressed(GLFW_KEY_RIGHT_BRACKET) && !rig.lights.empty()) rig.selected = (rig.selected + 1) % (int)rig.lights.size();
    if (in.pressed(GLFW_KEY_1)) lightView = LightView::Walk;
    if (in.pressed(GLFW_KEY_2)) lightView = LightView::TopDown;
    if (in.pressed(GLFW_KEY_3)) lightView = LightView::FromLight;
  }
  // ---------------- everyone else walks
  else {
    if (looking) orbit.orbit(mdx, mdy);
    orbit.zoom((float)in.state().scrollY);
    walk(dt);
    if (inHouse) {
      if (role == Role::Musician && in.pressed(GLFW_KEY_E)) {
        int s = orchestra.nearestFree(localPlayer.position, 2.0f, (int)localPlayer.id);
        if (s >= 0) claimSeat(s);
        else setStatus("Walk up to a free chair on the stage (behind the singers)");
      }
      if (role == Role::Audience && in.pressed(GLFW_KEY_E)) takeAudienceSeat();
      if (role == Role::LightingDesigner) {
        if (in.pressed(GLFW_KEY_2)) lightView = LightView::TopDown;
        if (in.pressed(GLFW_KEY_3)) lightView = LightView::FromLight;
        if (in.pressed(GLFW_KEY_LEFT_BRACKET) && !rig.lights.empty())
          rig.selected = (rig.selected + (int)rig.lights.size() - 1) % (int)rig.lights.size();
        if (in.pressed(GLFW_KEY_RIGHT_BRACKET) && !rig.lights.empty())
          rig.selected = (rig.selected + 1) % (int)rig.lights.size();
      }
    }
    if (in.pressed(GLFW_KEY_C)) toggleSit();
  }

  // singer push-to-sing works while walking around the stage
  if (role == Role::Singer) {
    pushToSing = in.down(GLFW_KEY_G) || in.down(GLFW_KEY_LEFT_ALT) || in.down(GLFW_KEY_RIGHT_ALT);
    audio.setPushToSing(pushToSing && inHouse);
    if (in.pressed(GLFW_KEY_M)) { micMuted = !micMuted; audio.setMuted(micMuted); setStatus(micMuted ? "Mic muted" : "Mic live"); }
    if (in.pressed(GLFW_KEY_N)) { audio.setMonitor(!audio.monitor()); setStatus(audio.monitor() ? "Hearing yourself (headphones only!)" : "Self-monitor off"); }
  }
  if (inHouse && in.pressed(GLFW_KEY_P) && role != Role::CameraOperator) returnToPlaza();
}

// ====================================================================== update

void World::update(float dt) {
  time += dt;
  if (statusTimer > 0.f) statusTimer -= dt;

  switch (zone) {
    case Zone::FadeOut:
      fade = std::min(1.f, fade + dt * 1.6f);
      if (fade >= 1.f) {
        zone = Zone::Establishing;
        establishT = 0.f;
        localPlayer.position = {0.8f, floorHeight(0.8f, 19.5f), 19.5f};
        localPlayer.yaw = 180.f;
        orbit.yaw = 90.f;
        orbit.pitch = 14.f;
        orbit.setFollow(localPlayer.position);
        audio.allNotesOff(0);
      }
      break;
    case Zone::Establishing:
      establishT += dt;
      fade = std::max(0.f, 1.f - establishT * 1.5f);
      if (establishT >= 3.4f) {
        zone = Zone::Auditorium;
        fade = 0.f;
        establishT = 0.f;
        Role r = localPlayer.role;
        localPlayer.role = Role::Audience;
        if (r == Role::Audience) setStatus("Welcome to the Opéra. Pick a role in the panel (or F5-F9).", 6.f);
        else setRole(r);
      }
      break;
    case Zone::FadeToCity:
      fade = std::min(1.f, fade + dt * 1.6f);
      if (fade >= 1.f) {
        zone = Zone::City;
        localPlayer.position = {0.f, 0.f, 7.5f};
        localPlayer.yaw = 0.f;
        orbit.yaw = -90.f;
        fade = 0.f;
        setStatus("Back on the plaza");
      }
      break;
    case Zone::City:
      fade = std::max(0.f, fade - dt * 2.f);
      break;
    default: break;
  }
  orbit.setFollow(localPlayer.position);

  // singer mouth follows the voice level
  if (localPlayer.role == Role::Singer) {
    float lvl = pushToSing && !micMuted ? std::clamp(audio.micLevel() * 14.f, 0.f, 1.f) : 0.f;
    localPlayer.mouthOpen = smoothDamp(localPlayer.mouthOpen, lvl, 18.f, dt);
  } else {
    localPlayer.mouthOpen = smoothDamp(localPlayer.mouthOpen, 0.f, 10.f, dt);
  }

  // loaded MIDI song: schedule ahead on the audio clock
  song.pump(audio.clock(), 0.12, [&](const NoteEvent& e) { onSongNote(e); });
  // loaded audio file (local or streamed): orchestra moves to loudness and beat (S3)
  static int lastBeats = 0;
  int beats = audio.songBeats();
  float lvl = audio.songLevel();
  bool newBeat = beats != lastBeats;
  lastBeats = beats;
  if (lvl > 0.01f || newBeat) {
    auto pulse = [&](FrogActor& f, int k) {
      if (f.role != Role::Musician || f.seatIndex < 0) return;
      f.songLevel = lvl;
      if (newBeat && ((k + beats) % 3 != 0)) f.onNote(std::min(1.f, 0.5f + lvl * 4.f));
    };
    int k = 0;
    for (auto& n : npcs) pulse(n, k++);
    for (auto& r : remotePlayers) pulse(r, k++);
  }

  localPlayer.update(dt);
  for (auto& r : remotePlayers) r.update(dt);
  for (auto& n : npcs) {
    n.update(dt);
    n.mouthOpen = std::max(0.f, (n.instrument == InstrumentId::Flute || n.instrument == InstrumentId::Trumpet)
                                    ? n.noteImpulse * 0.4f : 0.f);
  }
  for (auto& r : remotePlayers)
    if (r.role == Role::Singer)
      r.mouthOpen = std::max(r.mouthOpen, std::clamp(audio.remoteLevel(r.id, StreamKind::Voice) * 10.f, 0.f, 1.f));

  rig.updateFollowspots(performerPositions());
  rig.update(dt);
  handleNet(dt);
}

// ====================================================================== network

void World::handleNet(float dt) {
  static bool wasConnected = false;
  if (wasConnected && !net.connected()) {
    net.disconnect();
    for (auto& r : remotePlayers) audio.dropRemote(r.id);
    remotePlayers.clear();
    for (auto& s : orchestra.seats())
      if (s.claimedBy != (int)localPlayer.id) s.claimedBy = -1;
    remoteRecording = false;
    lightingOwner = -1;
    refreshNpcs();
    setStatus("Disconnected from the room", 5.f);
  }
  wasConnected = net.connected();
  if (!net.connected()) return;

  // id assigned by the server (seat claims follow the new id)
  if (net.playerId() != 0 && localPlayer.id != net.playerId()) {
    uint32_t old = localPlayer.id;
    localPlayer.id = net.playerId();
    for (auto& s : orchestra.seats())
      if (s.claimedBy == (int)old) s.claimedBy = (int)localPlayer.id;
    if (lightingOwner == (int)old) requestLightingDesk();
  }

  // ---- outgoing
  netSendTimer += dt;
  if (netSendTimer >= 0.05f && localPlayer.id != kOfflineId) {  // 20 Hz network tick
    netSendTimer = 0.f;
    PlayerState ps;
    ps.x = localPlayer.position.x;
    ps.y = localPlayer.position.y;
    ps.z = localPlayer.position.z;
    ps.yaw = localPlayer.yaw;
    ps.role = (uint8_t)localPlayer.role;
    ps.seat = (int8_t)localPlayer.seatIndex;
    ps.sitting = localPlayer.sitting;
    ps.playing = localPlayer.playing;
    ps.mouth = localPlayer.mouthOpen;
    ps.instrument = (uint8_t)localPlayer.instrument;
    ps.suit = 0;
    ps.inHouse = outdoors() ? 0 : 1;
    std::strncpy(ps.name, localPlayer.name.c_str(), sizeof(ps.name) - 1);
    // only replicate while inside the house or on the plaza; zone is implicit in position
    net.send(makePacket(MsgType::Pose, localPlayer.id, ps));
    if (canEditLights() && lightingOwner == (int)localPlayer.id && rig.revision != sentRigRevision) {
      sentRigRevision = rig.revision;
      std::string blob = rig.serialize();
      net.send(makePacket(MsgType::Rig, localPlayer.id, blob.data(), blob.size()));
    }
  }
  static uint32_t seq = 0;
  for (StreamKind kind : {StreamKind::Voice, StreamKind::Song}) {
    std::vector<int16_t> pcm;
    audio.drainOutgoing(kind, pcm);
    for (size_t off = 0; off < pcm.size(); off += 960) {
      size_t n = std::min<size_t>(960, pcm.size() - off);
      std::string payload(sizeof(AudioHeader) + n * 2, '\0');
      AudioHeader ah{(uint8_t)kind, seq++};
      std::memcpy(payload.data(), &ah, sizeof(ah));
      std::memcpy(payload.data() + sizeof(ah), pcm.data() + off, n * 2);
      net.send(makePacket(MsgType::Audio, localPlayer.id, payload.data(), payload.size()));
    }
  }

  // ---- incoming
  bool seatsChanged = false;
  net.poll([&](const PacketHeader& h, const std::string& p) {
    uint32_t from = h.playerId;
    switch (h.type) {
      case MsgType::Pose: {
        if (p.size() < sizeof(PlayerState) || from == localPlayer.id) return;
        PlayerState ps;
        std::memcpy(&ps, p.data(), sizeof(ps));
        FrogActor* f = nullptr;
        for (auto& r : remotePlayers)
          if (r.id == from) f = &r;
        if (!f) {
          FrogActor nf;
          nf.id = from;
          nf.position = nf.netPosition = {ps.x, ps.y, ps.z};
          nf.yaw = nf.netYaw = ps.yaw;
          nf.suitColor = suitPalette[1 + from % 5];
          remotePlayers.push_back(nf);
          f = &remotePlayers.back();
          setStatus(std::string(ps.name) + " joined the room");
        }
        f->name = std::string(ps.name, strnlen(ps.name, sizeof(ps.name)));
        f->netPosition = {ps.x, ps.y, ps.z};
        f->netYaw = ps.yaw;
        f->lastHeard = time;
        f->inHouse = ps.inHouse != 0;
        f->role = (Role)std::min<int>(ps.role, (int)Role::Audience);
        f->sitting = ps.sitting != 0;
        f->playing = ps.playing != 0;
        if (f->role == Role::Singer) f->mouthOpen = ps.mouth;
        f->instrument = (InstrumentId)std::min<int>(ps.instrument, (int)InstrumentId::Timpani);
        int newSeat = f->role == Role::Musician ? ps.seat : -1;
        if (newSeat != f->seatIndex) {
          orchestra.release((int)from);
          if (newSeat >= 0 && newSeat < (int)orchestra.seats().size()) {
            auto& s = orchestra.seats()[newSeat];
            if (s.claimedBy == (int)localPlayer.id) {
              // both claimed the same chair: lower id keeps it
              if (from < localPlayer.id) { leaveSeat(); setStatus(f->name + " took that chair first"); }
            }
            orchestra.claim(newSeat, (int)from);
          }
          f->seatIndex = newSeat;
          seatsChanged = true;
        }
        break;
      }
      case MsgType::Note: {
        if (p.size() < sizeof(NotePayload)) return;
        NotePayload n;
        std::memcpy(&n, p.data(), sizeof(n));
        InstrumentId inst = (InstrumentId)std::min<int>(n.instrument, (int)InstrumentId::Timpani);
        uint32_t src = n.seat >= 0 ? kNpcBase + 100 + n.seat + from * 16 : from;
        if (n.on) audio.noteOn(inst, n.note, n.velocity / 127.f, src);
        else audio.noteOff(inst, n.note, src);
        if (n.on) {
          FrogActor* f = n.seat >= 0 ? frogAtSeat(n.seat) : frogById(from);
          if (f) f->onNote(n.velocity / 127.f);
        }
        break;
      }
      case MsgType::Rig:
        if (lightingOwner != (int)localPlayer.id || localPlayer.role != Role::LightingDesigner)
          rig.deserialize(p);
        break;
      case MsgType::Desk:
        if (p.size() >= 4) {
          int32_t owner;
          std::memcpy(&owner, p.data(), 4);
          lightingOwner = owner;
          if (owner >= 0 && owner != (int)localPlayer.id && localPlayer.role == Role::LightingDesigner)
            setStatus("Another lighting designer has the desk - use 'Take over desk' to hand it off", 5.f);
        }
        break;
      case MsgType::RecFlag:
        if (!p.empty() && from != localPlayer.id) {
          remoteRecording = p[0] != 0;
          FrogActor* f = frogById(from);
          remoteRecorderName = f ? f->name : "Camera operator";
        }
        break;
      case MsgType::Audio:
        if (p.size() > sizeof(AudioHeader)) {
          AudioHeader ah;
          std::memcpy(&ah, p.data(), sizeof(ah));
          size_t n = (p.size() - sizeof(ah)) / 2;
          std::vector<int16_t> pcm(n);
          std::memcpy(pcm.data(), p.data() + sizeof(ah), n * 2);
          audio.pushRemote(from, (StreamKind)(ah.kind & 1), pcm.data(), n);
        }
        break;
      case MsgType::Song:
        if (p.size() >= sizeof(SongPayload) && from != localPlayer.id) {
          SongPayload sp;
          std::memcpy(&sp, p.data(), sizeof(sp));
          FrogActor* f = frogById(from);
          std::string who = f ? f->name : "Someone";
          setStatus(sp.playing ? who + " started the music" : who + " stopped the music");
        }
        break;
      case MsgType::Leave: {
        auto it = std::find_if(remotePlayers.begin(), remotePlayers.end(), [&](auto& r) { return r.id == from; });
        if (it != remotePlayers.end()) {
          setStatus(it->name + " left the room");
          remotePlayers.erase(it);
        }
        orchestra.release((int)from);
        audio.dropRemote(from);
        audio.allNotesOff(from);
        seatsChanged = true;
        break;
      }
      default: break;
    }
  });
  if (seatsChanged) refreshNpcs();
}

// ====================================================================== view + draw

CameraView World::cameraView(float aspect) const {
  CameraView cv;
  // ---- gameplay camera
  auto gameplay = [&]() {
    CameraView g;
    bool inHouse = zone == Zone::Auditorium || zone == Zone::Establishing || zone == Zone::FadeToCity;
    if (inHouse && localPlayer.role == Role::CameraOperator) {
      if (camMode == CamMode::Rails) {
        g.view = rails.view();
        g.position = rails.position();
      } else {
        g.view = fly.view();
        g.position = fly.eye();
      }
      g.proj = rails.proj(aspect);
      g.film = true;
      g.focalMm = rails.focalLengthMm;
      g.fStop = rails.fStop;
      g.focusDist = rails.focusDistance;
      g.evOffset = rails.evOffset;
      return g;
    }
    if (inHouse && localPlayer.role == Role::LightingDesigner && lightView == LightView::TopDown) {
      glm::vec3 c{topDownCursor.x, 0.f, topDownCursor.y};
      g.position = c + glm::vec3(0, 40, 0);
      g.view = glm::lookAt(g.position, c, glm::vec3(0, 0, -1));
      float h = 11.f;
      // near plane slices the view at y = 9.5 so the dome and fly borders don't hide the stage
      g.proj = glm::ortho(-h * aspect, h * aspect, -h, h, 30.5f, 44.f);
      g.nearP = 30.5f;
      g.farP = 44.f;
      return g;
    }
    if (inHouse && localPlayer.role == Role::LightingDesigner && lightView == LightView::FromLight &&
        !rig.lights.empty()) {
      const auto& L = rig.lights[std::clamp(rig.selected, 0, (int)rig.lights.size() - 1)];
      g.position = L.position + L.direction * 0.4f;
      glm::vec3 up = std::abs(L.direction.y) > 0.95f ? glm::vec3(0, 0, -1) : glm::vec3(0, 1, 0);
      g.view = glm::lookAt(g.position, g.position + L.direction, up);
      g.proj = glm::perspective(glm::radians(std::clamp(L.beamAngle * 1.6f, 25.f, 100.f)), aspect, 0.1f, 600.f);
      return g;
    }
    // third-person orbit, kept inside the venue
    OrbitCamera o = orbit;
    for (int i = 0; i < 12; ++i) {
      glm::vec3 p = o.position();
      bool ok;
      if (outdoors()) ok = p.z > 0.6f && p.y > 0.3f;
      else ok = glm::length(glm::vec2(p.x, p.z - 5.f)) < 14.6f && p.y < 17.f && p.y > floorHeight(p.x, p.z) + 0.2f;
      if (ok) break;
      o.distance *= 0.8f;
    }
    g.view = o.view();
    g.proj = o.proj(aspect);
    g.position = o.position();
    return g;
  };

  cv = gameplay();
  if (zone == Zone::Establishing) {
    // RPG-style establishing sweep: Catmull-Rom path, slerped orientation, eased (W3)
    static CameraRail path = [] {
      CameraRail r;
      r.points = {{0.f, 14.5f, 19.5f}, {-7.5f, 11.f, 13.f}, {-6.f, 7.f, 5.f}, {-1.5f, 4.5f, 2.f}};
      r.rebuildArcTable();
      return r;
    }();
    float t = std::clamp(establishT / 2.8f, 0.f, 1.f);
    float e = t * t * (3.f - 2.f * t);
    glm::vec3 pos = path.sampleByArc(e * path.totalLength);
    glm::quat q0 = lookRotation(glm::vec3(0, 12.5f, 5.5f) - glm::vec3(0.f, 14.5f, 19.5f));
    glm::quat q1 = lookRotation(glm::vec3(0, 2.2f, -6.f) - path.points.back());
    glm::quat q = glm::slerp(q0, q1, e);
    // last stretch: settle into the gameplay camera
    float settle = glm::smoothstep(2.6f, 3.4f, establishT);
    glm::mat4 gv = glm::inverse(cv.view);
    glm::quat gq = glm::quat_cast(glm::mat3(gv));
    glm::vec3 gp = glm::vec3(gv[3]);
    pos = glm::mix(pos, gp, settle);
    q = glm::slerp(q, gq, settle);
    cv.position = pos;
    cv.view = glm::mat4_cast(glm::conjugate(q)) * glm::translate(glm::mat4(1), -pos);
    if (!cv.film) cv.proj = glm::perspective(glm::radians(55.f), aspect, 0.1f, 600.f);
  }
  return cv;
}

void World::collectDraw(std::vector<SceneObject>& out) const {
  bool inHouse = !outdoors();
  bool hideSelf = inHouse && localPlayer.role == Role::CameraOperator;
  bool lightView3 = inHouse && localPlayer.role == Role::LightingDesigner && lightView == LightView::FromLight;
  if (!hideSelf && !lightView3) localPlayer.appendDraw(out);
  if (inHouse)
    for (auto& n : npcs) n.appendDraw(out);
  for (auto& r : remotePlayers) {
    if (r.inHouse != inHouse) continue;                       // other zone
    if (inHouse && r.role == Role::CameraOperator) continue;  // operators ride the rails unseen
    r.appendDraw(out);
  }
}

}  // namespace og
