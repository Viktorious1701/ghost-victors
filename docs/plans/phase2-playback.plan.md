# Phase 2: Ghost playback

> **FR:** FR-1.1…1.5, FR-3.1…3.3 · **AC:** AC-01, AC-02, AC-03, AC-04, AC-06 (+P2-AC7/8) · **NFR:** NFR-1, NFR-3 · **Hooks:** `PlayLayer::init`, `PlayLayer::postUpdate`, `PlayLayer::resetLevel` · **SDS:** §3.1–3.3, §5.1 · **Status:** ✅ done — build + in-game green · **Branch/Commit:** n/a
> **Spec:** `docs/plans/phase2-playback.spec.md` · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**Delivers** an icon-only ghost `PlayerObject` that replays the level's saved `.gghost` alongside the
live player — lerped position/rotation, mode-switched vehicle, 0–3–5 % opacity fade, reset-synced.

## 1. Acceptance criteria
AC-01 play-along · AC-02 invisible <3 % · AC-03 fade 3–5 % · AC-04 icon-only in ship/wave/UFO ·
AC-06 reset sync · P2-AC7 no file → no ghost · P2-AC8 frame-stable 60/240 fps.

## 2. What we build
Three header-only engine helpers + ghost load/spawn/drive/reset in the `PlayLayer` `$modify` (§5).

## 3. FR / NFR
FR-1.1 play-along · FR-1.2/1.3/1.4 opacity SM · FR-1.5 reset · FR-3.1 native PlayerObject · FR-3.2 strip ·
FR-3.3 lerp · NFR-1 <2 %/tick · NFR-3 load <10 ms.

## 4. Gaps & Decisions (Resolved)

| ID | Area | Decision |
|----|------|----------|
| DP1 | Scope | Auto-load `replays/<levelID>.gghost` at `init`; no file → no ghost (P2-AC7). Victor UI = Phase 3. |
| DP2 | Timing | Drive playback in `postUpdate` off the player's `dt`-accumulated tick (×240) — same basis as recording. |
| DP3 | Scope | Recording still overwrites on completion (Phase 1). |
| DP4 | Rendering | Ghost visual-only: `PlayerObject::create(..., playLayer=false)`, added to `m_objectLayer` behind the player, positioned manually; never `m_player1/2`, so the game never simulates it. |
| DP5 | Rendering | Icons (all 8) + both colors from the loaded `ReplayHeader`. |
| DP6 | Rendering | Strip via `m_particleSystems` (stopSystem+hide each) + hide `m_regularTrail`/`m_shipStreak`/`m_waveTrail`, **every frame**. `setCascadeOpacityEnabled(true)` so opacity reaches child sprites. |
| DP7 | Rendering | Switch vehicle to `frame.gameMode` via `toggle*Mode(enable, noEffects=true)`; on reset revert to cube. **Fallback** if transitions misbehave at gate 3: keep ghost as cube, still lerp position/rotation. |
| DP8 | Playback | **Leading-pacer:** drive ghost at `playerTick + lead`; `lead` = `ghost-lead` mod setting (s, default 0.5) × 240. Renders ghost ahead/right (GD X is time-locked). Recording unchanged; lead 0 = overlap. Supersedes SRS FR-1.1 / SDS §3.3 "synchronized" (post-review). |
| DP9 | Rendering | Ghost must show the recorded player icon/colors (bug). Root cause: `create()`'s first arg is the player slot, not the cube icon — fixed to `create(1,1,…)` + explicit `updatePlayer*Frame`. Ghost has no glow + 50% opacity so a correct icon can still look "off". |
| DP10 | Lifecycle | Load/spawn the ghost from **both** `init` and `resetLevel` (idempotent) — GD's Retry calls `resetLevel`, not `init`, so a run recorded this session must spawn on the next attempt without exit/re-enter. Skip the read (clean info, not ERROR) when no file exists. |
| DP11 | Data/render | Capture **gravity-flip** (`m_isUpsideDown`) + **mini** (`m_vehicleSize<0.9`) packed into the spare high bits of `FrameData.gameMode` (bit4 flip, bit5 mini) — no format/size change, old files still load. Playback applies via `setScaleY(±size)` + `setScaleX(size)`. |
| DP12 | Playback | Drive the ghost by the player's **X-progress** (`getPositionX()` + lead-in-frames), not the accumulated-time tick. Time-indexing drifted → acceleration/inconsistent-lead/shake, worst under mirror (X negated). X-indexing is drift-free and mirror-safe. Assumes X monotonic (standard levels). |
| DP13 | Playback | **Mirror-transition lead damping.** GD mirror animates a horizontal flip of `m_objectLayer` (scaleX +1→0→−1); a leading ghost is off the flip pivot and swings ~2×lead off-screen mid-flip. Multiply lead by `min(1,\|m_objectLayer->getScaleX()\|)` so it collapses to ~0 through the flip (ghost rides the pivot) and returns when stable. Fallback if the scale isn't on `m_objectLayer`: read the ghost's cumulative world X-scale. |

## 5. Source-tree layout

```text
src/
├── GhostStripper.hpp        # (new) stripGhostVisuals — streaks + m_particleSystems
├── OpacityStateMachine.hpp  # (new) applyGhostOpacity(ghost, percent)
├── InterpolationEngine.hpp  # (new) updateGhostLerp(ghost, frames, tick) -> gameMode
├── main.cpp                 # (mod) load+spawn (init), drive (postUpdate), reset (resetLevel)
├── GhostManager.hpp / ReplaySerializer.hpp / DataTypes.hpp  # (reused)
```

## 6. Module responsibilities
Stripper (kill trails + all particle systems, per-frame) · Opacity SM (percent → visible/opacity) ·
Interp engine (binary-search + lerp, returns active gameMode) · PlayLayer hook (load/spawn/drive/reset).

## 7. Data / engine detail
- **Load (init):** `ReplaySerializer::loadFromFile(saveDir/replays/<levelID>.gghost, header, GhostManager::getLoadedFrames())`; success → `setActiveVictor("local")` + spawn; else inactive (P2-AC7).
- **Spawn:** `PlayerObject::create(header.cubeID, header.shipID, this, m_objectLayer, false)`; `addChild(ghost, -1)`; set all 8 icon frames + `setColor`/`setSecondColor` from header; `setCascadeOpacityEnabled(true)`; `stripGhostVisuals`; hidden + opacity 0.
- **Drive (postUpdate):** `tick = int(m_playTime*240)`; `mode = updateGhostLerp(...)`; if mode changed → `toggle*Mode(disable old / enable new, noEffects=true)`; `stripGhostVisuals`; `applyGhostOpacity(getCurrentPercent())`. Gated by `GhostManager::isGhostVisibleInPause()` (default true; Phase 3 toggle).
- **Reset (resetLevel):** revert vehicle to cube, position at frame 0, opacity 0, hidden.
- **Lerp:** `lower_bound` on `frame.tick`; `t=(tick−A.tick)/(B.tick−A.tick)`; clamp at both ends.

## 8. Rules & invariants
Opacity 0/ramp/128 (FR-1.2–1.4, `OpacityStateMachine.hpp`) · no trails/particles ever (FR-3.2/AC-04,
`GhostStripper.hpp` per frame) · ghost never affects gameplay (DP4) · reset to tick 0 with player
(FR-1.5/AC-06) · <2 %/tick (NFR-1, binary search + O(1) lerp).

## 9. Per-tick / lifecycle flow

```mermaid
sequenceDiagram
    actor Player
    participant PL as PlayLayer hook
    participant Ser as ReplaySerializer
    participant Ghost as ghost PlayerObject
    participant Eng as engine helpers

    Player->>PL: init(level)
    PL->>Ser: loadFromFile(replays/<id>.gghost)
    alt present & valid
        PL->>Ghost: create(...,false) + icons/colors
        PL->>Eng: strip; hidden; m_ghostActive=true
    else absent
        Note over PL: no ghost (P2-AC7)
    end
    loop postUpdate(dt)
        alt m_ghostActive
            PL->>Eng: mode = updateGhostLerp(ghost, frames, tick)
            opt mode changed
                PL->>Ghost: toggle*Mode(noEffects=true) + icon
            end
            PL->>Eng: strip; applyGhostOpacity(percent)
        end
    end
    Player->>PL: resetLevel()
    PL->>Ghost: cube, frame0, opacity 0, hidden
```

## 10. Convention compliance
`$modify` base-first ✅ · ghost ptr + mode/active flags in `Fields` ✅ · header-only helpers ✅ · behind
player ✅ · NFR-1 ✅.

## 11. Tasks (checklist)

| ID | Task | File |
|----|------|------|
| T01 | `GhostStripper.hpp` | `src/GhostStripper.hpp` |
| T02 | `OpacityStateMachine.hpp` | `src/OpacityStateMachine.hpp` |
| T03 | `InterpolationEngine.hpp` | `src/InterpolationEngine.hpp` |
| T04 | Load + activate in `init` (P2-AC7 guard) | `src/main.cpp` |
| T05 | Spawn + configure ghost | `src/main.cpp` |
| T06 | postUpdate drive (lerp + mode + strip + opacity) | `src/main.cpp` |
| T07 | resetLevel ghost reset | `src/main.cpp` |
| T08 | `geode build` green | — |

- [x] T01 [x] T02 [x] T03 [x] T04 [x] T05 [x] T06 [x] T07 [x] T08 *(build green on user's machine, VS 2022 / SDK 5.9.0)*

## 12. Definition of Done (gate)
**Universal:**
- [x] build green · loads, no crash · no regression · struct `static_assert`s pass · §11 ticked + §14 filled · NFR-1 met.

**Phase-specific (verified in-game):**
- [x] AC-01 — ghost races the recorded path while the player controls their own char.
- [x] AC-02 / AC-03 — invisible <3 %, smooth fade 3–5 %.
- [x] AC-04 — icon-only (particles/trails stripped) through ship/wave/UFO.
- [x] AC-06 — die + restart resets ghost with the player.
- [x] P2-AC7 — level with no `.gghost` → no ghost, no error.
- [x] P2-AC8 — frame-stable; X-progress indexing removed the drift/shake (mirror stabilized via DP13).

## 13. Verification
- **Build:** ✅ green on the user's machine (VS 2022 / MSVC, Geode SDK 5.9.0).
- **In-game (GD 2.2081):** ✅ ghost loads (`ghost loaded — 4789 frames, victor 'vik17'`), invisible→fade
  start-line, races the path, icon-only; gravity flips + mini shrink (DP11); leading pacer holds a
  steady position with no drift (DP12); mirror handled (DP13 damping in place — residual "ghost enters
  the portal ahead of you" is expected pacer behavior, accepted by the user). Header log confirms the
  recorded icon/colors are applied (`cube=267 … c1=(255,0,125)`), so the DP9 icon fix is verified.
- **Reload:** save→reload self-verify passes (`Verify reloaded OK — GGST, v1, 4776 frames`).

## 14. Dev Agent Record — File List
| File | C/M | Role |
|------|-----|------|
| `src/GhostStripper.hpp` | Created | strip trails + all `m_particleSystems` (icon-only racing ghost) |
| `src/OpacityStateMachine.hpp` | Created | 0/3/5 % start-line opacity fade |
| `src/InterpolationEngine.hpp` | Created | X-progress lookup + lerp (DP12) |
| `src/main.cpp` | Modified | load/spawn/configure/drive/reset ghost; packed flip+mini; lead + mirror damping |
| `src/DataTypes.hpp` | Modified | packed `gameMode` byte (flip bit4 / mini bit5) + flag macros (DP11) |
| `mod.json` | Modified | `ghost-lead` setting (pacer lead, seconds) |
| `CLAUDE.md` / `docs/SDS.md` | Modified | X-indexed playback + packed-gameMode notes |

**Not in scope:** pause toggle + victor UI (Phase 3), network/offline (Phase 4), P2/dual, keep-best,
Replay/Spectate & overlay (future Phase 5).
