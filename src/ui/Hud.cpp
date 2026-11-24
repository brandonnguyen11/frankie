#include "ui/Hud.h"
#include "core/AssetPath.h"
#include "core/Platform.h"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace og {

namespace {
const ImVec4 kRed{1.f, 0.25f, 0.25f, 1.f};
const ImVec4 kGold{1.f, 0.8f, 0.35f, 1.f};
const ImVec4 kDim{0.65f, 0.65f, 0.7f, 1.f};

void copyStr(char* dst, size_t n, const std::string& s) {
  std::strncpy(dst, s.c_str(), n - 1);
  dst[n - 1] = 0;
}
}  // namespace

bool Hud::init(GLFWwindow* window) {
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGuiStyle& st = ImGui::GetStyle();
  st.WindowRounding = 6.f;
  st.FrameRounding = 4.f;
  st.Colors[ImGuiCol_WindowBg] = ImVec4(0.06f, 0.03f, 0.03f, 0.88f);
  st.Colors[ImGuiCol_Header] = ImVec4(0.45f, 0.08f, 0.08f, 0.8f);
  st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.6f, 0.12f, 0.1f, 0.9f);
  st.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.07f, 0.07f, 0.9f);
  st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.55f, 0.12f, 0.1f, 1.f);
  st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.75f, 0.5f, 0.15f, 1.f);
  st.Colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.08f, 0.07f, 0.9f);
  st.Colors[ImGuiCol_SliderGrab] = ImVec4(0.9f, 0.7f, 0.3f, 1.f);
  st.Colors[ImGuiCol_CheckMark] = ImVec4(1.f, 0.8f, 0.35f, 1.f);
  st.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.4f, 0.06f, 0.06f, 1.f);
  ImGui_ImplGlfw_InitForOpenGL(window, false);  // Input forwards events
  ImGui_ImplOpenGL3_Init("#version 410");
  copyStr(midiPath_, sizeof(midiPath_), assetPath("samples/demo.mid"));
  copyStr(audioPath_, sizeof(audioPath_), assetPath("samples/demo.wav"));
  std::ifstream f(assetPath("credits/CREDITS.txt"));
  if (f) credits_.assign((std::istreambuf_iterator<char>(f)), {});
  if (const char* u = std::getenv("USER")) copyStr(playerName_, sizeof(playerName_), u);
  return true;
}

void Hud::begin() {
  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
}

void Hud::end() {
  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Hud::shutdown() {
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
}

// ------------------------------------------------------------------ banner (C6)
void Hud::drawRecBanner(World& w) {
  bool local = w.recording;
  bool remote = w.remoteRecording;
  if (!local && !remote) return;
  ImGuiIO& io = ImGui::GetIO();
  ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, 14.f}, ImGuiCond_Always, {0.5f, 0.f});
  ImGui::SetNextWindowBgAlpha(0.75f);
  ImGui::Begin("##rec", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_NoFocusOnAppearing);
  bool blink = std::fmod(w.time, 1.f) < 0.6f;
  ImVec2 c = ImGui::GetCursorScreenPos();
  float r = ImGui::GetTextLineHeight() * 0.38f;
  ImGui::GetWindowDrawList()->AddCircleFilled({c.x + r, c.y + ImGui::GetTextLineHeight() * 0.5f}, r,
                                              ImGui::GetColorU32(blink ? kRed : ImVec4(0.5f, 0.1f, 0.1f, 1.f)));
  ImGui::Dummy({r * 2.f + 2.f, ImGui::GetTextLineHeight()});
  ImGui::SameLine();
  ImGui::TextColored(kRed, "REC");
  ImGui::SameLine();
  if (local) ImGui::Text("You are recording  (V to stop)");
  else ImGui::Text("%s is recording the show", w.remoteRecorderName.c_str());
  ImGui::End();
}

// ------------------------------------------------------------------ roles (R1)
void Hud::drawRoles(World& w) {
  ImGui::SeparatorText("Role");
  const char* labels[] = {"Musician", "Singer", "Lighting", "Camera", "Audience"};
  for (int i = 0; i < 5; ++i) {
    if (i) ImGui::SameLine();
    bool active = (int)w.localPlayer.role == i;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.5f, 0.15f, 1.f));
    if (ImGui::Button(labels[i])) w.setRole((Role)i);
    if (active) ImGui::PopStyleColor();
  }
  if (w.zone == Zone::City) ImGui::TextColored(kDim, "Roles start once you're inside. Walk into the centre doors.");
  else if (w.zone == Zone::Auditorium && ImGui::SmallButton("Return to the plaza (P)")) w.returnToPlaza();
  if (w.zone == Zone::Auditorium && w.localPlayer.role != Role::CameraOperator) {
    ImGui::SameLine();
    if (ImGui::SmallButton(w.localPlayer.sitting ? "Stand up (C)" : "Sit down (C)")) w.toggleSit();
  }
}

// ------------------------------------------------------------------ musician (R2, I*, S*)
void Hud::drawMusician(World& w) {
  ImGui::SeparatorText("Orchestra");
  auto& seats = w.orchestra.seats();
  if (w.localPlayer.seatIndex >= 0) {
    auto& s = seats[w.localPlayer.seatIndex];
    ImGui::TextColored(kGold, "Seated at %s", s.label.c_str());
    ImGui::Text("Octave %d   %s", w.octaveBase / 12 - 1,
                w.audio.usingSamples(s.instrument) ? "sampled" : "synth (add samples to assets/samples)");
    ImGui::TextColored(kDim, "A S D F G H J K white, W E T Y U black, Z/X octave, hold Space = loud");
    if (ImGui::Button("Stand up (Backspace)")) w.leaveSeat();
  } else {
    ImGui::TextColored(kDim, "Pick a free chair, or walk to the pit and press E:");
  }
  if (ImGui::BeginTable("seats", 3, ImGuiTableFlags_SizingFixedFit)) {
    for (auto& s : seats) {
      ImGui::TableNextColumn();
      ImGui::PushID(s.index);
      bool mine = s.claimedBy == (int)w.localPlayer.id;
      bool free = s.claimedBy < 0;
      std::string label = s.label + (mine ? " *" : (free ? "" : " (taken)"));
      ImGui::BeginDisabled(!free && !mine);
      if (ImGui::Button(label.c_str(), {120, 0})) w.claimSeat(s.index);
      ImGui::EndDisabled();
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Load a song");
  ImGui::TextColored(kDim, "Songs stay on your computer. Only load music you have the rights to use.");
  ImGui::InputText("##midi", midiPath_, sizeof(midiPath_));
  ImGui::SameLine();
  if (ImGui::Button("...##m")) {
    std::string p;
    if (openFileDialog("Choose a MIDI file", {"mid", "midi"}, p)) copyStr(midiPath_, sizeof(midiPath_), p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load MIDI")) {
    std::string err;
    MidiSong song;
    if (song.load(midiPath_, err)) {
      // default part -> seat mapping by instrument (S1)
      std::vector<int> used(w.orchestra.seats().size(), 0);
      for (auto& p : song.parts) {
        int best = -1;
        for (auto& s : w.orchestra.seats())
          if (s.instrument == p.instrument && (best < 0 || used[s.index] < used[best])) best = s.index;
        if (best < 0)
          for (auto& s : w.orchestra.seats())
            if (best < 0 || used[s.index] < used[best]) best = s.index;
        p.seat = best;
        if (best >= 0) used[best]++;
      }
      w.stopSong();
      w.song.song = std::move(song);
      w.song.loaded = true;
      w.setStatus("Loaded " + w.song.song.title + " - assign parts below, then Play");
    } else {
      w.setStatus("MIDI: " + err, 5.f);
    }
  }
  if (w.song.loaded) {
    auto& song = w.song.song;
    ImGui::Text("%s  (%.0fs, %zu parts)", song.title.c_str(), song.duration, song.parts.size());
    if (ImGui::BeginTable("parts", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerH)) {
      for (size_t i = 0; i < song.parts.size(); ++i) {
        auto& p = song.parts[i];
        ImGui::PushID((int)i);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(p.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("GM %d", p.program);
        ImGui::TableNextColumn();
        std::string cur = p.seat >= 0 ? w.orchestra.seats()[p.seat].label : "(muted)";
        ImGui::SetNextItemWidth(130);
        if (ImGui::BeginCombo("##seat", cur.c_str())) {
          if (ImGui::Selectable("(muted)", p.seat < 0)) p.seat = -1;
          for (auto& s : w.orchestra.seats())
            if (ImGui::Selectable(s.label.c_str(), p.seat == s.index)) {
              p.seat = s.index;
              p.instrument = s.instrument;  // the chair's instrument plays the part
              for (auto& n : song.notes)
                if (n.part == (int)i) n.instrument = p.instrument;
            }
          ImGui::EndCombo();
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    if (!w.song.playing) {
      if (ImGui::Button("Play song")) w.startSong();
    } else {
      ImGui::Text("Playing %.1f / %.0f s", w.song.position(w.audio.clock()), song.duration);
      if (ImGui::Button("Stop")) w.stopSong();
    }
  }

  ImGui::InputText("##audio", audioPath_, sizeof(audioPath_));
  ImGui::SameLine();
  if (ImGui::Button("...##a")) {
    std::string p;
    if (openFileDialog("Choose an audio file", {"mp3", "wav", "flac"}, p)) copyStr(audioPath_, sizeof(audioPath_), p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load MP3/WAV")) {
    std::string err;
    if (w.audio.loadAudioFile(audioPath_, err)) {
      w.song.loaded = false;
      w.setStatus("Loaded audio (" + std::to_string((int)w.audio.audioFileDuration()) + " s) - Play to start");
    } else {
      w.setStatus("Audio: " + err, 5.f);
    }
  }
  if (w.audio.audioFileLoaded() && !w.song.loaded) {
    if (!w.audio.audioFilePlaying()) {
      if (ImGui::Button("Play audio")) w.startSong();
    } else {
      ImGui::Text("Playing %.1f / %.0f s  level %.2f", w.audio.audioFilePosition(), w.audio.audioFileDuration(),
                  w.audio.songLevel());
      if (ImGui::Button("Stop audio")) w.stopSong();
    }
  }
}

// ------------------------------------------------------------------ singer (V*)
void Hud::drawSinger(World& w) {
  ImGui::SeparatorText("Microphone");
  if (!w.audio.micAvailable()) {
    ImGui::TextColored(kRed, "No microphone. Allow mic access in System Settings > Privacy > Microphone.");
    return;
  }
  ImGui::TextColored(kGold, "Use headphones to prevent echo.");
  ImGui::Text("Hold G (or Option) to sing.  %s", w.pushToSing && !w.micMuted ? "LIVE" : "");
  ImGui::ProgressBar(std::clamp(w.audio.micLevel() * 8.f, 0.f, 1.f), {-1, 0}, "");
  if (ImGui::Checkbox("Mute (M)", &w.micMuted)) w.audio.setMuted(w.micMuted);
  bool mon = w.audio.monitor();
  if (ImGui::Checkbox("Hear myself with hall reverb (N, headphones only)", &mon)) w.audio.setMonitor(mon);
  float send = w.audio.reverbSend.load();
  if (ImGui::SliderFloat("Hall reverb", &send, 0.f, 0.6f)) w.audio.reverbSend = send;
}

// ------------------------------------------------------------------ lighting (L*)
void Hud::drawLighting(World& w) {
  ImGui::SeparatorText("Lighting desk");
  bool edit = w.canEditLights();
  if (!edit) {
    ImGui::TextColored(kGold, "Another designer has the desk.");
    if (ImGui::Button("Take over desk")) w.requestLightingDesk();
    ImGui::BeginDisabled();
  }
  int view = (int)w.lightView;
  ImGui::RadioButton("Walk (1)", &view, 0);
  ImGui::SameLine();
  ImGui::RadioButton("Top-down (2)", &view, 1);
  ImGui::SameLine();
  ImGui::RadioButton("Through lens (3)", &view, 2);
  w.lightView = (LightView)view;
  if (w.lightView == LightView::TopDown) ImGui::TextColored(kDim, "Click the stage to aim the selected light. WASD pans.");
  if (w.lightView == LightView::FromLight) ImGui::TextColored(kDim, "Right-drag / Tab+mouse or arrow keys to pan & tilt.");

  auto& rig = w.rig;
  bool changed = false;
  changed |= ImGui::SliderFloat("Haze", &rig.haze, 0.f, 1.f, "%.2f");
  changed |= ImGui::SliderFloat("House lights", &rig.houseLights, 0.f, 1.f, "%.2f");
  changed |= ImGui::SliderFloat("Chandelier", &rig.chandelier, 0.f, 1.f, "%.2f");
  ImGui::SliderFloat("House exposure (EV)", &w.houseEV, 5.f, 11.f, "%.1f");
  if (ImGui::Button("Agent 327 preset")) rig.applyDefaultAgent327();
  ImGui::SameLine();
  if (ImGui::Button("Save preset...")) {
    std::string p;
    if (!saveFileDialog("Save lighting preset", "lighting_preset.ogrig", p)) p = "lighting_preset.ogrig";
    w.setStatus(rig.saveFile(p) ? "Saved " + p : "Could not save " + p);
  }
  ImGui::SameLine();
  if (ImGui::Button("Load preset...")) {
    std::string p;
    if (!openFileDialog("Load lighting preset", {"ogrig", "txt"}, p)) p = "lighting_preset.ogrig";
    w.setStatus(rig.loadFile(p) ? "Loaded " + p : "Could not load " + p);
  }

  ImGui::SeparatorText("Lights");
  for (size_t i = 0; i < rig.lights.size(); ++i) {
    auto& L = rig.lights[i];
    ImGui::PushID((int)i);
    if (ImGui::Checkbox("##on", &L.enabled)) changed = true;
    ImGui::SameLine();
    glm::vec3 c = L.color() / std::max({L.color().r, L.color().g, L.color().b, 1e-3f});
    ImGui::ColorButton("##c", ImVec4(c.r, c.g, c.b, 1.f), ImGuiColorEditFlags_NoTooltip, {14, 14});
    ImGui::SameLine();
    if (ImGui::Selectable(L.name.c_str(), rig.selected == (int)i)) rig.selected = (int)i;
    ImGui::PopID();
  }
  if (ImGui::Button("Add light") && (int)rig.lights.size() < LightingRig::kMaxLights) rig.addLight();
  ImGui::SameLine();
  if (ImGui::Button("Remove selected")) rig.removeSelected();

  if (!rig.lights.empty()) {
    rig.selected = std::clamp(rig.selected, 0, (int)rig.lights.size() - 1);
    auto& L = rig.lights[rig.selected];
    ImGui::SeparatorText(L.name.c_str());
    // mounting point
    if (ImGui::BeginCombo("Mount", rig.mounts[std::clamp(L.mount, 0, (int)rig.mounts.size() - 1)].name.c_str())) {
      for (size_t m = 0; m < rig.mounts.size(); ++m)
        if (ImGui::Selectable(rig.mounts[m].name.c_str(), L.mount == (int)m)) { rig.setMount(L, (int)m); changed = true; }
      ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Intensity (kcd)", &L.intensityKcd, 0.f, 600.f, "%.0f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::Checkbox("Colour temperature", &L.useKelvin);
    if (L.useKelvin) changed |= ImGui::SliderFloat("Kelvin", &L.kelvin, 1800.f, 12000.f, "%.0f K");
    else changed |= ImGui::ColorEdit3("Colour", &L.rgb.x);
    changed |= ImGui::SliderFloat("Beam angle", &L.beamAngle, 4.f, 70.f, "%.0f deg");
    changed |= ImGui::SliderFloat("Edge softness", &L.softness, 0.f, 0.95f);
    changed |= ImGui::Checkbox("Casts shadows", &L.castsShadow);
    int g = (int)L.gobo;
    const char* gobos[] = {"Open", "Leaf breakup", "Window", "Stars"};
    if (ImGui::Combo("Gobo", &g, gobos, 4)) { L.gobo = (Gobo)g; changed = true; }
    // followspot (L3)
    auto names = w.performerNames();
    std::string cur = L.followTarget >= 0 && L.followTarget < (int)names.size() ? names[L.followTarget] : "Manual aim";
    if (ImGui::BeginCombo("Follow", cur.c_str())) {
      if (ImGui::Selectable("Manual aim", L.followTarget < 0)) { L.followTarget = -1; changed = true; }
      for (size_t k = 0; k < names.size(); ++k)
        if (ImGui::Selectable((names[k] + "##" + std::to_string(k)).c_str(), L.followTarget == (int)k)) {
          L.followTarget = (int)k;
          changed = true;
        }
      ImGui::EndCombo();
    }
    if (L.followTarget < 0) {
      float pan = glm::degrees(std::atan2(L.direction.x, -L.direction.z));
      float tilt = glm::degrees(std::asin(std::clamp(-L.direction.y, -1.f, 1.f)));
      bool a = ImGui::SliderFloat("Pan", &pan, -180.f, 180.f, "%.0f deg");
      a |= ImGui::SliderFloat("Tilt (down)", &tilt, -30.f, 89.f, "%.0f deg");
      if (a) {
        float pr = glm::radians(pan), tr = glm::radians(tilt);
        L.direction = {std::sin(pr) * std::cos(tr), -std::sin(tr), -std::cos(pr) * std::cos(tr)};
        changed = true;
      }
    }
  }

  ImGui::SeparatorText("Cues");
  ImGui::SetNextItemWidth(110);
  ImGui::InputText("##cue", cueName_, sizeof(cueName_));
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80);
  ImGui::DragFloat("fade s", &cueFade_, 0.1f, 0.f, 30.f, "%.1f");
  ImGui::SameLine();
  if (ImGui::Button("Record cue")) {
    rig.recordCue(cueName_, cueFade_);
    copyStr(cueName_, sizeof(cueName_), "Cue " + std::to_string(rig.cues.size() + 1));
  }
  for (size_t i = 0; i < rig.cues.size(); ++i) {
    ImGui::PushID(1000 + (int)i);
    if (ImGui::Button("GO")) rig.goCue((int)i);
    ImGui::SameLine();
    ImGui::Text("%s%s  (%.1fs)", rig.activeCue == (int)i ? "> " : "", rig.cues[i].name.c_str(), rig.cues[i].fadeSeconds);
    ImGui::PopID();
  }
  if (!edit) ImGui::EndDisabled();
  if (changed) rig.markDirty();
}

// ------------------------------------------------------------------ camera (C*)
void Hud::drawCamera(World& w) {
  ImGui::SeparatorText("Film camera");
  auto& r = w.rails;
  for (size_t i = 0; i < r.rails.size(); ++i) {
    if (i) ImGui::SameLine();
    bool active = r.activeRail == (int)i;
    if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.5f, 0.15f, 1.f));
    if (ImGui::Button((std::to_string(i + 1) + " " + r.rails[i].name).c_str())) r.switchRail((int)i);
    if (active) ImGui::PopStyleColor();
    if (i == 2) ImGui::NewLine();
  }
  ImGui::SliderFloat("Focal length", &r.focalLengthMm, 18.f, 135.f, "%.0f mm");
  ImGui::SliderFloat("Focus", &r.focusDistance, 0.5f, 40.f, "%.2f m", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderFloat("Aperture", &r.fStop, 1.4f, 16.f, "f/%.1f", ImGuiSliderFlags_Logarithmic);
  ImGui::SliderFloat("Exposure", &r.evOffset, -3.f, 3.f, "%+.1f EV (lower = brighter)");
  ImGui::TextColored(kDim, "A/D dolly, mouse pan (Tab or right-drag), Shift slow, Q/E zoom, R/F focus, T autofocus, Z/X iris");
  if (!w.recording) {
    if (ImGui::Button("Record (V)", {160, 28})) w.startRecording();
    ImGui::SameLine();
    ImGui::TextColored(kDim, "1920x1080 @ 30 fps .mp4");
  } else {
    if (ImGui::Button("Stop (V)", {160, 28})) w.stopRecording();
    ImGui::SameLine();
    ImGui::TextColored(kRed, "recording");
  }
  if (!w.lastSavedPath.empty()) ImGui::TextWrapped("Last take: %s", w.lastSavedPath.c_str());
}

// ------------------------------------------------------------------ multiplayer (M*)
void Hud::drawRoom(World& w) {
  ImGui::SeparatorText("Room");
  if (!w.net.connected()) {
    ImGui::SetNextItemWidth(120);
    ImGui::InputText("Name", playerName_, sizeof(playerName_));
    ImGui::SetNextItemWidth(200);
    ImGui::InputText("Room code / link", roomAddress_, sizeof(roomAddress_));
    if (ImGui::Button("Join room")) {
      std::string err;
      w.localPlayer.name = playerName_;
      if (w.net.connect(roomAddress_, playerName_, err)) w.setStatus("Joined " + w.net.address());
      else w.setStatus("Join failed: " + err + " (start ./opera_server)", 6.f);
    }
    ImGui::TextColored(kDim, "Host: run ./opera_server, share the room code it prints.");
  } else {
    ImGui::Text("In room %s as #%u", w.net.address().c_str(), w.net.playerId());
    ImGui::Text("Players: you");
    for (auto& r : w.remotePlayers) {
      ImGui::SameLine();
      ImGui::Text(", %s (%s)", r.name.c_str(), roleName(r.role));
    }
    if (ImGui::Button("Leave room")) w.net.disconnect();
  }
}

// ------------------------------------------------------------------ graphics
void Hud::drawGraphics(World& w, const FrameInfo& info) {
  if (!ImGui::CollapsingHeader("Graphics")) return;
  int q = (int)w.quality;
  const char* qs[] = {"Low", "Medium", "High"};
  if (ImGui::Combo("Quality (F1-F3)", &q, qs, 3)) w.quality = (Quality)q;
  const char* tm[] = {"AgX", "ACES"};
  ImGui::Combo("Tone mapping", &w.toneMap, tm, 2);
  ImGui::Checkbox("FXAA", &w.fxaa);
  if (w.zone == Zone::City) ImGui::SliderFloat("Time of day", &w.timeOfDay, 0.f, 1.f, "%.2f (afternoon -> dusk)");
  ImGui::Text("%.0f fps (%.1f ms)  %dx%d", info.fps, info.frameMs, info.renderW, info.renderH);
  ImGui::Text("%d draws, %d culled, %d lights, %d shadow maps", info.stats.drawCalls, info.stats.culled,
              info.stats.lights, info.stats.shadowMaps);
}

void Hud::drawHelp(World& w) {
  if (!ImGui::CollapsingHeader("Controls", showHelp_ ? ImGuiTreeNodeFlags_DefaultOpen : 0)) return;
  ImGui::TextColored(kDim,
                     "WASD move, Shift run, Tab / right-drag look, wheel zoom\n"
                     "E interact (doors, chairs, seats)   C sit/stand   P back to plaza\n"
                     "F1-F3 quality   F10 hide this panel");
  (void)w;
}

// ------------------------------------------------------------------ main panel
void Hud::draw(World& w, const FrameInfo& info) {
  drawRecBanner(w);
  ImGuiIO& io = ImGui::GetIO();
  if (w.statusTimer > 0.f && !w.status.empty()) {
    ImGui::SetNextWindowPos({io.DisplaySize.x * 0.5f, io.DisplaySize.y - 24.f}, ImGuiCond_Always, {0.5f, 1.f});
    ImGui::SetNextWindowBgAlpha(std::min(0.7f, w.statusTimer));
    ImGui::Begin("##status", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::TextUnformatted(w.status.c_str());
    ImGui::End();
  }
  if (w.hideHud) return;
  if (w.zone == Zone::Establishing || w.zone == Zone::FadeOut || w.zone == Zone::FadeToCity) return;

  ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({400, io.DisplaySize.y - 24}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Opéra Grenouille", nullptr, ImGuiWindowFlags_NoSavedSettings);
  ImGui::TextColored(kGold, "%s", w.zone == Zone::City ? "The plaza, 1958" : "Palais Garnier - auditorium");
  ImGui::SameLine();
  ImGui::TextColored(kDim, " %.0f fps", info.fps);

  drawRoles(w);
  if (w.zone == Zone::Auditorium) {
    switch (w.localPlayer.role) {
      case Role::Musician: drawMusician(w); break;
      case Role::Singer: drawSinger(w); break;
      case Role::LightingDesigner: drawLighting(w); break;
      case Role::CameraOperator: drawCamera(w); break;
      case Role::Audience:
        ImGui::SeparatorText("Audience");
        if (ImGui::Button("Take the nearest seat (E)")) w.takeAudienceSeat();
        break;
      default: break;
    }
  }
  drawRoom(w);
  drawGraphics(w, info);
  drawHelp(w);
  if (ImGui::CollapsingHeader("Credits")) ImGui::TextWrapped("%s", credits_.c_str());
  ImGui::End();
}

}  // namespace og
