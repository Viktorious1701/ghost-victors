# Phase 0: Data & state

> **FR:** FR-2.2, FR-2.4 · **AC:** none (SRS ACs are gameplay — phases 2–4; phase-level P0-AC1…3 below) · **NFR:** NFR-2 · **Hooks:** none (only a compile-anchor `#include` in `main.cpp`) · **SDS:** §2.1, §2.2, §4.2.A · **Status:** in-progress · **Branch:** n/a (no git repo) · **Commit:** n/a
> **Source specs:** `docs/SRS.md` · `docs/SDS.md` · **Spec:** `docs/plans/phase0-data-state.spec.md` · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**This phase delivers** the mod's data model — the packed `.gghost` binary structs and the
`GhostManager` singleton — with zero gameplay change, so every later phase has its types ready.

## 1. Spec — Acceptance criteria

| ID | Procedure | Expected outcome |
|----|-----------|------------------|
| P0-AC1 | Build the mod. | `static_assert(sizeof(ReplayHeader)==68)` and `sizeof(FrameData)==17` compile; a wrong size fails the build. |
| P0-AC2 | Reference `GhostManager::get()` from `main.cpp`, build. | Singleton compiles and links; accessors usable. |
| P0-AC3 | Launch GD 2.2081 with the mod. | Loads with no crash; existing log lines still print; gameplay unchanged. |

## 2. Spec — What we will build

**In scope:** `src/DataTypes.hpp` (packed `ReplayHeader` + `FrameData` + `static_assert`s),
`src/GhostManager.hpp` (singleton per SDS §4.2.A), and a minimal `#include` + one reference to
`GhostManager::get()` in `src/main.cpp` so both headers compile and link.

**Out of scope (later phases):** telemetry capture (Phase 1), serialization/file I/O
(`ReplaySerializer`, Phase 1), rendering/hooks logic (Phase 2), UI (Phase 3), networking (Phase 4).
Existing `main.cpp` hook bodies stay as stubs.

## 3. Spec — Functional & non-functional requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-2.2 | Sample tick, X/Y, rotation, gamemode (scale dropped → D2). | Defines `FrameData` shape only. |
| FR-2.4 | Completion serializes to a `.gghost` file. | Phase 0 defines the header/frame layout. |
| NFR-2 | ≤ 250 KB per 2-min recording. | 17-byte frame ⇒ ~60 Hz × 120 s × 17 B ≈ 122 KB. |

## 4. Gaps & Decisions (Resolved)

| ID | Area | Decision | Status |
|----|------|----------|--------|
| D1 | Data/format | `ReplayHeader` = **68 B** (8 icon IDs = 16 B); SDS §2.1's "64 B / 12 B icons" is a doc error. Fix `static_assert` + the "64" mentions in `CLAUDE.md` §4/§8 and `_TEMPLATE.plan.md` §12 to 68. | ✅ Resolved |
| D2 | Data/format | `FrameData` = **17 B**, no `scale` (SDS §2.2 over SRS FR-2.2 wording). | ✅ Resolved |

## 5. Technical plan — Source-tree layout

```text
src/
├── DataTypes.hpp        # (new) #pragma pack(1) ReplayHeader (68B) + FrameData (17B) + 2 static_asserts
├── GhostManager.hpp     # (new) global singleton: active victor id, loaded frames, recording buffer, pause flag
└── main.cpp             # (mod) #include both headers + one GhostManager::get() reference (compile/link anchor)

docs/plans/_TEMPLATE.plan.md   # (mod) §12 universal gate: sizeof(ReplayHeader)==64 → ==68
CLAUDE.md                      # (mod) §4 "exactly 64 bytes" and §8 DoD static_assert: 64 → 68
```
(`CMakeLists.txt` GLOB_RECURSEs `src/*.cpp`, so no build-file edit is needed for the new headers.)

## 6. Technical plan — Module responsibilities

| Layer | Owns (this phase) | File(s) |
|-------|-------------------|---------|
| Data model / binary format | packed structs + compile-time size guarantees | `src/DataTypes.hpp` |
| Manager singleton | cross-layer shared state (Meyers singleton, non-copyable) | `src/GhostManager.hpp` |
| Hook entry | includes headers so they compile; references singleton once; no logic | `src/main.cpp` |

## 7. Technical plan — Data & format

| Struct / buffer | Layout | Notes |
|-----------------|--------|-------|
| `ReplayHeader` | 68 B, `#pragma pack(push,1)`: magic[4] `GGST` · `formatVersion` u16=1 · `levelID` u32 · `victorName[32]` · 8×`uint16` icon IDs (cube/ship/ball/ufo/wave/robot/spider/swing) · color1 RGB (3×u8) · color2 RGB (3×u8) · `totalFrames` u32 | magic verified later (Phase 1 serializer); defaults set here. |
| `FrameData` | 17 B: `tick` u32 · `x` f32 · `y` f32 · `rotation` f32 · `gameMode` u8 | no `scale` (D2). |
| `GhostManager::m_loadedGhostFrames` / `m_recordingBuffer` | `std::vector<FrameData>` | empty at construction; populated in later phases. |

## 8. Technical plan — Rules & invariants

| Rule | Source | Enforced in |
|------|--------|-------------|
| 1-byte packing, no padding gaps | SDS §2.1 | `DataTypes.hpp` (`#pragma pack`) |
| `sizeof(ReplayHeader)==68`, `sizeof(FrameData)==17` | D1 / D2 | `DataTypes.hpp` (`static_assert`) |
| Header defaults: magic `GGST`, `formatVersion=1` | SDS §2.2 | `DataTypes.hpp` (member initializers) |
| Single global instance, non-copyable | SDS §4.2.A | `GhostManager.hpp` (private ctor + deleted copy/move) |
| 17-byte frame preserves NFR-2 budget | NFR-2 | `DataTypes.hpp` |

## 9. Per-tick / lifecycle flow

**Not applicable this phase.** Phase 0 introduces no runtime flow — it only defines types and a
singleton. The lifecycle sequence diagram is introduced in Phase 1, where these structs are first read
and written.

## 10. Convention compliance (CLAUDE.md §4 / §5)

| Rule | Status | Note |
|------|--------|------|
| `$modify` calls base impl first | N/A | No hook logic changes; existing base-calls untouched. |
| Per-instance state in `Fields` | N/A | `GhostManager` is a global singleton by design (SDS §4.2.A), not per-instance state. |
| Header-only helpers as `.hpp`; binary structs in `DataTypes.hpp` | ✅ | Exactly this layout. |
| `using namespace geode::prelude;` / `log::` | ✅ | The `main.cpp` reference uses `log::info`. |
| Meets NFR budgets | ✅ | NFR-2 by 17-byte frame design. |

## 11. Tasks (checklist)

| ID | Task | File |
|----|------|------|
| T01 | Create packed `ReplayHeader` + `FrameData` + two `static_assert`s | `src/DataTypes.hpp` |
| T02 | Create `GhostManager` singleton (state + accessors, non-copyable) | `src/GhostManager.hpp` |
| T03 | `#include` both headers in `main.cpp`; add one `GhostManager::get()` log reference | `src/main.cpp` |
| T04 | Correct `64 → 68` references | `CLAUDE.md`, `docs/plans/_TEMPLATE.plan.md` |
| T05 | `geode build` → green | — |

- [x] T01 [x] T02 [x] T03 [x] T04 [ ] T05 *(build pending — no toolchain in dev env; run on your machine)*

## 12. Definition of Done (gate)

**Universal gate:**
- [ ] `geode build` succeeds, no errors.
- [ ] Mod loads in GD 2.2081; no crash on menu / level entry / exit.
- [ ] No regression — gameplay unchanged with the mod active.
- [ ] `static_assert(sizeof(ReplayHeader)==68)` and `sizeof(FrameData)==17` pass.
- [ ] All §11 tasks ticked and §14 File List filled.
- [ ] NFR-2 respected by the 17-byte frame design.

**Phase-specific:**
- [ ] P0-AC1 — struct sizes assert correctly (build proves it).
- [ ] P0-AC2 — `GhostManager::get()` compiles + links from `main.cpp`.
- [ ] P0-AC3 — mod loads, existing log lines still print, no behavior change.

## 13. Verification

- **Build:** `geode build` — **not run in the dev environment** (no cmake / geode CLI / `GEODE_SDK`
  present). Awaiting build on the user's machine.
- **Struct sizes:** verified by arithmetic — `ReplayHeader` = 4+2+4+32+16+6+4 = **68**;
  `FrameData` = 4+4+4+4+1 = **17**. The `static_assert`s will confirm this at compile time.
- **In-game runtime** (GD 2.2081): pending — mod should load, the `GhostManager ready (active victor:
  false)` log line should print in `PlayLayer::init`, and entering a level should behave exactly as
  before. `<result: pending user test>`
- **Note:** the two `static_assert`s are the only automated check; the rest is manual/in-game.

## 14. Dev Agent Record — File List

| File | Created / Modified | Role |
|------|--------------------|------|
| `src/DataTypes.hpp` | Created | Packed `.gghost` structs + size asserts |
| `src/GhostManager.hpp` | Created | Global state singleton |
| `src/main.cpp` | Modified | Include headers + singleton reference |
| `CLAUDE.md` | Modified | 64 → 68 byte corrections |
| `docs/plans/_TEMPLATE.plan.md` | Modified | 64 → 68 in DoD gate |

**Not in scope (later phases):** `ReplaySerializer` + capture (Phase 1), rendering/hooks (Phase 2), UI
(Phase 3), networking (Phase 4).
