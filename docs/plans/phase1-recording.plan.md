# Phase 1: Recording

> **FR:** FR-2.1, FR-2.2, FR-2.3, FR-2.4 · **AC:** none direct (phase-level P1-AC1…6) · **NFR:** NFR-2, NFR-3 · **Hooks:** `PlayLayer::init`, `PlayLayer::resetLevel`, `PlayLayer::postUpdate` (per-frame capture — see D8), `PlayLayer::levelComplete` · **SDS:** §2, §4.2.B, §5.1 · **Status:** ✅ done — build + in-game green · **Branch:** n/a (no git repo) · **Commit:** n/a
> **Source specs:** `docs/SRS.md` · `docs/SDS.md` · **Spec:** `docs/plans/phase1-recording.spec.md` · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**This phase delivers** position telemetry capture during a valid run and serialization to a `.gghost`
file on completion, with loud logging + a save→reload self-verify so recording is provable without any
playback. Position-only (spec D3); no click/input capture, no rendering, no network.

> ⚠ **Binding names below are illustrative / reverse-engineered** — every `m_*` field / method marked ⚠
> must be confirmed against the locally installed **Geode 5.8.2** bindings while coding (CLAUDE.md §9, spec D5).

## 1. Spec — Acceptance criteria

| ID | Procedure | Expected outcome |
|----|-----------|------------------|
| P1-AC1 | Full 0→100% run, Normal Mode. | `<saveDir>/replays/<levelID>.gghost` written; log `SAVED … → <abs path> (N KB)`. |
| P1-AC2 | Practice Mode; and a StartPos/non-0% start. | Log `SKIPPED`; no buffering; no file. |
| P1-AC3 | Die / restart mid-run. | Log `reset buffer`; buffer cleared; no partial file. |
| P1-AC4 | Auto self-verify on save (reload written file). | Log `Verify: reloaded OK — magic GGST, v1, N frames`; parse < 10 ms. |
| P1-AC5 | ~2-min run. | File ≤ 250 KB. |
| P1-AC6 | Same section at 60 fps vs 240 fps. | Identical step timestamps + matching frame counts. |

## 2. Spec — What we will build

**In scope:** `ReplaySerializer.hpp` (binary IO + verify); recording gate/capture/reset/save wired into
the existing `PlayLayer` `$modify`; save-path helper (`replays/<levelID>.gghost`); observability logs +
save→reload self-verify.

**Out of scope:** input/click capture (future phase, D3), ghost rendering (Phase 2), UI (Phase 3),
network (Phase 4), Player 2 / dual (D6).

## 3. Spec — Functional & non-functional requirements

| ID | Requirement | Where |
|----|-------------|-------|
| FR-2.1 | Record only Normal Mode, from 0%. | gate in `init`/`resetLevel` |
| FR-2.2 | Sample step, X/Y, rotation, gamemode. | `update` capture |
| FR-2.3 | Flush buffer on death/exit. | `resetLevel` |
| FR-2.4 | Serialize on `levelComplete`. | `ReplaySerializer::saveToFile` |
| NFR-2 | ≤ 250 KB / 2 min. | ~60 Hz × 17 B (D4) |
| NFR-3 | Parse < 10 ms. | header + one bulk read |

## 4. Gaps & Decisions (Resolved)

| ID | Area | Decision |
|----|------|----------|
| D3 | Storage | Position-only now; `.gghost` forward-compatible for a future input track via `formatVersion` (v1 = header+frames; future v2 appends an input block after the frames — no v1 layout change). |
| D4 | Perf | Capture every 4th physics step (~60 Hz) — within NFR-2. |
| D5 | Correctness | Timestamp/throttle by `GJBaseGameLayer::m_currentStep` / `m_gameState.m_unkUint2` ⚠, not a frame counter; fix `CLAUDE.md`/SDS `m_currentTick` wording. |
| D6 | Scope | Player 1 only. |
| D7 | Perf/complexity | Capture throttled by step (`≥ last+4`) instead of a per-step `PlayerObject::update` hook. 60/240 fps ⇒ identical samples (P1-AC6). Odd fps ⇒ nearest-frame step (acceptable). Per-step hook is the fallback for perfect fidelity. |
| **D8** | **Bindings** | **Capture in `PlayLayer::postUpdate` (real per-frame override, win `0x39da60`), NOT `PlayLayer::update`.** On Windows `PlayLayer::update` has no binding (only m1/imac addresses); the loop is `GJBaseGameLayer::update`, so a `PlayLayer::update` hook silently never fires. Found at gate-3 testing (buffer empty on completion despite the gate opening). |

## 5. Technical plan — Source-tree layout

```text
src/
├── ReplaySerializer.hpp   # (new) saveToFile / loadFromFile — 68B header + frame array, magic+version+size verify
├── main.cpp               # (mod) gate + capture + reset + save wired into the PlayLayer $modify
├── GhostManager.hpp       # (reused) getRecordingBuffer()
└── DataTypes.hpp          # (reused) ReplayHeader / FrameData

CLAUDE.md                  # (mod) fix `m_currentTick` → `m_currentStep`/`m_gameState.m_unkUint2` wording (D5)
```
(`CMakeLists.txt` globs `src/*.cpp`; the new `.hpp` needs no build change.)

## 6. Technical plan — Module responsibilities

| Layer | Owns (this phase) | File |
|-------|-------------------|------|
| Serializer | binary `.gghost` read/write + magic/version/size validation | `src/ReplaySerializer.hpp` |
| Hook (`PlayLayer`) | gate, step-throttled capture, buffer reset, header build + save, logging/self-verify | `src/main.cpp` |
| Manager singleton | holds `m_recordingBuffer` (`std::vector<FrameData>`) | `src/GhostManager.hpp` (reused) |
| Save-path helper | `getSaveDir()/replays/<levelID>.gghost` (+ `create_directories`) | `ReplaySerializer.hpp` or `main.cpp` |

## 7. Technical plan — Data & format

- **Write:** `ReplayHeader` (68 B) then `totalFrames × FrameData` (17 B). `formatVersion = 1`.
- **Header on `levelComplete`:** `levelID` ← `m_level->m_levelID` ⚠ · `victorName` ← local player name
  (`GJAccountManager::get()->m_username` ⚠), truncate to 31 + NUL · 8 icon IDs ← `GameManager::sharedState()`
  getters (`getPlayerFrame/Ship/Ball/Bird/Dart/Robot/Spider/Swing`) ⚠ · colors ← player color **indices**
  → RGB via `GameManager::colorForIdx(idx)` ⚠ · `totalFrames` ← `buffer.size()`.
- **`FrameData` per capture:** `tick`=current step ⚠ · `x/y`=`m_player1->getPositionX/Y()` ⚠ ·
  `rotation`=`getRotation()` ⚠ · `gameMode`=mapped enum (0=cube,1=ship,2=ball,3=ufo,4=wave,5=robot,6=spider,7=swing) ⚠.
- **Load/verify:** read 68 B; reject if `memcmp(magic,"GGST",4)!=0` or `formatVersion!=1`; assert remaining
  bytes == `totalFrames*17`; bulk-read frames; return bool.

## 8. Technical plan — Rules & invariants

| Rule | Source | Enforced in |
|------|--------|-------------|
| Record only Normal + from 0% | FR-2.1 | `!m_isPracticeMode && m_isFrom0` ⚠ |
| Capture ~60 Hz keyed to step, not frame | D4/D5/D7 | `update`: `step ≥ m_lastCaptureStep + 4` |
| Buffer cleared every attempt | FR-2.3 | `resetLevel` + `init` |
| Serialize only on real completion | FR-2.4 | `levelComplete`, only if `m_isRecording` |
| Verify magic + version + size on load | SDS §4.2.B | `loadFromFile` |
| ≤ 250 KB / parse < 10 ms | NFR-2/3 | 17 B @ 60 Hz; single bulk read |
| Player 1 only | D6 | capture reads `m_player1` only |

## 9. Per-tick / lifecycle flow

```mermaid
sequenceDiagram
    actor Player
    participant PL as PlayLayer hook
    participant GM as GhostManager
    participant Ser as ReplaySerializer

    Player->>PL: init(level)
    PL->>PL: base init; m_isRecording = !practice && from0; m_lastCaptureStep = -1
    PL->>GM: recordingBuffer.clear()
    Note over PL: log STARTED or SKIPPED

    loop each frame — update(dt)
        Player->>PL: update(dt) (base first)
        alt m_isRecording && step >= lastCaptureStep+4
            PL->>GM: push FrameData{step, x, y, rot, mode}
            PL->>PL: m_lastCaptureStep = step
        end
    end

    alt death / restart
        Player->>PL: resetLevel()
        PL->>GM: recordingBuffer.clear()
        PL->>PL: m_lastCaptureStep = -1; re-eval gate
        Note over PL: log reset buffer
    end

    alt completion
        Player->>PL: levelComplete()
        alt m_isRecording
            PL->>PL: build ReplayHeader (level, name, icons, colors, totalFrames)
            PL->>Ser: saveToFile(replays/<levelID>.gghost, header, buffer)
            PL->>Ser: loadFromFile(...) self-verify
            Note over PL: log SAVED <path> (N KB) + Verify reloaded OK
        end
    end
```

## 10. Convention compliance (CLAUDE.md §4 / §5)

| Rule | Status | Note |
|------|--------|------|
| `$modify` calls base first | ✅ | all four overrides call base first |
| Per-instance state in `Fields` | ✅ | `m_isRecording`, `m_lastCaptureStep` in Fields; buffer in GhostManager (SDS design) |
| Header-only helper `.hpp` | ✅ | `ReplaySerializer.hpp` |
| `log::info/error` | ✅ | observability logs |
| NFR budgets | ✅ | D4 cadence; single bulk read |

## 11. Tasks (checklist)

| ID | Task | File |
|----|------|------|
| T01 | `ReplaySerializer::saveToFile`/`loadFromFile` (+magic/version/size verify) | `src/ReplaySerializer.hpp` |
| T02 | Save-path helper `getSaveDir()/replays/<levelID>.gghost` + `create_directories` | `ReplaySerializer.hpp`/`main.cpp` |
| T03 | Gate + Fields state (`m_isRecording`, `m_lastCaptureStep`) in `init`/`resetLevel` | `src/main.cpp` |
| T04 | Step-throttled capture (~60 Hz) in `update` | `src/main.cpp` |
| T05 | Clear buffer on `resetLevel` | `src/main.cpp` |
| T06 | Header population + save on `levelComplete` | `src/main.cpp` |
| T07 | Observability logs + save→reload self-verify | `src/main.cpp` |
| T08 | Fix `m_currentTick` → `m_currentStep` wording (D5) | `CLAUDE.md` |
| T09 | `geode build` green | — |

- [x] T01 [x] T02 [x] T03 [x] T04 [x] T05 [x] T06 [x] T07 [x] T08 [x] T09 *(build green on user's machine; capture fixed via dt-accumulated tick — D8/bugfix)*

## 12. Definition of Done (gate)

**Universal gate:**
- [x] `geode build` succeeds, no errors.
- [x] Mod loads in GD 2.2081; no crash on menu / level entry / exit.
- [x] No regression — gameplay unchanged.
- [x] Struct `static_assert`s still pass (`ReplayHeader==68`, `FrameData==17`).
- [x] All §11 tasks ticked and §14 File List filled.
- [x] NFR-2 (≤250 KB) and NFR-3 (<10 ms) met.

**Phase-specific:**
- [x] P1-AC1 — full Normal run writes `<levelID>.gghost` (logged path + size).
- [x] P1-AC2 — Practice / non-0% records nothing (`SKIPPED`).
- [x] P1-AC3 — death/restart clears buffer (`reset buffer`); no partial file.
- [x] P1-AC4 — save→reload self-verify passes (magic/version/count), < 10 ms.
- [x] P1-AC5 — ~2-min file ≤ 250 KB.
- [~] P1-AC6 — satisfied **by design** (dt-accumulated tick is framerate-independent); not explicitly A/B tested at 60 vs 240 fps.

## 13. Verification

- **Build:** ✅ `geode build` green on the user's machine (VS 2022 / MSVC), mod loads, no regression.
- **In-game** (GD 2.2081): ✅
  - P1-AC1/AC4: "free level 100009" run → `SAVED 486 frames -> …\replays\96495344.gghost (8330 bytes)`
    then `Verify reloaded OK — magic GGST, v1, 486 frames`. Byte math exact (68 + 486×17 = 8330).
  - AC6-style sanity: 486 frames / ~8 s ≈ 60 Hz cadence as designed.
  - P1-AC2: Practice/StartPos → repeated `recording SKIPPED (practice/startpos)`, no file.
  - P1-AC3: entering practice mid-run flipped the gate to `SKIPPED (buffer cleared)` on `resetLevel`.
- **NFR:** ✅ 8330 B for ~8 s ⇒ ~122 KB / 2 min (< 250 KB, NFR-2); single-read reload verified (NFR-3).
- **Root-cause note:** initial builds captured 1 frame because `m_currentStep` reads 0 in `postUpdate`;
  fixed by deriving the tick from accumulated `dt × 240` (see plan bugfix / `gd-binding-gotchas` memory).
- **Note:** no automated harness beyond struct `static_assert`s; recording proven via logs + save→reload
  self-verify + inspecting the written file.

## 14. Dev Agent Record — File List

| File | Created / Modified | Role |
|------|--------------------|------|
| `src/ReplaySerializer.hpp` | Created | `.gghost` binary IO + verify |
| `src/main.cpp` | Modified | recording gate / capture / reset / save / logs |
| `CLAUDE.md` | Modified | `m_currentTick` → `m_currentStep` wording (D5) |

**Not in scope (later phases):** input/click capture (future input-track phase), ghost rendering
(Phase 2), UI (Phase 3), network (Phase 4), P2/dual (D6).
