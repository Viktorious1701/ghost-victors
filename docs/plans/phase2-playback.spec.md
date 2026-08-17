# Phase 2 — Ghost playback · Spec

> Gate 1 artifact (spec). Reviewed & approved. The template-based implementation plan is derived from
> this at `docs/plans/phase2-playback.plan.md`.

**Status:** approved · **FR:** FR-1.1, FR-1.2, FR-1.3, FR-1.4, FR-1.5, FR-3.1, FR-3.2, FR-3.3 · **AC:** AC-01, AC-02, AC-03, AC-04, AC-06 · **NFR:** NFR-1, NFR-3 · **SDS:** §3.1, §3.2, §3.3, §5.1 · **Depends on:** Phase 0 + Phase 1 · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**Summary.** On entering a level, if a saved `.gghost` for it exists, load it and spawn an **icon-only**
ghost `PlayerObject` behind the player. Each frame, move the ghost by linear interpolation between
recorded keyframes at the player's current tick, and set its opacity via the 0–3–5 % start-line state
machine. Reset the ghost to tick 0 with the player on restart. Purely visual — the ghost never affects
gameplay physics.

---

## 1. Acceptance criteria

| ID | Procedure | Expected outcome |
|----|-----------|------------------|
| AC-01 | Enter a level that has a saved ghost; make jump inputs. | You control your own character normally while the ghost icon moves independently along the recorded path. |
| AC-02 | Observe the ghost between 0 % and 3 %. | Ghost fully invisible (`opacity 0`, not visible). |
| AC-03 | Play from 3 % to 5 %. | Ghost opacity smoothly ramps `0 → 128` (no pop-in). |
| AC-04 | Watch the ghost through ship / wave / UFO sections. | Icon-only: no trails, streaks, or boost/drag/land particles. |
| AC-06 | Die at ~40 % and restart. | Ghost **and** player reset to the start together (ghost tick 0, hidden). |
| P2-AC7 | Enter a level with **no** saved ghost. | No ghost spawns; gameplay unchanged, no errors. |
| P2-AC8 | Race the ghost at 60 fps and 240 fps. | Ghost stays smooth and frame-stable (NFR-1); no stutter/desync. |

---

## 2. What we will build

**In scope:**
- `src/GhostStripper.hpp` — disable/hide the ghost's `m_waveTrail`, `m_regularTrail`,
  `m_shipBoostParticles`, `m_dragParticles`, `m_ufoParticles`, `m_landParticles0/1` (⚠ verify names).
- `src/OpacityStateMachine.hpp` — opacity as a function of the player's completion %:
  `<3 % → 0/hidden`, `3–5 % → floor(128·(P−3)/2)`, `≥5 % → 128`.
- `src/InterpolationEngine.hpp` — binary-search the frame array by tick and lerp x / y / rotation
  between the two bracketing keyframes.
- `main.cpp` (PlayLayer `$modify`):
  - `init`: if `replays/<levelID>.gghost` loads OK, store frames in `GhostManager`, mark active, spawn
    the ghost `PlayerObject` (icons/colors from the header), strip it, add to `m_objectLayer` **behind**
    the player, start hidden.
  - `postUpdate`: if a ghost is active, drive opacity (by `getCurrentPercent()`) + lerp position (by the
    player's `dt`-accumulated tick). Recording capture from Phase 1 keeps running in the same hook.
  - `resetLevel`: snap the ghost to tick 0, hidden, opacity 0 (alongside the existing buffer reset).

**Out of scope (later phases):**
- Victor-selection UI + pause hide/show toggle → **Phase 3** (AC-08).
- Network fetch / offline / upload-order → **Phase 4** (AC-05, AC-07).
- Player 2 / dual / platformer ghost (D6).
- "Keep best run" — completion still **overwrites** the file (Phase 1 behavior).

---

## 3. Functional / non-functional requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-1.1 | Player plays live while the ghost replays in real time. | play-along |
| FR-1.2/1.3/1.4 | Opacity 0 (<3 %) → ramp 0–128 (3–5 %) → locked 128 (≥5 %). | `OpacityStateMachine.hpp` |
| FR-1.5 | Ghost resets to tick 0 on death/restart. | `resetLevel` (AC-06) |
| FR-3.1 | Ghost is a native `PlayerObject::create(...)`. | keeps limb/vehicle/rotation anims |
| FR-3.2 | Strip all particle emitters + trails. | `GhostStripper.hpp` (AC-04) |
| FR-3.3 | Position via linear interpolation between keyframes. | `InterpolationEngine.hpp` |
| NFR-1 | Ghost update < 2 % CPU/tick; smooth at 144/240 Hz. | lerp + binary search only |
| NFR-3 | Load/parse the `.gghost` < 10 ms at level entry. | reuse `ReplaySerializer` |

---

## 4. Decisions

- **DP1 — Auto-load own run.** At `init`, load `replays/<levelID>.gghost` (if present) as the active
  ghost. Race your last run; victor selection is Phase 3. (No ghost if the file is absent → P2-AC7.)
- **DP2 — Playback in `postUpdate`, same tick basis.** Drive the ghost from the player's
  `dt`-accumulated tick (× 240), identical to recording, so keyframes align. (D8 + Phase 1 bugfix.)
- **DP3 — Overwrite on completion.** Recording still overwrites the file each finish (Phase 1 behavior);
  you always race your most recent run.
- **DP4 — Ghost is visual-only.** Added to `m_objectLayer` behind the player and positioned manually
  each frame; it must not simulate physics or collide (⚠ ensure the created `PlayerObject` stays inert).
- **DP5 — Appearance from the header.** Ghost icon IDs + colors come from the loaded `ReplayHeader`.
- **DP8 — Leading-pacer ghost (supersedes "synchronized").** Playback drives the ghost at
  `playerTick + lead` where `lead` is a Geode mod setting `ghost-lead` (seconds, default 0.5). Because
  GD's X is time-locked, this renders the ghost *ahead / to the player's right* as a pacer to chase.
  Recording is unchanged (real positions at the true tick); only the display is offset. Lead 0 =
  synchronized overlap. **Deviation from SRS FR-1.1 / SDS §3.3 "synchronized"** — recorded here as the
  authoritative choice (added after in-game review).
- **DP9 — Ghost must show the recorded icon (bug fix).** The ghost renders the player's own icon/colors
  from the header (not a generic cube). A load-time diagnostic logs the header's icon IDs + colors to
  localize any mismatch (capture vs apply). Note: the ghost has no glow and 50 % opacity, which can make
  a correct icon look different.
- **DP11 — Capture gravity-flip + mini.** `FrameData.gameMode` is packed (bits 0-3 mode, bit4
  `m_isUpsideDown`, bit5 mini from `m_vehicleSize`); playback flips/scales the ghost accordingly. No
  format change (old files read the high bits as 0 = normal).
- **DP12 — X-progress-indexed playback (mirror fix).** The ghost is driven by the player's actual
  X-position + a lead (in frames), not by accumulated time — time-indexing drifted and caused the
  mirror-section acceleration/shake. Supersedes the tick-`lerp` sketch in SDS §3.3.
- ⚠ Binding names confirmed against local `gd.bro`: `PlayerObject::create(...)`, `m_particleSystems`,
  `toggle*Mode(enable, noEffects)`, `updatePlayer*Frame`, `setColor`/`setSecondColor`,
  `m_isUpsideDown`, `m_vehicleSize`.
