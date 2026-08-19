# Phase 3 — UI · Spec

> Gate 1 artifact (spec). Reviewed & approved. The template-based implementation plan is derived from
> this at `docs/plans/phase3-ui.plan.md`.

**Status:** approved · **FR:** FR-4.1 (local variant), FR-1 (pause visibility) · **AC:** AC-08 (+ phase-level P3-AC*) · **SDS:** §1 (UI Controller), §6 · **Hooks:** `LevelInfoLayer::init`, `PauseLayer::customSetup` · **Depends on:** Phase 1 (recording) + Phase 2 (playback) · **Roadmap:** `CLAUDE.md#8-implementation-roadmap`

**Summary.** Let the player choose which locally-saved run becomes the racing ghost, via a "Victors"
button + popup on the level page, and toggle the ghost's visibility mid-run from the pause menu.
Recording changes to **keep every completed run** (not overwrite) so there's a real list to pick from.
Network-fed victor lists + upload-order ranking are Phase 4 — Phase 3 builds the UI shell against local
files.

---

## 1. Acceptance criteria

| ID | Procedure | Expected outcome |
|----|-----------|------------------|
| AC-08 | Open `PauseLayer` mid-level, hit "Hide Ghost". | Ghost vanishes immediately; toggling back shows it. |
| P3-AC1 | Open a level page. | A "Victors" button appears in `LevelInfoLayer`. |
| P3-AC2 | Click it on a level with ≥1 local run. | Popup lists the runs for THIS level (victor name + length); other levels' runs aren't shown. |
| P3-AC3 | Pick a run, then play. | That run is the ghost you race. Pick a different one → different ghost next play. |
| P3-AC4 | Complete two runs on one level. | **Both** `.gghost` files are kept (no overwrite); both appear in the popup. |
| P3-AC5 | Level with no local runs. | Popup shows an empty/"no victors yet" state; no ghost, no error. |

---

## 2. What we will build

**In scope:**
- **Recording → keep-multiple:** save each completed run to `replays/<levelID>/<name>.gghost` (naming
  scheme decided in gate 2 — likely timestamp-based). Supersedes the single-file overwrite (DP3).
- **`LevelInfoLayer` "Victors" button** (via `geode.node-ids` to find the menu) → opens a popup.
- **Victors popup** (local): lists this level's runs (victor name + duration ≈ frames/60), select one →
  set it active in `GhostManager`; option to select "none" (disable ghost).
- **Ghost load respects the selection:** `PlayLayer` loads the **selected** run (default = most recent
  local run if none picked), replacing the fixed `replays/<levelID>.gghost` auto-load.
- **`PauseLayer` toggle** → flips `GhostManager::setGhostVisibleInPause`; `PlayLayer::postUpdate` must
  **actively hide** the ghost when off (today it just skips the drive, leaving it on screen).

**Out of scope (later phases):**
- Network fetch + **upload-order ranking** (AC-05) → Phase 4 (the popup is local-only for now).
- Offline-cache download (AC-07) → Phase 4.
- Replay/Spectate & overlay → Phase 5.

---

## 3. Functional / non-functional requirements

| ID | Requirement | Notes |
|----|-------------|-------|
| FR-4.1 (local) | A "Victors" entry point on the level page listing victors. | Local files now; API-fed in Phase 4. |
| FR-1.x (pause) | Ghost visibility respects a mid-game toggle. | `setGhostVisibleInPause` + immediate hide. |
| AC-08 | Pause toggle hides/shows the ghost immediately. | `PauseLayer::customSetup`. |
| NFR-3 | Popup opens / list builds fast (a handful of files). | Just a dir listing + header reads. |

---

## 4. Decisions

- **DP14 — Keep multiple recordings per level** (supersedes DP3 overwrite): `replays/<levelID>/…`, one
  file per completed run. `PlayLayer` default-loads the most recent unless the popup selected another.
- **DP15 — "Victors" popup is local this phase.** Lists local runs (victor name + length); no
  upload-order ranking yet (that's Phase 4's networked list — AC-05).
- **DP16 — Selection lives in `GhostManager`** (extend it to hold the chosen run/path, not just a
  bool). Set by the popup, read by `PlayLayer` load. Session-scoped (persisting the per-level choice
  across sessions is a nice-to-have, gate-2 call).
- **DP17 — Pause toggle must actively hide the ghost when off** (add the else-branch in `postUpdate`),
  not just stop driving it.
- **File org:** add `src/hooks/LevelInfoLayerHook.cpp` + `src/hooks/PauseLayerHook.cpp` + a popup
  header (per SDS §6); move the existing `LevelInfoLayer` stub out of `main.cpp`. PlayLayer stays in
  `main.cpp` for now.
- ⚠ Geode UI bindings to confirm at gate 2/3: `LevelInfoLayer` menu node-id (`geode.node-ids`),
  `geode::Popup`/`FLAlertLayer` for the popup, `PauseLayer::customSetup` + its button menu, and a
  `CCMenuItemToggler`/sprite for the toggle.
