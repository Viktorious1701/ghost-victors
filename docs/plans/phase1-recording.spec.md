# Phase 1 — Recording · Spec

> Gate 1 artifact (spec). Reviewed & approved. The template-based implementation plan is derived from
> this at `docs/plans/phase1-recording.plan.md`.

**Status:** approved · **FR:** FR-2.1, FR-2.2, FR-2.3, FR-2.4 · **NFR:** NFR-2, NFR-3 · **SDS:** §2, §4.2.B, §5.1 · **Depends on:** Phase 0 (`DataTypes.hpp`, `GhostManager.hpp`) · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**Summary.** Capture the local player's motion during a valid run and, on completion, serialize it to a
`.gghost` binary file via a new `ReplaySerializer`. Recording is **position-only** (the visual-ghost
data); the format is kept forward-compatible so a future phase can add an input-event track for
click-pattern comparison. No playback/rendering, no networking yet.

---

## 1. Acceptance criteria (phase-level)

No SRS §4 gameplay AC tests recording directly (those are playback/UI/network). Phase 1 criteria:

| ID | Procedure | Expected outcome |
|----|-----------|------------------|
| P1-AC1 | Play a level start-to-finish (0→100%) in **Normal Mode**. | On `levelComplete`, a `.gghost` file for that level is written to the mod save dir. |
| P1-AC2 | Play in **Practice Mode**, and separately start from a non-0% point (StartPos/checkpoint). | **No** file written and **no** buffering — the gate rejects both. |
| P1-AC3 | Die or restart mid-run, then check state. | Recording buffer is cleared; no partial file; the next attempt starts fresh. |
| P1-AC4 | Reload the written file with `ReplaySerializer::loadFromFile`. | Magic `GGST` verifies, `formatVersion==1`, `totalFrames == frames.size()`; parse completes in <10 ms (NFR-3). |
| P1-AC5 | Record a ~2-minute run and check file size. | ≤ 250 KB (NFR-2). |
| P1-AC6 | Record the same section at 60 fps and at 240 fps. | Comparable frame counts and identical step timestamps — capture is **framerate-independent** (per-step cadence, not per-frame). |

---

## 2. What we will build

**In scope:**
- `src/ReplaySerializer.hpp` — `saveToFile()` / `loadFromFile()` binary IO with magic + version verify
  (SDS §4.2.B), guarding against short/truncated reads.
- Recording logic in the `PlayLayer` `$modify` hook:
  - **Gate:** only **Normal Mode**, only runs **from 0%** (`!m_isPracticeMode && m_isFrom0` — verify
    exact binding names).
  - **Capture:** append a `FrameData` (step, x, y, rotation, gameMode) at a fixed **~60 Hz cadence
    keyed to the physics step counter**, into `GhostManager`'s recording buffer.
  - **Reset:** clear the buffer on `resetLevel` (death/restart).
  - **Complete:** on `levelComplete`, populate a `ReplayHeader` (levelID, victor/player name, the
    player's 8 icon IDs + 2 colors, totalFrames) and serialize buffer → `.gghost`.
- Save-path helper: deterministic filename per level (e.g. `{levelID}.gghost`) in the mod's save dir.

**Out of scope (later phases):**
- **Input-event capture** (`(step, button, isPress)` via `handleButton`) — deferred to a dedicated
  future "input track" phase; format is reserved for it now (see D3).
- Playback / ghost rendering (Phase 2), UI (Phase 3), upload/network (Phase 4).
- **Player 2 / dual / platformer** capture — Player 1 only for now (D6).

---

## 3. Functional / non-functional requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-2.1 | Record only Normal Mode, from 0%. | The capture gate. |
| FR-2.2 | Sample step, X/Y, rotation, gamemode. | Position stream (no scale — Phase 0 D2; no click events this phase — D3). |
| FR-2.3 | Flush temp buffer on death/exit. | Cleared in `resetLevel`. |
| FR-2.4 | Serialize to `.gghost` on `levelComplete`. | Via `ReplaySerializer::saveToFile`. |
| NFR-2 | ≤ 250 KB / 2-min run. | ~60 Hz × 120 s × 17 B ≈ 122 KB. |
| NFR-3 | Parse from cache < 10 ms. | Single header read + one bulk frame read. |

---

## 4. Decisions

- **D3 — Position-only now, input-ready format.** Record only position this phase. Keep `.gghost`
  forward-compatible for a future input-event track via `formatVersion` dispatch (v1 = header + frame
  array; a future v2 appends an input block after the frames). Exact reservation mechanism decided in
  the gate-2 plan. (User: option A; click-comparison definitely planned but built later — and cannot be
  backfilled onto v1 runs.)
- **D4 — Per-step ~60 Hz cadence.** Capture every 4th physics step (240/4), keyed to the step counter,
  **not** per rendered frame → framerate-independent and within NFR-2. (Full 240 Hz ≈ 488 KB/2 min
  would break NFR-2.)
- **D5 — Correct step field.** Use `GJBaseGameLayer::m_currentStep` / `m_gameState.m_unkUint2` (verify
  against local Geode 5.8.2 bindings). Update `CLAUDE.md`/SDS wording that says `m_currentTick`.
- **D6 — Player 1 only.** Dual/platformer P2 recording deferred; documented limitation.
- **Note:** because click-comparison is definitely planned and un-backfillable, schedule the
  input-track phase before relying on a library of recorded runs.
