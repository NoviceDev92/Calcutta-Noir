# Calcutta Noir (Kolkata: Last Exit)
**A Dark Apocalyptic First-Person Pseudo-3D Escape Racer in Qt & C++**

---

## Overview
**Calcutta Noir** (*Kolkata: Last Exit*) is a software-rasterized first-person survival escape racer built in pure C++ and Qt (no OpenGL, no hardware shaders). Powered by a **Mode-7 Scanline Inverse Perspective Ground Rasterizer** over `4096×4096` non-wrapping urban maps, **3D World-Space Smoke/Ember/Dust Particles**, **Dynamic Firelight & Animated Hooghly Water**, and **Seismic Collapse Physics**.

Features an animated 4-scene pixel-art intro cutscene, a continuous 3-sector escape from Gariahat to Howrah Bridge, interactive Checkpoint Pit-Stops for repairs and upgrades with Salvage Scrap (`₹`), and synchronized LAN convoy multiplayer.

---

## Controls
| Key | Action |
| :--- | :--- |
| **W / Up Arrow** | Accelerate |
| **S / Down Arrow** | Brake / Reverse |
| **A / D (or Left / Right)** | Steer |
| **Shift** | Handbrake Powerslide + Mini-Turbo |
| **Space** | Activate Nitrous Boost (or Skip Cutscene) |
| **E** | Use Tactical Gear (Dozer Ram / Shockwave Flare) |
| **P** | Open Checkpoint Pit-Stop (Repair & Upgrades) |
| **R / Shift+R** | Resume Saved Checkpoint / Fresh Stage Restart |
| **C** | Replay Intro Story Cutscene (from Title Menu) |

---

## Escape Stages (2 Checkpoints Each)
1. **Level 1 (Medium): Gariahat Collapse** — Gariahat Crossing, Rashbehari Ave tram rails, Golpark *(CP 1)*, Ballygunge Phari *(CP 2)*, Park Circus to AJC Bose ramp.
2. **Level 2 (Hard): Park Street & Maidan** — Park Street colonial ruins, Chowringhee & *The 42*, Esplanade Metro sinkholes *(CP 1)*, Victoria Memorial & Shaheed Minar *(CP 2)* to Prinsep Ghat.
3. **Level 3 (Insane): Howrah Bridge Collapse** — BBD Bagh (Writers' Building), Strand Road docks *(CP 1)*, narrow Howrah Bridge deck over the Hooghly River *(CP 2)* with snapping cantilever trusses to the West Bank Bunker.

---

## Build & Run Instructions
1. Open `RetroKart3D.pro` in **Qt Creator** (or run `qmake RetroKart3D.pro` + `mingw32-make release`).
2. Select the desktop kit (e.g., **Desktop Qt 6.11.1 MinGW 64-bit**).
3. Run `release/RetroKart3D.exe`.
