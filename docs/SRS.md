# Software Requirements Specification (SRS)
## Project Name: Ghost Victors (Geometry Dash Mod)
**Target Platform:** Windows / macOS / Android (Geometry Dash 2.2+ via Geode SDK)  
**Author:** Vikkie  
**Document Version:** 1.1  
**Status:** Approved Design Specification  

---

## 1. Introduction

### 1.1 Purpose
This document specifies the official software requirements for **Ghost Victors**, a Geometry Dash mod built on top of the **Geode SDK**. Designed by **Vikkie**, the mod enables players to race interactively alongside semi-transparent "ghost" representations of past victors in real-time while actively playing a level, record their own successful 100% runs, and share them to a centralized server.

### 1.2 Scope
* **In Scope:**
  * **Interactive Play-Along Mode:** Real-time synchronized ghost playback while the user actively controls their character.
  * **Dynamic Start-Line Opacity Fade-In:** Ghost remains invisible during the initial start (0%–3%) and smoothly fades into view (3%–5%) to eliminate start-line visual clutter.
  * **Telemetry Capture Engine:** Recording player motion vectors during valid 0%–100% normal mode attempts.
  * **Local Binary Serialization:** Local storage using compact `.gghost` binary files.
  * **Cloud Synchronization:** Asynchronous communication with a REST Web API to fetch replays ranked chronologically by upload date ("First Victors First").
  * **Icon-Only Ghost Rendering:** Native `PlayerObject` rendering with all particle emitters, wave trails, and motion streaks stripped away.
  * **Framerate Interpolation:** Linear position blending (`lerp`) for smooth playback on 60Hz to 240Hz+ displays.
  * **In-Game Interfaces:** Menu selection integrated into `LevelInfoLayer` and a mid-game toggle inside `PauseLayer`.
* **Out of Scope (Phase 1):**
  * Real-time networked P2P multiplayer (this system relies on asynchronous ghost replay streams).
  * Practice Mode / Custom StartPos recording.

### 1.3 Definitions and Acronyms
* **GD:** Geometry Dash (v2.2+)
* **SDK:** Software Development Kit (Geode SDK)
* **`PlayLayer`:** Primary Geometry Dash class managing gameplay physics and loop state.
* **`LevelInfoLayer`:** Geometry Dash layer displaying level metadata and play controls.
* **`PlayerObject`:** Cocos2d-x node class representing player icons and vehicles.
* **`lerp`:** Linear Interpolation, blending positions between recorded keyframes.
* **`.gghost`:** The custom binary replay file format designed for this mod.

---

## 2. Overall Description

### 2.1 Product Perspective & Gameplay Model
Ghost Victors is an **interactive Play-Along racing mod**. The user is in full control of their player character. As the user moves through the level, the selected victor's ghost advances simultaneously based on elapsed physics ticks.

```
 [ Player Controls Character ] ───(Active Inputs)───┐
                                                     ├──> [ PlayLayer Screen ]
 [ Ghost Telemetry Engine ]    ───(Tick-Synced Lerp)─┘
```

### 2.2 Design Constraints & Visual Cleanliness
1. **Zero Start-Line Clutter:** To prevent the ghost icon from obscuring the player's view at the start of a level (0%), the ghost remains hidden until the player passes 3% progress.
2. **Zero Particle Interference:** The ghost sprite must render strictly as an **Icon Only**. Wave trails, motion streaks, portal bursts, and vehicle boost particles are stripped to prevent sightline blockage in hard levels.

---

## 3. System Features & Functional Requirements

### 3.1 Play-Along & Dynamic Ghost Opacity Engine
* **FR-1.1 (Interactive Mode):** The player shall actively play the level while the ghost icon moves along the recorded path in real-time.
* **FR-1.2 (0%–3% Invisibility):** When level progress is between `0.0%` and `3.0%`, the ghost opacity shall be set to `0` (`setVisible(false)`).
* **FR-1.3 (3%–5% Smooth Fade-In):** When level progress is between `3.0%` and `5.0%`, the ghost opacity shall smoothly transition from `0` to `128` ($50\%$ transparency).
* **FR-1.4 (> 5% Full Opacity State):** When level progress exceeds `5.0%`, the ghost opacity shall lock at `128` ($50\%$ transparency).
* **FR-1.5 (Attempt Reset):** Upon player death or attempt restart, the ghost playback position and opacity state shall immediately reset to tick 0.

### 3.2 Telemetry Capture & Local Recording
* **FR-2.1:** Recording shall only activate during **Normal Mode** attempts starting from **0%**.
* **FR-2.2:** Telemetry shall sample physics state at ~60Hz keyframes (tick count, X/Y position, rotation angle, scale, and active gamemode).
* **FR-2.3:** Upon death or exit, temporary memory buffers shall be flushed.
* **FR-2.4:** Upon `PlayLayer::levelComplete()`, the buffer shall serialize to a local `.gghost` binary file.

### 3.3 Ghost Rendering & Particle Stripping
* **FR-3.1:** Ghosts shall be instantiated via `PlayerObject::create()` to retain native limb, vehicle, and rotation animations.
* **FR-3.2:** The system shall forcibly disable:
  * `m_waveTrail`
  * `m_regularTrail`
  * `m_shipBoostParticles` / `m_dragParticles` / `m_ufoParticles`
  * Ground dust and death explosion particles (`m_landParticles0`, `m_landParticles1`).
* **FR-3.3:** Keyframe playback shall calculate intermediate positions using linear interpolation (`lerp`).

### 3.4 Web API Communication & Server Ranking
* **FR-4.1:** The mod shall query `GET /api/v1/levels/{level_id}/victors` upon opening `LevelInfoLayer`.
* **FR-4.2:** Victors shall be returned and displayed in **chronological upload order** (the first player to upload a victor run for a level is ranked #1).
* **FR-4.3:** Downloading shall be asynchronous via `geode::utils::web::WebRequest` and cached locally to prevent duplicate requests.

---

## 4. Acceptance Criteria Matrix

The following criteria must be passed for a build to be verified as complete:

| Test ID | Feature | Acceptance Test Procedure | Expected Outcome |
| :--- | :--- | :--- | :--- |
| **AC-01** | **Interactive Play-Along** | Start a level with an active victor selected and make jump inputs. | Player character responds to inputs normally while the ghost icon moves independently along the victor's run path. |
| **AC-02** | **Start-Line Invisibility** | Enter a level and observe the ghost between 0% and 3% progress. | The ghost is completely invisible (`opacity = 0`) to allow 100% clear sightlines at the start. |
| **AC-03** | **3%–5% Smooth Fade-In** | Play past 3% up to 5% progress. | Ghost icon smoothly transitions from invisible to 50% opacity (`alpha: 0 -> 128`) without visual popping. |
| **AC-04** | **Particle & Trail Stripping** | Observe the ghost during Ship, Wave, and UFO modes. | Ghost renders **Icon Only**. No wave trails, motion streaks, or boost particles appear behind the ghost. |
| **AC-05** | **Upload-Order Ranking** | Open the Victors popup on a level with multiple uploaded runs. | The victor who uploaded their run first chronologically appears at position `#1` at the top of the list. |
| **AC-06** | **Attempt Reset Sync** | Die at 40% and click restart. | Player character and ghost character instantly reset to the start line together. |
| **AC-07** | **Offline Fallback** | Disconnect internet and open a level with a previously downloaded victor. | The cached `.gghost` file loads from disk, and the ghost renders normally without errors. |
| **AC-08** | **Mid-Game Pause Toggle** | Open `PauseLayer` mid-level and click "Hide Ghost". | The ghost icon immediately vanishes from gameplay until toggled back on. |

---

## 5. Non-Functional Requirements

### 5.1 Performance Requirements
* **NFR-1 (Framerate Overhead):** Rendering and updating the ghost player shall consume $< 2\%$ CPU execution time per tick, maintaining 144Hz/240Hz+ performance.
* **NFR-2 (Binary Storage Size):** A 2-minute level recording shall not exceed `250 KB` in binary `.gghost` format.
* **NFR-3 (Load Latency):** Loading and parsing a binary replay file from local cache shall complete in $< 10\text{ms}$.

---

## 6. Software Interface Mapping

```cpp
// Hook Mapping Overview for Vikkie's Ghost Victors Mod

LevelInfoLayer::init()  --> Injects "Victors" button & queries API (Ranked by upload date).
PlayLayer::init()       --> Loads cached .gghost file, spawns PlayerObject, strips particles/trails.
PlayLayer::update()     --> Handles lerp playback, updates 3%-5% dynamic opacity fade-in.
PlayLayer::resetLevel() --> Resets recording buffer & repositions ghost to tick 0.
PlayLayer::levelComplete() --> Serializes recording buffer to binary disk file & triggers upload.
PauseLayer::customSetup() --> Injects mid-game Ghost Visibility Toggle button.
```

---

**Author Sign-Off:**  
**Vikkie** — *Lead Mod Developer & Creator*