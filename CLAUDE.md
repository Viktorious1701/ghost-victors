# CLAUDE.md — Ghost Victors

Guidance for implementing the **Ghost Victors** Geometry Dash mod. Read this first, then treat
`docs/SRS.md` (requirements + acceptance matrix) and `docs/SDS.md` (technical architecture) as the
**authoritative source of truth**. When this file and the specs disagree, the specs win — update
this file to match.

---

## 1. What this mod is

Ghost Victors is an **interactive play-along ghost-racing mod** built on the **Geode SDK** for
Geometry Dash 2.2+ (GD `2.2081`). The player fully controls their own character; alongside them, a
semi-transparent **icon-only** "ghost" of a past victor advances in real time, driven by recorded
telemetry synced to physics ticks.

Core loop:
- **Record** valid 0%→100% Normal-Mode runs to a compact local `.gghost` binary file.
- **Replay** a selected victor's ghost with framerate-independent interpolation (lerp) and a
  start-line opacity fade-in.
- **Share/fetch** runs via an async REST API; victors are ranked in **chronological upload order**
  ("First Victors First").

---

## 2. Build & test

- **Requires the `GEODE_SDK` env var** pointing at a Geode SDK checkout — `CMakeLists.txt` hard-errors
  without it.
- Build with the Geode CLI: **`geode build`** (see README). Toolchain: CMake ≥ 3.21, **C++23**,
  SHARED library target `ghost-victors`.
- Sources are collected via `GLOB_RECURSE src/*.cpp`, so **new `.cpp` files under `src/` and
  `src/hooks/` are picked up automatically — no CMake edits needed** to add source files. (New
  header-only `.hpp` files never need CMake changes either.)
- CI: `.github/workflows/multi-platform.yml` builds via `geode-sdk/build-geode-mod@main` for
  Windows, macOS, iOS, Android32, Android64.
- **There is no automated test harness.** Verification is manual/in-game against GD `2.2081`. Every
  change should be validated against the SRS §4 acceptance criteria **AC-01 … AC-08**. Treat those as
  the definition of done.

---

## 3. Architecture map

Five decoupled modules (SDS §1, §4). Keep this separation when implementing.

| Module | Responsibility |
| --- | --- |
| **Geode hooks** | Entry points: `LevelInfoLayer`, `PlayLayer`, `PauseLayer`. |
| **`GhostManager`** | Global singleton: active victor id, loaded ghost frames, recording buffer, pause-visibility flag. |
| **Ghost engine** | Particle stripper, opacity state machine, lerp interpolation. |
| **Recording/capture** | Per-tick telemetry buffer + completion trigger. |
| **`ReplaySerializer` / `NetworkManager` / cache** | `.gghost` binary IO, async HTTP via `web::WebRequest`, local filesystem cache. |

Hook → responsibility (SRS §6):

| Hook | Does |
| --- | --- |
| `LevelInfoLayer::init` | Inject "Victors" button; query API (ranked by upload date). |
| `PlayLayer::init` | Load cached `.gghost`, spawn ghost `PlayerObject`, strip particles/trails. |
| `PlayLayer::update` | Lerp playback + 3%–5% dynamic opacity fade-in; telemetry capture. |
| `PlayLayer::resetLevel` | Reset recording buffer; reposition ghost to tick 0. |
| `PlayLayer::levelComplete` | Serialize buffer to `.gghost`; trigger upload. |
| `PauseLayer::customSetup` | Inject mid-game ghost visibility toggle. |

---

## 4. Domain rules that are easy to get wrong

These are the details most likely to be implemented incorrectly. Get them exactly right.

- **Opacity state machine** (SRS FR-1.2…1.4, SDS §3.2). As a function of the player's completion
  percent `P`:
  - `P < 3.0` → `setVisible(false)`, opacity `0`.
  - `3.0 ≤ P < 5.0` → visible; opacity = `floor(128 * (P - 3.0) / 2.0)` (linear `0 → 128`).
  - `P ≥ 5.0` → visible; opacity locked at `128` (50%).
- **Icon-only rendering** (SDS §3.1). On the ghost `PlayerObject`, strip/disable: `m_waveTrail`,
  `m_regularTrail`, `m_shipBoostParticles`, `m_dragParticles`, `m_ufoParticles`, `m_landParticles0`,
  `m_landParticles1`. No trails, streaks, boost bursts, or dust may appear.
- **Recording gate** (SRS FR-2.x). Capture only in **Normal Mode**, only for runs **starting at 0%**
  (`!m_isPracticeMode && m_isFrom0`). Flush the buffer on death/reset; serialize only on
  `levelComplete()`.
- **`.gghost` binary format** (SDS §2). `#pragma pack(push, 1)`. `ReplayHeader` is **exactly 68
  bytes**, magic = `GGST`. `FrameData` is **exactly 17 bytes** (`tick` u32, `x`/`y`/`rotation` f32,
  `gameMode` u8). **Verify the magic on load** and reject mismatches. Do not change struct layout
  without bumping `formatVersion`. (SDS §2.1's diagram says 64 B, but §2.2's 8 icon IDs make the real
  packed size 68 B — decision D1 in `docs/plans/phase0-data-state.plan.md`.) The `gameMode` byte is
  **packed**: bits 0-3 = gamemode enum, bit 4 = upside-down/gravity, bit 5 = mini (Phase 2 DP11); old
  files have the high bits 0 (= normal), so it stays backward-compatible.
- **Ghost playback is X-progress-indexed, not time-indexed** (Phase 2 DP12). Drive the ghost off the
  player's actual `getPositionX()` (+ a lead in frames), not an accumulated-`dt` tick — GD position is a
  function of X, and time-indexing drifts (causes acceleration/shake, worst under mirror portals which
  negate X). See `src/InterpolationEngine.hpp::updateGhostByProgress`. **Mirror-transition damping**
  (Phase 2 DP13): a *leading* ghost is off the flip pivot and swings off-screen while GD animates a
  mirror flip, so scale the lead by `min(1, |m_objectLayer->getScaleX()|)` (→0 mid-flip).
- **Upload-order ranking** (SRS FR-4.2). The first player to upload a run for a level is `#1`. API
  query sorts ascending by upload date (`sort=upload_asc`).
- **Attempt reset sync** (SRS FR-1.5 / AC-06). On `resetLevel()`, the ghost snaps back to tick 0
  (opacity `0`, hidden) together with the player.
- **Per-frame hook must be `PlayLayer::postUpdate`, not `PlayLayer::update`.** On Windows `PlayLayer`
  has no own `update` (only macOS/iOS addresses in the bindings; the loop is `GJBaseGameLayer::update`),
  so a `$modify(PlayLayer)::update` hook **silently never fires** there. Put per-frame telemetry capture
  and (Phase 2) lerp/opacity in `PlayLayer::postUpdate` — a real PlayLayer override that fires every
  frame. Read the per-physics-step counter as `GJBaseGameLayer::m_currentStep`.
- **Pause toggle** (AC-08). The `PauseLayer` toggle flips `GhostManager::setGhostVisibleInPause`;
  `PlayLayer::update` must respect it and hide the ghost immediately when off.
- **Non-functional budgets** (SRS §5): ghost update < 2% CPU/tick (keep 144/240Hz+ smooth); a
  2-minute recording ≤ 250 KB; local replay load/parse < 10 ms.

---

## 5. Coding conventions

- Use the Geode **`$modify`** pattern (see `src/main.cpp`). Always call the base implementation first
  (`if (!PlayLayer::init(...)) return false;`) and short-circuit on failure.
- Store per-instance state in the modify class's **`Fields` struct** (e.g. the ghost
  `PlayerObject*`), never as free member variables.
- `using namespace geode::prelude;`. Log via `geode::log::info` / `log::error`.
- Keep binary structs in `DataTypes.hpp`; implement the small engine helpers as header-only `.hpp`
  files (matching the inline definitions the SDS already provides).
- Create the ghost via `PlayerObject::create(...)` and add it to `m_objectLayer` **behind** the main
  player so it never occludes the real character.
- Match the existing style in `src/main.cpp`: banner comments delimiting hook sections, same brace
  and indentation style.

---

## 6. Target directory layout

Create these as implementation proceeds (SDS §6):

```
docs/
  SRS.md                # requirements + acceptance matrix (source of truth)
  SDS.md                # technical architecture (source of truth)
  plans/
    _TEMPLATE.plan.md   # house-style phase plan template (copy this)
    phase{N}-{slug}.plan.md   # one filled-in plan per phase (see §7)
src/
  main.cpp              # entry / existing hooks (may be split into hooks/ as it grows)
  DataTypes.hpp         # ReplayHeader, FrameData (packed)
  GhostManager.hpp      # global singleton
  GhostStripper.hpp     # particle/trail stripping
  OpacityStateMachine.hpp
  InterpolationEngine.hpp
  ReplaySerializer.hpp  # .gghost binary IO
  NetworkManager.hpp    # async HTTP + download
  hooks/
    PlayLayerHook.cpp
    LevelInfoLayerHook.cpp
    PauseLayerHook.cpp
```

Currently only `src/main.cpp` exists, containing stubbed `$modify` hooks for `LevelInfoLayer` and
`PlayLayer` with placeholder comments — no feature logic yet.

---

## 7. Planning workflow — spec first (required)

**Do not start coding a phase until its plan file exists and has been reviewed.** For every phase (or
any non-trivial feature), first write a small spec+plan file so the work stays legible and trackable.

Steps:

1. Copy `docs/plans/_TEMPLATE.plan.md` → `docs/plans/phase{N}-{slug}.plan.md`
   (e.g. `docs/plans/phase1-recording.plan.md`).
2. Fill the **Spec half first**, in this order: (1) acceptance criteria (the AC-IDs this phase
   satisfies), (2) what we will build (scope + out-of-scope), (3) functional/non-functional
   requirements (the FR-/NFR-IDs). Get it reviewed.
3. Then fill the **Technical half**: architecture (module responsibilities), source-tree/file
   structure, data/format, rules & invariants, the per-tick lifecycle diagram, and the task checklist.
4. Only then write code. Keep the plan updated as you go; tick off tasks in §11, satisfy the
   Definition of Done gate in §12, and fill the post-build File List in §14. A phase is **Done only
   when every box in the §12 DoD gate is ticked.**

All IDs come from the specs: **FR-** (SRS §3), **AC-** (SRS §4), **NFR-** (SRS §5), the hook map
(SRS §6), and **SDS §** design references. Plan status moves `ready-for-dev → in-progress → done`.

---

## 8. Implementation roadmap

Ordered so each phase is independently buildable and testable. Each phase gets its own plan file
(§7) before implementation begins.

- **Phase 0 — Data & state.** Add `DataTypes.hpp` (`ReplayHeader`, `FrameData`) and
  `GhostManager.hpp` singleton. No behavior change; just compiles.
  → `docs/plans/phase0-data-state.plan.md`
- **Phase 1 — Recording.** Capture telemetry in `PlayLayer::update` (Normal-Mode / from-0% gate),
  clear the buffer in `resetLevel`, and `ReplaySerializer::saveToFile` on `levelComplete`. Verify a
  `.gghost` file is written and stays within the NFR-2 size budget.
  → `docs/plans/phase1-recording.plan.md`
- **Phase 2 — Ghost playback.** Add `GhostStripper.hpp`, `OpacityStateMachine.hpp`,
  `InterpolationEngine.hpp`. Spawn the ghost `PlayerObject` in `PlayLayer::init`, drive lerp +
  opacity in `update`, reset in `resetLevel`. Validate **AC-01…AC-04, AC-06**.
  → `docs/plans/phase2-playback.plan.md`
- **Phase 3 — UI.** `LevelInfoLayerHook` "Victors" button + popup; `PauseLayerHook` hide/show toggle
  wired to `GhostManager::setGhostVisibleInPause`. Validate **AC-08**.
  → `docs/plans/phase3-ui.plan.md`
- **Phase 4 — Network + cache (Supabase-backed "Victors Platform").** Expanded from the SRS's generic
  REST plan into a Supabase service — see `docs/plans/victors-platform.design.md`. Sub-phases:
  - **P4a — Backend & data model:** Supabase `runs` table (with reserved verification columns), storage
    bucket, RLS, `submit` Edge Function. (Server-side, not the mod.)
  - **P4b — Client networking:** `NetworkManager` async fetch (upload-order ranking, AC-05) + download
    to local cache + offline fallback (AC-07) + **source badges** (🤖 Bot) + submit path.
  - **P6 — Seeding:** "allow practice/macro recording" setting + **Bot Vikkie** upload + AREDL targets.
  - **P7 — Verification & moderation (deferred):** submission form + Vikkie review + legit rule
    (top-300 raw footage). Columns reserved now; workflow built later.
  → `docs/plans/victors-platform.design.md`
- Split `main.cpp` hooks into `src/hooks/*.cpp` per §6 as they grow.
- **Phase 5 — Replay / Spectate & Compare (future, post-Phase 4).** A separate **time-driven,
  no-live-player** mode built on the same position-based `.gghost`:
  - *Standalone replay* — watch a recorded run without playing: drive the ghost by an internal time
    clock (tick-indexed), with the **camera following the replay** and the live player hidden/frozen.
  - *Overlay / compare* — load **2+ runs**, drive them all off one shared clock at **true tick (no
    lead)** to compare how two players' paths/timing differ.
  - **Full visuals here** (particles, wave trail, boost, etc.) — do **not** strip. Stripping becomes
    **mode-dependent**: the live play-along racing ghost stays **icon-only** (§4 / AC-04); the spectate
    ghosts render full.
  - New work: a **time-indexed playback path** alongside the X-progress engine; **multi-ghost**
    (generalize `GhostManager` from one loaded run to N); **camera control** (the main risk — puppet the
    camera along the replay, or puppet+hide the real player); a spectate entry point; and **actually
    driving the ghost's `update`/animation so trails+particles emit** (they don't under pure
    `setPosition`).
  - Note: click-pattern **analytics** (Zoink vs Doggie input comparison) still needs the deferred
    **input-event track** (spec D3) — the *visual* overlay works with position data today.

### Definition of Done

A phase is only **Done** when its plan file's §12 DoD gate is fully ticked. That gate has a **universal
part** applied to every phase — `geode build` green · loads in GD `2.2081` with no crash · no
regression to base gameplay · struct `static_assert`s pass (`sizeof(ReplayHeader)==68`,
`sizeof(FrameData)==17`) · all plan tasks ticked · relevant NFR budgets met — plus the **phase-specific**
criteria below. There is no automated test harness: apart from the `static_assert`s, done-ness is
proven by a manual in-game walk of the listed AC-IDs against GD `2.2081`.

| Phase | Definition of Done (must all pass) | Covers |
|-------|------------------------------------|--------|
| **0 — Data & state** | Struct `static_assert`s pass; `GhostManager::get()` reachable; builds + loads with **no in-game behavior change**. | (foundation) |
| **1 — Recording** | Full 0→100% Normal-Mode run writes a `.gghost` (≤250 KB / ~2 min); Practice or non-0% start records nothing; death mid-run clears the buffer (no file); reload validates magic `GGST` and `totalFrames` = frame count. | FR-2.x · NFR-2 |
| **2 — Playback** | Ghost follows path while player controls own char; invisible 0–3%; smooth fade 0→128 over 3–5%; icon-only (no trails/particles) in ship/wave/UFO; die@40% + restart resets ghost **and** player to tick 0 together; frame-stable at 144/240Hz. | AC-01…04 · AC-06 · NFR-1 |
| **3 — UI** | "Victors" button appears in `LevelInfoLayer`; `PauseLayer` toggle hides/shows the ghost immediately mid-level. | AC-08 |
| **4 — Network + cache** | Victors list is upload-ascending (first uploader = #1); downloads cached with no duplicate requests; offline with a cached ghost still loads from disk and renders. | AC-05 · AC-07 |

---

## 9. Open items / TODO

- **REST API backend = Supabase** (decided; supersedes the SDS `https://your-api.com` placeholder).
  See `docs/plans/victors-platform.design.md`. The Supabase **project URL + anon key** ship as **mod
  settings** in `mod.json`; uploads go through a `submit` Edge Function (anon key only, never
  `service_role`). Still to create: the Supabase project/table/bucket (P4a).
- **Metadata is still template default.** `mod.json` `description` is empty; `about.md`,
  `changelog.md`, `support.md`, and `README.md` are unedited template placeholders. Fill these in
  before any release.
- **Not under version control.** There is no `.git` directory yet. Initialize git if you want history
  before starting implementation.
- **Replay / Spectate & Compare** is a planned **future phase** (see §8 "Phase 5") — watch a run
  without playing + overlay two runs to compare, with full visuals and camera-follow. Not started.
- **Verify GD bindings.** The SDS code is illustrative. Confirm exact member/method names against the
  installed Geode `5.8.2` bindings while implementing — some may need adjustment to compile. Verified
  so far (from `geode-sdk/bindings` 2.2074, used by Geode 5.8.x): the physics step counter is
  `GJBaseGameLayer::m_currentStep` (**not** `m_gameState->m_currentTick`, which does not exist); there
  is **no `m_isFrom0`** — detect a from-0% run via `!m_isPracticeMode && m_startPosObject == nullptr`;
  `GJGameLevel::m_levelID` is a `SeedValueRSV` (use `.value()` for the int); gamemode is read from
  `PlayerObject` bool flags (`m_isShip`/`m_isBall`/`m_isBird`(UFO)/`m_isDart`(wave)/`m_isRobot`/
  `m_isSpider`/`m_isSwing`, cube = all false). Still to confirm: the particle/trail member names
  (Phase 2).
