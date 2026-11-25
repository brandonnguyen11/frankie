# Opéra Grenouille

CS4361 interactive 3D graphics project: a multiplayer 1950s–70s city plaza and a Palais Garnier–style
opera house where suited frogs perform under player-built stage lighting, filmed on camera rails and saved
as a video file.

**Stack:** C++17 · OpenGL 4.1 core · GLSL (all shading is our own) · GLFW · glad · glm · Assimp · Dear ImGui ·
miniaudio · AVFoundation (macOS video) / ffmpeg (Windows video)

---

## Contents

1. [Requirements](#requirements)
2. [Setup on macOS](#setup-on-macos)
3. [Setup on Windows](#setup-on-windows)
4. [Running the game](#running-the-game)
5. [Multiplayer](#multiplayer)
6. [Controls](#controls)
7. [Recording](#recording)
8. [Troubleshooting](#troubleshooting)
9. [Graphics concepts and where they live](#graphics-concepts-and-where-they-live)
10. [Assets and licensing](#assets-and-licensing)
11. [Developer tools](#developer-tools) · [Project layout](#project-layout)

---

## Requirements

|  | macOS (primary, tested) | Windows 10/11 (supported, less tested) |
|---|---|---|
| GPU | any Mac that supports OpenGL 4.1 (Apple Silicon recommended) | GPU with OpenGL 4.1+ drivers (NVIDIA / AMD / Intel from ~2012 on) |
| Compiler | Xcode Command Line Tools (Apple Clang) | Visual Studio 2022 with **Desktop development with C++** |
| Build tools | CMake ≥ 3.20 | CMake ≥ 3.20 (bundled with Visual Studio) |
| Libraries | Homebrew: `glfw glm assimp` | vcpkg: `glfw3 glm assimp` |
| Video recording | built in (AVFoundation) | `ffmpeg` on `PATH` |
| Disk | ~150 MB including build | ~1 GB including vcpkg |

Already in the repo (nothing to install): glad, Dear ImGui, miniaudio, stb_image, and the CC0 instrument
samples in `assets/samples/`.

---

## Setup on macOS

**1. Install the tools** (one time)

```bash
xcode-select --install                     # Apple compiler; skip if already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"   # Homebrew, if missing
brew install cmake glfw glm assimp
```

**2. Get the code and build**

```bash
git clone https://github.com/brandonnguyen11/frankie.git
cd frankie
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

**3. Run**

```bash
./build/opera_grenouille
```

The first launch asks for **microphone** access (used for singing). Click Allow, or the game still runs
without singing. After pulling new changes, only step 2's last line, `cmake --build build -j`, is needed.

---

## Setup on Windows

**1. Install the tools** (one time)

* [Visual Studio 2022 Community](https://visualstudio.microsoft.com/) → tick **Desktop development with C++**
  (this includes CMake and the MSVC compiler).
* [Git for Windows](https://git-scm.com/download/win).
* vcpkg (C++ package manager). In **Developer PowerShell for VS 2022**:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install glfw3 glm assimp --triplet x64-windows
```

* Optional, for recording video: ffmpeg.

```powershell
winget install Gyan.FFmpeg      # then open a new terminal so it is on PATH
```

**2. Get the code and build** (Developer PowerShell for VS 2022)

```powershell
git clone https://github.com/brandonnguyen11/frankie.git
cd frankie
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -A x64
cmake --build build --config Release
```

**3. Run**

```powershell
.\build\Release\opera_grenouille.exe
```

Shaders, assets and the vcpkg DLLs are copied next to the `.exe`, so the `build\Release` folder can be
zipped and run on another Windows PC.

> Windows status: the networking, audio and recorder all have Windows code paths, but day-to-day
> development and testing happens on macOS. If a build error appears, open an issue with the full log.

---

## Running the game

```bash
./build/opera_grenouille                       # single player
./build/opera_grenouille --join HOST:PORT --name Ribbit   # join a room straight away
./build/opera_grenouille --help                # all options
```

(Windows: replace `./build/opera_grenouille` with `.\build\Release\opera_grenouille.exe`.)

The game starts in the city plaza with an establishing shot (any key skips it). Walk into the opera-house
doors to go inside, then pick a role from the left panel. Everything works in single player. Joining a room only
adds other people.

---

## Multiplayer

Every player needs the game built on their own computer (see above). One person also runs the room server.

**Host**

```bash
./build/opera_server 7777          # Windows: .\build\Release\opera_server.exe 7777
```

It prints room codes such as `192.168.1.42:7777` (and the link `opera://192.168.1.42:7777`). Share the
`192.168…` one. Leave this window open; the host then starts the game and joins with `127.0.0.1:7777`.

**Everyone else**

* In game: left panel → **Room** → type a **Name**, paste the room code → **Join room**. **Leave room**
  disconnects.
* Or: `./build/opera_grenouille --join 192.168.1.42:7777 --name Ribbit`

Notes:

* Up to 8 players. Late joiners receive the current lighting rig, poses, desk owner, recording state and song.
* Everyone must be on the **same network**. Campus Wi-Fi often blocks device-to-device traffic, so a phone
  hotspot is the most reliable option for demos. Across the internet you need port forwarding or a tunnel
  (e.g. Tailscale).
* The first time the server runs, macOS / Windows Firewall asks to allow incoming connections. Choose
  **Allow** (on Windows, tick **Private networks**).
* To test on one machine, run the server and two copies of the game, both joining `127.0.0.1:7777`.
* Synced: players, roles, orchestra seats, notes, lighting edits and cues, singing (push-to-talk), song playback.

---

## Controls

The panel on the left has a button for every action (F-keys are media keys on most Mac laptops; hold Fn).

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
| **Musician** | Claim a chair on stage with E (or click one in the panel). Seated: A S D F G H J K white keys, W E T Y U black keys, Z/X octave, hold Space for loud, Backspace to stand. Load a MIDI or MP3/WAV from the panel |
| **Singer** | Taken to a stage mark. Hold G (or Option/Alt) to sing, M mute, N hear yourself (headphones only) |
| **Lighting designer** | 1 walk · 2 top-down (click the stage to aim) · 3 through the lens (drag/arrow keys pan & tilt) · [ ] select light. Panel: mount, intensity, Kelvin/RGB, beam, softness, shadows, gobo, haze, house, chandelier, cues, presets. **Frog spots**: Spotlight / Select / Off per frog, *Spot every frog*, *Clear frog spots* |
| **Camera operator** | see below |
| **Audience** | E (or panel button) to take the nearest free seat |

**Camera operator**

| Mode | Key | Controls |
|---|---|---|
| Rails | 1–5 | A/D dolly · mouse moves the aim point · Shift slow · Q/E zoom 18–135 mm · R/F focus pull · T autofocus · Z/X aperture · `,` / `.` exposure. **Lock on stage** (on by default) keeps the camera turned toward the stage while dollying; **Re-centre aim** resets it |
| Free 360 | 6 | Mouse looks anywhere · WASD fly · Space up · C down · Shift slow |
| Drone | 7 | Gimbal always locked on the stage · WASD strafe/advance · Space up · C down · mouse moves the aim point · **Re-centre on stage** |
| Any mode | V | **Record / stop** |

---

## Recording

Press **V** (or the red record button) as camera operator. While recording, the screen shows a camcorder
viewfinder (REC dot, timecode, audio meters, red tally border). The viewfinder is **not** in the saved video.

* Output: 1920×1080, constant 30 fps, H.264 + AAC `.mp4`, containing the camera view and the room's full mix.
* macOS: on stop a save dialog opens; default folder `~/Movies/Opera Grenouille/`.
  Windows: saved automatically to `%USERPROFILE%\Videos\Opera Grenouille\` (status bar shows the file name).
* macOS uses AVFoundation (no extra install). Windows uses ffmpeg, which must be on `PATH`.

---

## Troubleshooting

| Problem | Fix |
|---|---|
| `Could not find a package configuration file provided by "glfw3"` | macOS: `brew install glfw glm assimp`. Windows: pass `-DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake` and make sure the vcpkg install step finished |
| CMake used an old cache after changing setup | delete the `build` folder and configure again |
| Black window / "OpenGL 4.1 not supported" (Windows) | update the GPU driver from NVIDIA/AMD/Intel; on laptops, set the game to use the dedicated GPU |
| Slow frame rate | F1 (Low) or F2 (Medium) quality |
| No sound | check the system output device; samples live in `assets/samples/` (re-fetch with `python3 tools/fetch_samples.py`) |
| Singing does nothing | allow microphone access: macOS *System Settings → Privacy & Security → Microphone*; Windows *Settings → Privacy → Microphone* |
| "Join failed" | server not running, wrong IP, different network, or firewall blocked it |
| Windows: recording fails with "could not start ffmpeg" | install ffmpeg and open a new terminal so it is on `PATH` |
| Windows: missing `glfw3.dll` / `assimp*.dll` | run the `.exe` from `build\Release`, where vcpkg copies the DLLs |

---

## Graphics concepts and where they live

| Concept | Where |
|---|---|
| Scene graph / transforms | `actors/Frog.cpp`: hierarchical frog rig (hips → torso → head/jaw, shoulder → elbow → hand, hip → knee → foot) posed procedurally for walk, sit, singing and each instrument |
| Cameras & projection | `camera/OrbitCamera`, `camera/RailCamera`, `camera/FlyCamera`: focal length → vertical FOV on a 24 mm sensor; ortho top-down desk view; free-look and stage-locked drone (look-at gimbal, slerped, banking) |
| Curves & interpolation | Catmull-Rom rails with an arc-length table for constant dolly speed; quaternion slerp pans; frame-rate-independent exponential smoothing; spline + slerp establishing sweep |
| Shading models | `shaders/common.glsl`: Cook-Torrance GGX (height-correlated Smith), anisotropic GGX gold leaf, Charlie sheen velvet and wool, wrap + transmission frog skin, clear coat (lacquer, car paint, wet skin, puddles) |
| Lights & attenuation | spot/point/sun in physical units (candela, lux, nits), windowed inverse-square falloff, Kelvin → linear RGB, procedural gobos, per-frog followspots |
| Shadows | 8-layer spot shadow-map array + 4096² sun map, slope-scaled bias, normal offset, rotated-Poisson PCF with hardware compare |
| GI approximations | hemisphere "light probe" ambient + reflection lobes toward the chandelier and stage, SSAO (half-res, depth-aware blur) |
| Volumetrics | `shaders/volume.frag`: per-pixel ray/cone intersection, raymarched 3D-noise haze, Henyey-Greenstein phase, shadow-mapped so frogs cut the beams |
| Framebuffers & post | HDR RGBA16F, depth/normal prepass, bloom mip chain, thin-lens DOF from circle of confusion, AgX/ACES tone mapping, EV100 exposure, FXAA, film grain/vignette |
| Visibility & performance | frustum culling, instancing (≈600 seats), quality tiers, async PBO readback for recording |

---

## Assets and licensing

* Architecture, frogs, props and city are procedural (`scene/AuditoriumScene.cpp`, `scene/CityScene.cpp`), our own work.
* Instrument samples: VSCO 2 Community Edition, **CC0** (`assets/samples/SOURCE.txt`).
* Agent 327: Operation Barbershop (Blender Studio, CC BY-ND) is a **lighting reference only**; no geometry is used.
* **Using Sketchfab models:** licensed glTF/GLB/OBJ/FBX files can be dropped into `assets/models/` and listed in
  `assets/models/models.txt` (`<city|house> <file> <scale> <x> <y> <z> <yawDeg> [replace]`). Material names
  containing *velvet/seat/curtain*, *gold/gilt*, *marble*, *wood*, *glass* get the matching custom shader.
  The *Opera House* model by SammyCAnimations is CC BY 4.0 (credit required). The *Opera Palais Garnier* model
  has no stated license, so it must not be shipped without the author's permission. Add every CC BY asset to
  `assets/credits/CREDITS.txt`.

---

## Developer tools

```bash
# headless screenshots (city | plaza | house | film | pit | balcony | lighting | musician | orbit | drone | free | spots | rec)
./build/opera_grenouille --shot out.png --scene balcony --size 1920x1080 [--frames N] [--hud]
# film a take with the demo song and exit (checks the recording pipeline)
./build/opera_grenouille --record-test 10 --scene film
```

## Project layout

```
src/core        app loop, input, asset paths      src/render   meshes, shaders, render targets, frame graph
src/scene       world/gameplay, venue + city      src/actors   frog rig and animation
src/lighting    rig, mounts, cues, followspots    src/camera   orbit/rail/fly cameras, recorder (AVFoundation / ffmpeg)
src/audio       sampler, synth fallback, reverb,  src/net      wire protocol, client
                mic, MIDI parser, song player     server/      room server
shaders/        common.glsl + every pass          tools/       sample fetcher
third_party/    glad, imgui, miniaudio, stb       assets/      samples, textures, models, credits
```
