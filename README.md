# Opéra Grenouille

CS4361 interactive 3D graphics project: a multiplayer 1950s–70s city plaza and a Palais Garnier–style
opera house where suited frogs perform under player-built stage lighting, filmed on camera rails and saved
as a video file.

**Stack:** C++17 · OpenGL 4.1 core · GLSL (all shading is our own) · GLFW · glad · glm · Assimp (optional
model import) · Dear ImGui · miniaudio · AVFoundation (macOS video encoding)

## Build and run (macOS)

```bash
brew install cmake glfw glm assimp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/opera_grenouille
```

Shaders and assets are copied next to the binary, so it runs from any directory.
Instrument samples ship in `assets/samples/`; to re-fetch them run `python3 tools/fetch_samples.py`.

### Multiplayer

```bash
./build/opera_server 7777        # prints the room code(s), e.g. 192.168.1.31:7777
./build/opera_grenouille --join 192.168.1.31:7777 --name Ribbit
```

Or type the room code into **Room → Join room** in the panel. Up to 8 players per room.
Late joiners receive the current rig, poses, desk owner, recording flag and song state.

## Controls

The panel on the left has a button for every action (F-keys are media keys on most Mac laptops).

| Everyone | |
|---|---|
| Move / run | WASD · Shift |
| Look | right-drag, or Tab to capture the mouse (Esc releases) · wheel zooms |
| Enter the opera | walk into the centre doors (or E near them) |
| Skip the establishing shot | any key |
| Sit / stand · back to plaza | C · P |
| Quality · hide panel | F1 / F2 / F3 · F10 |

| Role | Keys |
|---|---|
| **Musician** | walk to the pit, E at a chair (or click a chair in the panel). Seated: A S D F G H J K white keys, W E T Y U black keys, Z/X octave, hold Space for loud, Backspace to stand. Load a MIDI or MP3/WAV from the panel |
| **Singer** | taken to a stage mark. Hold G (or Option) to sing, M mute, N hear yourself (headphones only) |
| **Lighting designer** | 1 walk · 2 top-down (click the stage to aim) · 3 through the lens (drag/arrow keys pan & tilt) · [ ] select light. Everything else is in the panel: mount, intensity, Kelvin/RGB, beam, softness, shadows, gobo, followspot target, haze, house, chandelier, cues, presets |
| **Camera operator** | 1–5 rails · A/D dolly (eased) · mouse pan/tilt · Shift slow · Q/E zoom 18–135 mm · R/F focus pull · T autofocus · Z/X aperture · , / . exposure · **V record / stop** |
| **Audience** | E (or panel button) to take the nearest free seat |

Recordings are 1920×1080 at a constant 30 fps, H.264 + AAC `.mp4`, containing the camera view (no UI) and
the whole room's mix. On stop a save dialog opens; the default location is `~/Movies/Opera Grenouille/`.

## What is implemented where (graphics concepts)

| Concept | Where |
|---|---|
| Scene graph / transforms | `actors/Frog.cpp`: hierarchical frog rig (hips → torso → head/jaw, shoulder → elbow → hand, hip → knee → foot) posed procedurally for walk, sit, singing and each instrument |
| Cameras & projection | `camera/OrbitCamera`, `camera/RailCamera`: focal length → vertical FOV on a 24 mm sensor; ortho top-down desk view |
| Curves & interpolation | Catmull-Rom rails with an arc-length table for constant dolly speed; quaternion slerp pans; spline + slerp establishing sweep (`World::cameraView`) |
| Shading models | `shaders/common.glsl`: Cook-Torrance GGX (height-correlated Smith), anisotropic GGX gold leaf, Charlie sheen velvet and wool, wrap + transmission frog skin, clear coat (lacquer, car paint, wet skin, puddles) |
| Lights & attenuation | spot/point/sun in physical units (candela, lux, nits), windowed inverse-square falloff, Kelvin → linear RGB, procedural gobos |
| Shadows | 8-layer spot shadow-map array + 4096² sun map, slope-scaled bias, normal offset, rotated-Poisson PCF with hardware compare |
| GI approximations | hemisphere "light probe" ambient + reflection lobes toward the chandelier and stage, SSAO (half-res, depth-aware blur) |
| Volumetrics | `shaders/volume.frag`: per-pixel ray/cone intersection, raymarched 3D-noise haze, Henyey-Greenstein phase, shadow-mapped so frogs cut the beams |
| Framebuffers & post | HDR RGBA16F, depth/normal prepass, CoD-style bloom mip chain, thin-lens DOF from circle of confusion, AgX/ACES tone mapping, EV100 exposure, FXAA, film grain/vignette on the film camera |
| Visibility & performance | frustum culling, instancing (≈600 seats), quality tiers (Low: 4 shadows, half-res volumetrics, no SSAO) |
| Scene transitions | full-screen fade + scripted establishing sweep |

## Assets and licensing

* Architecture, frogs, props and city are procedural (`scene/AuditoriumScene.cpp`, `scene/CityScene.cpp`) — our own work.
* Instrument samples: VSCO 2 Community Edition, **CC0** (`assets/samples/SOURCE.txt`).
* Agent 327: Operation Barbershop (Blender Studio, CC BY-ND) is a **lighting reference only**; no geometry is used.
* **Using Sketchfab models:** licensed glTF/GLB/OBJ/FBX files can be dropped into `assets/models/` and listed in
  `assets/models/models.txt` (`<city|house> <file> <scale> <x> <y> <z> <yawDeg> [replace]`). Material names
  containing *velvet/seat/curtain*, *gold/gilt*, *marble*, *wood*, *glass* get the matching custom shader.
  The *Opera House* model by SammyCAnimations is CC BY 4.0 (credit required). The *Opera Palais Garnier* model
  has no stated license, so it must not be shipped without the author's permission. Add every CC BY asset to
  `assets/credits/CREDITS.txt`.

## Developer tools

```bash
# headless screenshots (city | plaza | house | film | pit | balcony | lighting | musician | orbit)
./build/opera_grenouille --shot out.png --scene balcony --size 1920x1080 [--hud]
# film a take with the demo song and exit (checks the recording pipeline)
./build/opera_grenouille --record-test 10 --scene film
```

## Layout

```
src/core        app loop, input, asset paths      src/render   meshes, shaders, render targets, frame graph
src/scene       world/gameplay, venue + city      src/actors   frog rig and animation
src/lighting    rig, mounts, cues, presets        src/camera   orbit/rail cameras, recorder (AVFoundation / ffmpeg)
src/audio       sampler, synth fallback, reverb,  src/net      wire protocol, client
                mic, MIDI parser, song player     server/      room server
shaders/        common.glsl + every pass          tools/       sample fetcher
```
