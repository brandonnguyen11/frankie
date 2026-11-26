# Opéra Grenouille: Full Test Plan

A step-by-step checklist that exercises every feature: rendering, the plaza, every role, MIDI and audio
songs, live notes, microphone and voice, lighting (including followspots and cues), all three camera modes,
recording, and multiplayer.

Each test has **steps** and an **expected result**. Tick the box when it passes; write the problem next to it
when it fails. Commands use macOS paths. On Windows, replace `./build/opera_grenouille` with
`.\build\Release\opera_grenouille.exe` (same for `opera_server`).

Build and setup are in [README.md](README.md).

---

## Contents

0. [What you need](#0-what-you-need)
1. [Automated smoke tests (5 min)](#1-automated-smoke-tests)
2. [Launch, plaza and moving around](#2-launch-plaza-and-moving-around)
3. [Auditorium, panel and graphics settings](#3-auditorium-panel-and-graphics-settings)
4. [Roles](#4-roles)
5. [Musician: live playing](#5-musician-live-playing)
6. [Musician: MIDI songs](#6-musician-midi-songs)
7. [Musician: MP3 / WAV / FLAC songs](#7-musician-mp3--wav--flac-songs)
8. [Singer and microphone](#8-singer-and-microphone)
9. [Lighting designer](#9-lighting-designer)
10. [Frog followspots](#10-frog-followspots)
11. [Lighting cues and presets](#11-lighting-cues-and-presets)
12. [Camera: rails](#12-camera-rails)
13. [Camera: Free 360 and Drone](#13-camera-free-360-and-drone)
14. [Recording and the viewfinder](#14-recording-and-the-viewfinder)
15. [Audience](#15-audience)
16. [Multiplayer: one computer](#16-multiplayer-one-computer)
17. [Multiplayer: two or more computers](#17-multiplayer-two-or-more-computers)
18. [Multiplayer: error cases](#18-multiplayer-error-cases)
19. [Windows-specific checks](#19-windows-specific-checks)
20. [Performance](#20-performance)
21. [Demo-day dress rehearsal](#21-demo-day-dress-rehearsal)
22. [Sign-off sheet](#22-sign-off-sheet)

---

## 0. What you need

| Item | Used in |
|---|---|
| Headphones (prevents mic echo) | 8, 17 |
| A working microphone | 8, 17 |
| A real MIDI file with several instruments (`.mid`) | 6, 16, 17 |
| A song file (`.mp3`, `.wav` or `.flac`) | 7, 17 |
| A second computer on the same network (or a phone hotspot) | 17 |
| A video player (QuickTime / VLC) | 14 |
| ~45 min for a full pass; ~10 min for sections 1 + 21 only | |

The bundled `assets/samples/demo.mid` and `demo.wav` are short test clips. Use real songs for sections 6–7.

---

## 1. Automated smoke tests

These run without clicking anything and catch most breakages. Run them after every build.

- [ ] **Build succeeds**
  ```bash
  cmake --build build -j
  ```
  Expected: ends with `Built target opera_grenouille` and `Built target opera_server`, no errors.

- [ ] **Every scene renders** (headless screenshots)
  ```bash
  for s in city plaza house film pit balcony lighting musician orbit spots rec drone free; do
    ./build/opera_grenouille --shot /tmp/og_$s.png --scene $s --size 1280x720 || echo "FAIL $s"
  done
  open /tmp/og_*.png
  ```
  Expected: no `FAIL`; each image shows the right view, nothing black, pink or garbled.
  `spots` shows a narrow light on every frog; `drone` and `free` show the two new cameras.

- [ ] **HUD renders**
  ```bash
  ./build/opera_grenouille --shot /tmp/og_hud.png --scene rec --hud
  ```
  Expected: the image includes the viewfinder overlay (REC, timecode, meters, border).

- [ ] **Recording pipeline**
  ```bash
  ./build/opera_grenouille --record-test 10 --scene film
  ```
  Expected: prints the saved `.mp4` path. Open it: ~10 s, 1920×1080, the demo song is audible,
  no viewfinder graphics in the video.

- [ ] **Room server protocol**
  ```bash
  ./build/opera_server 7790 &
  python3 tools/net_smoke_test.py 7790
  kill %1
  ```
  Expected: last line `ALL NET TESTS PASSED`. (This server now holds an empty test rig, so stop it
  afterwards and don't play on it.)

- [ ] **Help text**
  ```bash
  ./build/opera_grenouille --help
  ```
  Expected: lists `--join`, `--name`, `--shot`, `--scene`, `--size`, `--frames`, `--hud`, `--record-test`.

---

## 2. Launch, plaza and moving around

Start the game: `./build/opera_grenouille`

| # | Steps | Expected |
|---|---|---|
| - [ ] 2.1 | Launch | Window opens; an establishing camera sweep over the plaza plays |
| - [ ] 2.2 | Press any key during the sweep | Sweep is skipped; you control your frog in the plaza |
| - [ ] 2.3 | Microphone prompt (first launch, macOS) | System asks for microphone access; click **Allow** |
| - [ ] 2.4 | W A S D / arrow keys | Frog walks in that direction relative to the camera |
| - [ ] 2.5 | Hold Shift while moving | Frog runs |
| - [ ] 2.6 | Right-drag the mouse | Camera orbits the frog |
| - [ ] 2.7 | Tab, then move the mouse | Mouse is captured, camera looks around without dragging |
| - [ ] 2.8 | Esc | Mouse is released |
| - [ ] 2.9 | Mouse wheel | Camera zooms in/out |
| - [ ] 2.10 | Walk to the edges of the plaza / into the opera walls | Frog stops at the boundary; never walks out of the world or through walls |
| - [ ] 2.11 | Panel → **Graphics → Time of day** slider | Sky and sun move from afternoon to dusk; shadows change direction and length |
| - [ ] 2.12 | Panel → Role section in the plaza | Says roles start once you're inside |
| - [ ] 2.13 | Walk into the centre doors of the opera (or press E / Enter near them) | Fades and transitions into the auditorium |

---

## 3. Auditorium, panel and graphics settings

| # | Steps | Expected |
|---|---|---|
| - [ ] 3.1 | Look at the auditorium | Stage with 12 orchestra frogs (front row on deck, back row on a riser), red velvet seats, gold trim, chandelier, visible light beams in haze |
| - [ ] 3.2 | Panel title | Reads *Palais Garnier - auditorium* with an fps counter |
| - [ ] 3.3 | Press C | Frog sits in the nearest seat; C again stands up. Panel button **Sit down / Stand up (C)** does the same |
| - [ ] 3.4 | F10 | Panel hides; F10 again shows it (on Mac laptops hold **Fn**) |
| - [ ] 3.5 | F1, F2, F3 (or **Graphics → Quality**) | Low / Medium / High; Low visibly drops shadow detail and haze quality, fps goes up |
| - [ ] 3.6 | **Graphics → Tone mapping**: AgX ↔ ACES | Colours change subtly; ACES is more saturated and contrasty |
| - [ ] 3.7 | **Graphics → FXAA** off/on | Off: jagged edges on railings and seat rows; on: smoothed |
| - [ ] 3.8 | **Graphics** stats line | Shows fps, ms, resolution, draws, culled, lights, shadow maps; values change as you look around |
| - [ ] 3.9 | Panel → **Controls** | Shows the key help |
| - [ ] 3.10 | Panel → **Credits** | Shows asset credits (VSCO2 CC0, etc.) |
| - [ ] 3.11 | Press P (or **Return to the plaza (P)**) | Back in the plaza; can walk back in |
| - [ ] 3.12 | Resize / fullscreen the window | Image rescales; no stretching or black regions |

---

## 4. Roles

Inside the auditorium, click each role button at the top of the panel.

| # | Role | Expected |
|---|---|---|
| - [ ] 4.1 | **Musician** | Panel shows *Orchestra* seat table, *Load a song* |
| - [ ] 4.2 | **Singer** | Frog is taken to a stage mark; panel shows *Microphone* section |
| - [ ] 4.3 | **Lighting** | Panel shows *Lighting desk*, *Spotlight a frog*, *Lights*, *Cues* |
| - [ ] 4.4 | **Camera** | View switches to the film camera; viewfinder overlay appears; panel shows *Film camera* |
| - [ ] 4.5 | **Audience** | Panel shows *Take the nearest seat (E)* |
| - [ ] 4.6 | Switch roles several times quickly | No crash; the active role button is highlighted orange |

---

## 5. Musician: live playing

Choose **Musician**.

| # | Steps | Expected |
|---|---|---|
| - [ ] 5.1 | Click a free chair in the seat table (or walk to it on stage and press E) | *Seated at Violin 2* (etc.); your frog sits at that chair with the instrument |
| - [ ] 5.2 | Check the line under *Seated at* | Shows the octave and **sampled** (not *synth*) for every instrument |
| - [ ] 5.3 | Press A S D F G H J K | C major scale (white keys) plays with the real instrument sound |
| - [ ] 5.4 | Press W E T Y U | Sharps/flats (black keys) |
| - [ ] 5.5 | Hold several keys | Chord plays; notes stop when keys are released |
| - [ ] 5.6 | Z / X | Octave down / up; status bar shows *Octave N*; pitch changes |
| - [ ] 5.7 | Hold Space while playing | Notes are louder (higher velocity) |
| - [ ] 5.8 | Watch your frog | Plays the instrument (bowing, keys, valves, mallets) while notes sound |
| - [ ] 5.9 | Repeat 5.1–5.8 for **each instrument** (table below) | Each sounds like itself across low and high octaves; no clicks or silence |
| - [ ] 5.10 | Backspace (or **Stand up**) | Frog leaves the chair; chair shows as free again |
| - [ ] 5.11 | Claim a different chair | Switches cleanly; previous chair is released |

**Every instrument on the keyboard.** The instrument comes from the chair you claim; seat labels are in the
panel's seat table. For each one: claim the chair, play the scale (A–K), the black keys (W E T Y U), go down
two octaves with Z and up two with X, and play a loud note with Space held.

| # | Instrument | Chair to claim | Expected sound and animation |
|---|---|---|---|
| - [ ] 5.12 | Piano | *Piano 1* (front row, far left) | Piano tone with natural decay; frog's hands on the keys |
| - [ ] 5.13 | Violin | *Violin 2* or *3* (front), *Violin 9* or *10* (back) | Bowed violin, sustains while key is held; frog bows |
| - [ ] 5.14 | Flute | *Flute 4* or *5* (front) | Breathy flute, sustains; frog holds flute to its mouth |
| - [ ] 5.15 | Trumpet | *Trumpet 6* (front) or *Trumpet 11* (back) | Bright brass, sustains; frog raises the trumpet |
| - [ ] 5.16 | Cello | *Cello 7* or *8* (back row, left) | Deep bowed cello; best in lower octaves (Z) |
| - [ ] 5.17 | Timpani | *Timpani 12* (back row, far right) | Drum hit that rings out; frog strikes with mallets; pitch changes per key |
| - [ ] 5.18 | All of the above | very low and very high octaves | Still plays (pitch-shifted samples), no silence, no crash |
| - [ ] 5.19 | Two at once | two players (section 16) on different chairs | Both instruments heard together by both players |

---

## 6. Musician: MIDI songs

Choose **Musician** → **Load a song**.

| # | Steps | Expected |
|---|---|---|
| - [ ] 6.1 | Click **…** next to the MIDI box | macOS file dialog filtered to `.mid/.midi` (Windows: type the path) |
| - [ ] 6.2 | Pick a multi-instrument MIDI → **Load MIDI** | Status *Loaded <title>*; shows title, length in seconds, number of parts |
| - [ ] 6.3 | Parts table | One row per track/channel with name, GM program number, and an assigned seat |
| - [ ] 6.4 | **Play song** | Orchestra frogs play their assigned parts in time with the music; *Playing x / y s* counts up |
| - [ ] 6.5 | Listen | Sounds like the song; instruments match parts reasonably; no stutter or drift |
| - [ ] 6.6 | While playing, play live notes on your own chair | Your notes mix with the song |
| - [ ] 6.7 | Change a part's seat in the dropdown, play again | That part now comes from the new chair and with that chair's instrument |
| - [ ] 6.8 | Set a part to **(muted)**, play again | That part is silent; its frog stops playing |
| - [ ] 6.9 | **Stop** | All notes stop immediately; no hanging notes |
| - [ ] 6.10 | Let a song play to the end | Stops by itself about 1 s after the last note |
| - [ ] 6.11 | Load the bundled `assets/samples/demo.mid` | Loads (short, few notes) |
| - [ ] 6.12 | Type a wrong path → **Load MIDI** | Status shows *MIDI: <error>*; no crash |
| - [ ] 6.13 | Rename a text file to `.mid` and load it | Error message; no crash |

---

## 7. Musician: MP3 / WAV / FLAC songs

| # | Steps | Expected |
|---|---|---|
| - [ ] 7.1 | **…** next to the audio box → pick an `.mp3` → **Load MP3/WAV** | Loads; shows duration |
| - [ ] 7.2 | **Play audio** | Song plays through the hall (with reverb); *Playing x / y s  level n* updates |
| - [ ] 7.3 | Watch the frogs | Animate to loudness and beat (bigger moves on loud parts) |
| - [ ] 7.4 | **Stop audio** | Stops immediately |
| - [ ] 7.5 | Repeat with a `.wav` and a `.flac` | All three formats play |
| - [ ] 7.6 | Load a long song (5+ min) | Loads without freezing for long; plays to the end |
| - [ ] 7.7 | Bad path / unsupported file | Error in the status bar; no crash |

---

## 8. Singer and microphone

Put on **headphones**. Choose **Singer**.

| # | Steps | Expected |
|---|---|---|
| - [ ] 8.1 | Look at the panel | *Microphone* section with level bar. If it says *No microphone*, allow access (macOS: System Settings → Privacy & Security → Microphone; Windows: Settings → Privacy → Microphone) and restart |
| - [ ] 8.2 | Talk without holding anything | Level bar moves; nothing is sent (not LIVE) |
| - [ ] 8.3 | Hold **G** (or Option / Alt) and sing | Panel shows **LIVE**; your frog's mouth opens with your voice |
| - [ ] 8.4 | Press **N** (or tick *Hear myself with hall reverb*) | You hear yourself in the headphones with hall reverb; status *Hearing yourself* |
| - [ ] 8.5 | Move the **Hall reverb** slider | More/less echo on your voice and instruments |
| - [ ] 8.6 | Press **M** (or tick *Mute*) while holding G | Status *Mic muted*; no longer LIVE; mouth stops |
| - [ ] 8.7 | M again | *Mic live* |
| - [ ] 8.8 | N again | Self-monitor off |
| - [ ] 8.9 | Sing while a song plays | Voice and song are both heard (and both are recorded, see 14.10) |

---

## 9. Lighting designer

Choose **Lighting**.

**Views**

| # | Steps | Expected |
|---|---|---|
| - [ ] 9.1 | **Walk (1)** / press 1 | Normal walking view |
| - [ ] 9.2 | **Top-down (2)** / press 2 | Plan view of the stage with light positions; WASD pans |
| - [ ] 9.3 | In top-down, click on the stage | Selected light swings to aim at that spot |
| - [ ] 9.4 | **Through lens (3)** / press 3 | You see from the selected light; right-drag / Tab+mouse / arrow keys pan and tilt it |
| - [ ] 9.5 | `[` and `]` | Select previous / next light; through-lens view jumps to that light |

**Room controls**

| # | Steps | Expected |
|---|---|---|
| - [ ] 9.6 | **Haze** 0 → 1 | Beams invisible at 0, thick and visible at 1 |
| - [ ] 9.7 | **House lights** 0 → 1 | Auditorium walls and seats go from dark to lit |
| - [ ] 9.8 | **Chandelier** 0 → 1 | Chandelier glow and its light on the ceiling increase |
| - [ ] 9.9 | **House exposure (EV)** | Whole image brighter/darker |
| - [ ] 9.10 | **Agent 327 preset** | Rig resets to the warm-key / cool-rim look |

**Per-light controls** (select a light in the *Lights* list)

| # | Steps | Expected |
|---|---|---|
| - [ ] 9.11 | Toggle the light's checkbox | Light turns off/on |
| - [ ] 9.12 | **Add light** | New light appears in the list and on the rig |
| - [ ] 9.13 | Add until full | Stops at 20 lights; no crash |
| - [ ] 9.14 | **Remove selected** | Light disappears |
| - [ ] 9.15 | **Mount** dropdown | Light moves to that truss / balcony rail / boom / booth |
| - [ ] 9.16 | **Intensity (kcd)** | Brighter / dimmer pool on stage |
| - [ ] 9.17 | **Colour temperature** on, move **Kelvin** 2000 → 10000 | Orange → white → blue |
| - [ ] 9.18 | Untick colour temperature, use the colour swatch | Any RGB colour |
| - [ ] 9.19 | **Beam angle** | Pool and beam widen / narrow |
| - [ ] 9.20 | **Edge softness** | Hard-edged vs soft-edged pool |
| - [ ] 9.21 | **Casts shadows** off/on | Frog shadows from that light vanish / return; frogs cut the haze beam when on |
| - [ ] 9.22 | **Gobo**: Breakup, Window, Stars | Pattern appears in the pool and in the beam |
| - [ ] 9.23 | **Follow**: pick a performer | Light tracks that frog as it moves |
| - [ ] 9.24 | **Pan** / **Tilt** sliders | Light aims manually |

---

## 10. Frog followspots

In **Lighting** → *Spotlight a frog*.

| # | Steps | Expected |
|---|---|---|
| - [ ] 10.1 | **Spotlight** next to one frog | A narrow warm spot lands on just that frog, named *Spot: <frog>* in the Lights list |
| - [ ] 10.2 | Make that frog move (e.g. a player walking, or a singer) | Spot follows it |
| - [ ] 10.3 | **Select** next to it | That spot becomes the selected light (editable below) |
| - [ ] 10.4 | **Off** | That spot is removed |
| - [ ] 10.5 | **Spot every frog** | Every frog on stage gets its own spot (up to the 20-light limit) |
| - [ ] 10.6 | Look at the curtains | No stray spot patches on the curtains; all musicians visible |
| - [ ] 10.7 | **Clear frog spots** | All *Spot:* lights removed; your other lights remain |

---

## 11. Lighting cues and presets

| # | Steps | Expected |
|---|---|---|
| - [ ] 11.1 | Set a look; name it *Cue 1*, fade 3 s → **Record cue** | Cue appears in the list; name box advances to *Cue 2* |
| - [ ] 11.2 | Make a very different look (colours, haze, house) → **Record cue** | *Cue 2* added |
| - [ ] 11.3 | **GO** on Cue 1 | Rig smoothly crossfades back over 3 s; active cue marked with `>` |
| - [ ] 11.4 | Record a cue with fade 0 and GO it | Instant snap |
| - [ ] 11.5 | **Save preset…** → choose a file | Status *Saved <path>* |
| - [ ] 11.6 | Change the rig, then **Load preset…** that file | Rig returns exactly to the saved state |
| - [ ] 11.7 | Load a non-preset file | *Could not load*; rig unchanged; no crash |

---

## 12. Camera: rails

Choose **Camera** → **Rails (1-5)**.

| # | Steps | Expected |
|---|---|---|
| - [ ] 12.1 | Press 1–5 (or the rail buttons) | Camera jumps to each rail (stalls, balcony, etc.); active rail highlighted |
| - [ ] 12.2 | A / D | Camera dollies smoothly along the curved rail with ease-in/out |
| - [ ] 12.3 | Dolly with **Lock on stage** ticked (default) | Camera keeps turning to keep the stage centred |
| - [ ] 12.4 | Move the mouse | Aim point moves left/right and up/down on the stage |
| - [ ] 12.5 | **Re-centre aim** | Aim snaps back to centre stage |
| - [ ] 12.6 | Untick **Lock on stage**, dolly, move mouse | Free pan/tilt; camera no longer tracks the stage |
| - [ ] 12.7 | Hold Shift while dollying / aiming | Slower, finer movement |
| - [ ] 12.8 | Q / E (or **Focal length**) | Zoom from 18 mm (wide) to 135 mm (tight) |
| - [ ] 12.9 | R / F (or **Focus**) | Focus pulls near/far; background or foreground blurs; **Auto focus** unticks |
| - [ ] 12.10 | T | Auto focus back on; aim point snaps sharp |
| - [ ] 12.11 | Z / X (or **Aperture**) | Lower f-number = more blur (shallow depth of field) |
| - [ ] 12.12 | `,` / `.` (or **Exposure**) | Darker / brighter |
| - [ ] 12.13 | **Reset position** | Camera returns to the rail start |

---

## 13. Camera: Free 360 and Drone

| # | Steps | Expected |
|---|---|---|
| - [ ] 13.1 | **Free 360 (6)** / press 6, then Tab (or right-drag) | Status explains the controls; moving the mouse looks anywhere, including straight up at the ceiling |
| - [ ] 13.2 | WASD, Space, C | Fly forward/back/sideways, up, down |
| - [ ] 13.3 | Fly into walls, floor, ceiling | Camera stops at surfaces; doesn't leave the building |
| - [ ] 13.4 | Shift | Slow movement |
| - [ ] 13.5 | **Drone (7)** / press 7 | Camera always points at the stage |
| - [ ] 13.6 | A / D (strafe) | Drone slides sideways while the gimbal keeps the stage centred and in focus; banks slightly into the move |
| - [ ] 13.7 | Space / C | Rises / descends, still aimed at the stage |
| - [ ] 13.8 | W / S | Closer / farther from the stage |
| - [ ] 13.9 | Hands off | Gentle hover bob |
| - [ ] 13.10 | Mouse or **Aim left/right / Aim height** sliders | Aim point moves across the stage |
| - [ ] 13.11 | **Focus locked on aim point** on/off | On: subject stays sharp while moving; off: manual focus |
| - [ ] 13.12 | **Re-centre on stage**, **Reset drone** | Aim resets; drone returns to start |
| - [ ] 13.13 | Q/E zoom, Z/X aperture, `,`/`.` exposure in both modes | Work as on rails |
| - [ ] 13.14 | Switch back to **Rails** | Rails resume where they were |

---

## 14. Recording and the viewfinder

Choose **Camera**. Start a song (MIDI or audio) first so there is sound.

| # | Steps | Expected |
|---|---|---|
| - [ ] 14.1 | Before recording | Viewfinder corner brackets, lens readout (mm, f-stop, focus), battery, round **red record button** |
| - [ ] 14.2 | Press **V** (or click the red button / **rec**) | Recording starts: blinking red **REC**, running timecode, red tally border, button turns into a stop square, side panel hides |
| - [ ] 14.3 | Watch **CH1/CH2** meters | Move with the music and voices |
| - [ ] 14.4 | Dolly, zoom and switch camera modes while rolling | All work; recording continues |
| - [ ] 14.5 | Press V again | Recording stops; panel returns |
| - [ ] 14.6 | macOS | Save dialog opens (default `~/Movies/Opera Grenouille/`); choose a place |
| - [ ] 14.7 | Windows | Saved automatically to `%USERPROFILE%\Videos\Opera Grenouille\`; status shows the file |
| - [ ] 14.8 | Open the video | 1920×1080, 30 fps, plays in QuickTime/VLC |
| - [ ] 14.9 | Look at the video | Camera view only: **no** viewfinder graphics, **no** panel |
| - [ ] 14.10 | Listen | Instruments, song, singing voice and reverb are all there |
| - [ ] 14.11 | Clap on screen / hit a timpani note | Sound lines up with the picture (no drift by the end) |
| - [ ] 14.12 | Compare length | Video length = time you recorded (even if fps dipped) |
| - [ ] 14.13 | Record for 3+ minutes | File plays fully; no freeze or crash |
| - [ ] 14.14 | Record again and **Cancel** the save dialog | No crash; the take is kept in `~/Movies/Opera Grenouille/` and the status shows its path |

---

## 15. Audience

| # | Steps | Expected |
|---|---|---|
| - [ ] 15.1 | Choose **Audience**, walk near the seats, press E (or **Take the nearest seat**) | Frog sits in the nearest free seat |
| - [ ] 15.2 | C | Stand up |

---

## 16. Multiplayer: one computer

Tests networking without a second machine.

```bash
./build/opera_server 7777                                  # Terminal 1, leave open
./build/opera_grenouille --join 127.0.0.1:7777 --name A    # Terminal 2
./build/opera_grenouille --join 127.0.0.1:7777 --name B    # Terminal 3
```

| # | Steps | Expected |
|---|---|---|
| - [ ] 16.1 | Server terminal | Prints `room code: 127.0.0.1:7777` and a `192.168…` code with an `opera://` link |
| - [ ] 16.2 | After both join | Server prints `+ A joined as #1`, `+ B joined as #2` |
| - [ ] 16.3 | Each window → **Room** | *In room 127.0.0.1:7777 as #n*, players list shows the other with their role |
| - [ ] 16.4 | Walk A around | A's frog moves smoothly in B's window (and vice versa) |
| - [ ] 16.5 | A goes inside; B stays in the plaza | Each sees the other only where they actually are |
| - [ ] 16.6 | A and B both Musician; A takes a chair | That chair shows as taken for B; B can't take it |
| - [ ] 16.7 | A plays notes | B hears them and sees A's frog playing |
| - [ ] 16.8 | A loads and plays a MIDI song | B sees *A started the music*, hears it and sees the orchestra play |
| - [ ] 16.9 | A plays an MP3 | B hears it (streamed) |
| - [ ] 16.10 | A sings (hold G) | B hears A's voice and sees A's mouth move (use headphones) |
| - [ ] 16.11 | A and B both Lighting | First one has the desk; the other sees *Another designer has the desk* with controls greyed out |
| - [ ] 16.12 | B clicks **Take over desk** | B can now edit; A is locked out |
| - [ ] 16.13 | Desk owner changes haze, colours, adds a light, adds frog spots | Same change appears on the other screen within a moment |
| - [ ] 16.14 | Desk owner GOes a cue | Crossfade shows on both |
| - [ ] 16.15 | A becomes Camera and records | B sees a red *REC* banner with A's name at the top |
| - [ ] 16.16 | Start a third client C after changes were made (late join) | C immediately has the current lighting, sees A and B in place, sees the REC banner if recording, gets the song state |
| - [ ] 16.17 | B clicks **Leave room** | Server prints `- B left`; B's frog disappears for A; B keeps playing single-player |
| - [ ] 16.18 | Close C's window without leaving | Server notices; C's frog disappears for others |
| - [ ] 16.19 | Join with the link form `opera://127.0.0.1:7777` in the Room box | Works the same |
| - [ ] 16.20 | Join from the panel instead of `--join` (type Name + code → **Join room**) | Status *Joined 127.0.0.1:7777* |

---

## 17. Multiplayer: two or more computers

Do this before demo day, on the network you'll present on (or a phone hotspot).

| # | Steps | Expected |
|---|---|---|
| - [ ] 17.1 | Host: `./build/opera_server 7777`; note the `192.168…` code | Code printed |
| - [ ] 17.2 | First run on the host | macOS / Windows asks to allow incoming connections → **Allow** (Windows: tick *Private networks*) |
| - [ ] 17.3 | Host joins with `127.0.0.1:7777`; guest joins with the `192.168…` code | Both appear in the server log and each other's player list |
| - [ ] 17.4 | Repeat 16.4–16.16 across the two machines | Same results |
| - [ ] 17.5 | Voice both ways with headphones | Clear, no echo, delay under ~half a second |
| - [ ] 17.6 | Guest records a video while the host plays music and sings | Guest's video contains the host's music and voice |
| - [ ] 17.7 | Play together for 10 minutes | No disconnects, no growing lag |
| - [ ] 17.8 | 3+ machines (if available) | Everyone sees and hears everyone |

If the guest can't connect: check the IP, that both are on the same Wi-Fi, and the firewall. Campus
Wi-Fi often blocks device-to-device connections, so switch to a phone hotspot.

---

## 18. Multiplayer: error cases

| # | Steps | Expected |
|---|---|---|
| - [ ] 18.1 | Join `127.0.0.1:7799` (no server) | *Join failed: no room server at…* within ~1 s; game keeps running |
| - [ ] 18.2 | Join `192.168.1.250:7777` (nobody there) | Fails within ~3 s; no freeze longer than that |
| - [ ] 18.3 | Join `not-a-host:7777` | *Join failed: cannot resolve…* |
| - [ ] 18.4 | Join `127.0.0.1:abc` | *Join failed: bad room address* |
| - [ ] 18.5 | Kill the server while two clients are connected | Clients drop back to single player; no crash |
| - [ ] 18.6 | Restart the server and rejoin | Works |
| - [ ] 18.7 | Connect 9 clients | 9th is rejected; server prints `room full, rejected a connection` |
| - [ ] 18.8 | Start a second server on the same port | Prints `bind failed on port 7777` |
| - [ ] 18.9 | Turn Wi-Fi off on a guest for 10 s | Guest disconnects cleanly; others see them leave |

---

## 19. Windows-specific checks

| # | Steps | Expected |
|---|---|---|
| - [ ] 19.1 | Build with Visual Studio + vcpkg (see README) | Builds both `opera_grenouille.exe` and `opera_server.exe` |
| - [ ] 19.2 | Run from `build\Release` | Starts; no missing-DLL errors |
| - [ ] 19.3 | Sections 2–15 | Same as macOS, except file boxes need typed paths (no file dialog) |
| - [ ] 19.4 | Record without ffmpeg installed | Clear error *could not start ffmpeg*; no crash |
| - [ ] 19.5 | Record with ffmpeg on PATH | `.mp4` with audio in `Videos\Opera Grenouille` |
| - [ ] 19.6 | Windows client joins a Mac server, and the reverse | Full multiplayer works across platforms |

---

## 20. Performance

Use **Graphics** stats (fps, ms) on the presentation machine.

| # | Steps | Expected |
|---|---|---|
| - [ ] 20.1 | High quality, auditorium, wide balcony view | ≥ 30 fps (≥ 60 on Apple Silicon Pro/Max) |
| - [ ] 20.2 | **Spot every frog** + haze 1.0 | Still ≥ 30 fps on High, or drop to Medium |
| - [ ] 20.3 | Recording on High | Smooth recording; the file is still constant 30 fps |
| - [ ] 20.4 | Run for 20 minutes | fps steady; memory doesn't keep climbing (Activity Monitor) |
| - [ ] 20.5 | Low quality on a weak laptop | Playable |

---

## 21. Demo-day dress rehearsal

A 10-minute run that touches every headline feature, in presentation order.

- [ ] Run the section 1 smoke tests.
- [ ] Start the server on the host; one teammate joins from a second machine.
- [ ] Plaza: establishing shot, walk, time-of-day slider, enter the opera.
- [ ] Lighting: Agent 327 preset → haze up → **Spot every frog** → record Cue 1 → change look → Cue 2 → GO Cue 1 (crossfade).
- [ ] Musician (teammate): load a real MIDI, assign a part, **Play song**; play a few live notes.
- [ ] Singer: hold G and sing a line (headphones).
- [ ] Camera: rails with stage lock → drone strafe → **V** record 30 s → stop → save.
- [ ] Play the recorded video: picture, music, voice, no viewfinder.
- [ ] Graphics panel: show stats and toggle FXAA / tone mapping to explain post-processing.
- [ ] Have a backup: a pre-recorded video and screenshots in case the network fails.

---

## 22. Sign-off sheet

| Section | macOS | Windows | Tester | Date | Notes |
|---|---|---|---|---|---|
| 1 Smoke tests | | | | | |
| 2 Plaza | | | | | |
| 3 Auditorium / graphics | | | | | |
| 4 Roles | | | | | |
| 5 Live playing | | | | | |
| 6 MIDI songs | | | | | |
| 7 Audio songs | | | | | |
| 8 Singer / mic | | | | | |
| 9 Lighting | | | | | |
| 10 Followspots | | | | | |
| 11 Cues / presets | | | | | |
| 12 Rails | | | | | |
| 13 Free / Drone | | | | | |
| 14 Recording | | | | | |
| 15 Audience | | | | | |
| 16 Multiplayer (1 PC) | | | | | |
| 17 Multiplayer (2+ PCs) | | | | | |
| 18 Network errors | | | | | |
| 19 Windows | n/a | | | | |
| 20 Performance | | | | | |
| 21 Dress rehearsal | | | | | |
