# Phase 0 — Data & state · Spec

> Gate 1 artifact (spec). Reviewed & approved. The template-based implementation plan is derived from
> this at `docs/plans/phase0-data-state.plan.md`.

**Status:** approved · **FR:** FR-2.2, FR-2.4 (data model only) · **NFR:** NFR-2 · **SDS:** §2.1, §2.2, §4.2.A · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**Summary.** Define the on-disk/in-memory data model for the mod: the packed `.gghost` binary structs
and the global `GhostManager` singleton that holds active-victor state, loaded ghost frames, the
recording buffer, and the pause-visibility flag. No hooks, no rendering, no I/O yet — just the types
everything else builds on.

---

## 1. Acceptance criteria (phase-level)

No SRS §4 gameplay AC maps to Phase 0 (those are behavioral — phases 2–4). Phase 0's criteria are
foundational and derive from the Definition-of-Done gate:

| ID | Criterion | How verified |
|----|-----------|--------------|
| P0-AC1 | `sizeof(ReplayHeader) == 68` (per D1) and `sizeof(FrameData) == 17`. | Compile-time `static_assert` in `DataTypes.hpp`; build fails if wrong. |
| P0-AC2 | `GhostManager::get()` returns a usable singleton; its accessors compile and link. | Referenced from `main.cpp`; `geode build` succeeds. |
| P0-AC3 | Mod builds and loads in GD 2.2081 with **no in-game behavior change**. | Launch GD; mod loads, existing log lines still print, gameplay unchanged. |

---

## 2. What we will build

**In scope:**

- `src/DataTypes.hpp` — `#pragma pack(push, 1)` `ReplayHeader` (magic `GGST`, `formatVersion`,
  `levelID`, `victorName[32]`, 8 icon IDs, 2 RGB colors, `totalFrames`) and `FrameData`
  (`tick`, `x`, `y`, `rotation`, `gameMode`), plus the two `static_assert`s.
- `src/GhostManager.hpp` — singleton per SDS §4.2.A: `m_activeVictorID` (`std::optional<std::string>`),
  `m_loadedGhostFrames`, `m_recordingBuffer` (`std::vector<FrameData>`), `m_isGhostVisibleInPause`, and
  accessors (`setActiveVictor` / `clearActiveVictor` / `hasActiveVictor` / `getLoadedFrames` /
  `getRecordingBuffer` / `isGhostVisibleInPause` / `setGhostVisibleInPause`).
- Minimal `#include` of both headers in `src/main.cpp` so they are actually compiled (header-only files
  aren't picked up by the `src/*.cpp` glob) and the `static_assert`s fire.

**Out of scope (later phases):** telemetry capture, serialization / file I/O (`ReplaySerializer` →
Phase 1), rendering / hooks logic, networking. `main.cpp` hook bodies stay as their current stubs.

---

## 3. Functional / non-functional requirements

| ID | Requirement | Notes for Phase 0 |
|----|-------------|-------------------|
| FR-2.2 | Telemetry fields: tick, X/Y, rotation, gamemode (scale → D2, dropped). | Defines `FrameData` shape only; capture is Phase 1. |
| FR-2.4 | Completion serializes to a `.gghost` binary file. | Phase 0 defines the header/frame layout the serializer will write. |
| NFR-2 | ≤ 250 KB per 2-min recording. | Guaranteed by the 17-byte `FrameData` (~60 Hz × 120 s × 17 B ≈ 122 KB). |

---

## 4. Decisions

- **D1 — `ReplayHeader` = 68 bytes (not 64).** SDS §2.1's diagram claims 64 B with a "12 B Icon
  Config", but §2.2's struct declares **8** `uint16_t` icon IDs (16 B). Real packed total: `4 (magic)
  + 2 (version) + 4 (levelID) + 32 (victorName) + 16 (8 icons) + 6 (colors) + 4 (totalFrames) = 68`.
  GD 2.2 has 8 gamemodes, so all 8 icon IDs are kept and the SDS "64" is treated as a documentation
  error. The `static_assert` target and the "64 bytes" references in `CLAUDE.md` §4/§8 and
  `_TEMPLATE.plan.md` §12 are corrected to **68** during implementation.
- **D2 — `FrameData` = 17 bytes, no `scale`.** SRS FR-2.2 lists "scale" among sampled fields, but SDS
  §2.2's `FrameData` has no scale field. We follow SDS §2.2 (tick u32, x/y/rotation f32, gameMode u8 =
  17 B); the 17-byte size is load-bearing for NFR-2 and the DoD. Scale is dropped from the on-disk
  format.
