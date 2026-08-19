# Ghost Victors — Onboarding

How this project is built and how we work on it. Read `CLAUDE.md` for the authoritative coding rules,
and treat `docs/SRS.md` (requirements + AC matrix) and `docs/SDS.md` (architecture) as the source of
truth. This file is the "get oriented + current state + lessons learned" companion.

---

## 1. What it is

An interactive **play-along ghost-racing** mod for Geometry Dash 2.2 (GD `2.2081`) on the **Geode SDK**.
You control your own character; a semi-transparent, **icon-only** ghost of a past run advances beside
you in real time. Runs are recorded to a compact `.gghost` binary and (later) shared/ranked by upload
order. Core loop: **record** valid 0→100 % Normal-Mode runs → **replay** with lerp + a start-line
opacity fade → **share/fetch** via REST.

---

## 2. Current status

| Phase | Scope | Status |
|-------|-------|--------|
| **0 — Data & state** | `DataTypes.hpp` (`ReplayHeader` 68 B, `FrameData` 17 B) + `GhostManager` singleton | ✅ Done (build + in-game verified) |
| **1 — Recording** | capture telemetry → `.gghost` on completion; gated to Normal/from-0 % | ✅ Done (records ~60 Hz, save+reload verified in-game) |
| **2 — Playback** | icon-only ghost, X-progress lerp, opacity fade, reset sync, leading pacer, gravity/mini | ✅ Done (in-game verified: gravity flip, mini, mirror-damped lead, correct icon) |
| **3 — UI** | "Victors" button in `LevelInfoLayer` + `PauseLayer` hide/show toggle | ⏳ Next |
| **4 — Victors Platform (Supabase)** | P4a backend (`runs` table + storage + `submit` fn) · P4b client fetch/cache/offline + Bot badges + submit · P6 macro seeding ("Bot Vikkie") | ⏳ Next (design: `docs/plans/victors-platform.design.md`) |
| **7 — Legit verification** | submission form (YouTube + Drive raw footage) + Vikkie review; AREDL top-300 rule | ⏳ Deferred (columns reserved) |
| **5 — Replay / Spectate & Compare** | watch a run without playing + overlay two runs (full visuals, camera-follow) | ⏳ Future |

Each phase has a spec + plan under `docs/plans/phase{N}-*.{spec,plan}.md`.

---

## 3. How we work — the three-gate discipline

**Never jump straight to code.** For every phase (or non-trivial feature), work in three reviewed gates:

1. **Spec** → `docs/plans/phase{N}-{slug}.spec.md`: acceptance criteria → what we build → FR/NFR. Stop
   for review.
2. **Plan** → `docs/plans/phase{N}-{slug}.plan.md` from `docs/plans/_TEMPLATE.plan.md`: architecture,
   file structure, rules, a per-tick diagram, task checklist, and a **Definition-of-Done gate**. Stop
   for review.
3. **Implement + build** → write the code, then hand back for the user to `geode build` and test in-game.

Rules of the road:
- Draft each spec/plan for approval **before** writing the repo file (don't commit the plan doc
  unreviewed).
- "keep going" means *advance to the next gate*, not skip one.
- A phase is **Done only when its plan §12 DoD gate is fully ticked** (build green · loads, no crash ·
  no regression · struct `static_assert`s pass · tasks ticked · NFR budgets · phase ACs verified
  in-game). IDs come from the specs: **FR-**/**AC-**/**NFR-** and **SDS §**.

---

## 4. Build & test loop

- **The dev/agent environment has no C++ toolchain** — no compiler, cmake, or `geode` CLI. Claude
  writes code; **the user builds and tests on Windows.** Never claim a green build that wasn't run.
- User's setup: **Windows · Visual Studio 17 2022 · MSVC `cl.exe` 14.44 (toolset v143)** · Geode SDK
  **5.9.0** at `C:\Users\GLOBEE\Documents\Geode` · Geode CLI 3.8.0 · GD `2.2081`. Build: `geode build`.
- **Toolchain gotcha (important):** bindings are pulled from `main` (unpinned), so a fresh fetch can
  outrun the installed Codegen → parse errors in `GeometryDash.bro` (e.g. the `[[renamed_from]]`
  attribute). Fix: `geode sdk update` **then** `geode sdk install-binaries` (update alone leaves "No
  valid loader binary to link to"), then delete `build/` and rebuild. If `build/` won't delete, it's
  almost always **Geometry Dash still running** (the mod DLL is loaded) or VS Code — close them.
  Reproducible alternative: pin bindings via `GEODE_BINDINGS_REPO_PATH`.
- **Where things land at runtime:**
  - `.gghost` files → `%LocalAppData%\GeometryDash\geode\mods\vikkie.ghost-victors\replays\<levelID>.gghost`
  - Logs → Geode platform console (Geode → Settings → *Show Platform Console*) or
    `<GD install>\geode\logs\`. The mod logs loudly (`Recording SAVED … frames → … (bytes)`,
    `Verify reloaded OK`, `capture started`, `recording SKIPPED`).
- **Only automated check** is the struct `static_assert`s (they fail the compile if the layout drifts).
  Everything else is a manual in-game walk of the phase's AC-IDs.

---

## 5. Architecture at a glance

```
src/
  DataTypes.hpp           # packed .gghost structs (ReplayHeader 68B, FrameData 17B) + static_asserts
  GhostManager.hpp        # global singleton: active victor, loaded frames, recording buffer, pause flag
  ReplaySerializer.hpp    # .gghost binary IO + magic/version/size verify (binding-independent)
  GhostStripper.hpp       # icon-only: hide streaks + stop/hide all m_particleSystems (Phase 2)
  OpacityStateMachine.hpp # 0 / ramp 0→128 / 128 fade by player percent (Phase 2)
  InterpolationEngine.hpp # binary-search + lerp of x/y/rotation (Phase 2)
  main.cpp                # PlayLayer + LevelInfoLayer $modify hooks (record + playback wiring)
docs/
  SRS.md · SDS.md         # source of truth
  plans/                  # _TEMPLATE.plan.md + per-phase spec/plan files (with DoD gates)
```

Hooks: `PlayLayer::init` (load + spawn ghost, gate recording), `PlayLayer::postUpdate` (per-frame
capture + playback), `PlayLayer::resetLevel` (flush + reset ghost), `PlayLayer::levelComplete`
(serialize). Recording buffer lives in `GhostManager`; per-instance state (ghost ptr, flags,
play-time) lives in the modify class `Fields`.

---

## 6. Hard-won gotchas (do not relearn these)

- **Per-frame hook = `PlayLayer::postUpdate`, NOT `update`.** On Windows `PlayLayer` has no own
  `update` (the loop is `GJBaseGameLayer::update`), so a `PlayLayer::update` hook silently never fires.
- **Running tick = accumulated `dt × 240`.** `GJBaseGameLayer::m_currentStep` reads 0 / isn't cumulative
  in `postUpdate` (caused a 1-frame-only recording). Record and playback share this same `dt`-tick.
- **Strip particles via `m_particleSystems`** (a CCArray) + hide `m_regularTrail`/`m_shipStreak`/
  `m_waveTrail`. The SDS's `m_shipBoostParticles`/`m_dragParticles`/`m_ufoParticles` names don't exist.
- **From-0 % gate** = `!m_isPracticeMode && m_startPosObject == nullptr` (there is no `m_isFrom0`).
- **`GJGameLevel::m_levelID` is a `SeedValueRSV`** → use `.value()` for the int.
- **`ReplayHeader` is 68 bytes, not 64** (the SDS diagram's "64 B / 12 B icons" is wrong; 8 icon IDs
  = 16 B). Gamemode from `PlayerObject` bool flags (`m_isShip/m_isBall/m_isBird`(UFO)`/m_isDart`(wave)/
  `m_isRobot/m_isSpider/m_isSwing`, cube = all false).
- Binding names are reverse-engineered and can move between versions — **verify against the local
  `geode-sdk/bindings` checkout** (a copy was pulled to scratch during Phase 1/2) before trusting one.

---

## 7. Key decisions (traceable in the plan files)

- **D1** header = 68 B · **D2** `FrameData` has no `scale` (17 B) · **D3** position-only now, `.gghost`
  format kept forward-compatible for a future **input-event track** (click-pattern comparison —
  can't be backfilled, so schedule it before mass-recording) · **D4** capture ~60 Hz (every 4th tick)
  for NFR-2 · **D5** correct step field · **D6** Player 1 only · **D7/D8** postUpdate + `dt`-tick.
- **DP1** Phase 2 auto-loads the level's own `.gghost` (race yourself; victor UI is Phase 3) ·
  **DP3** completion overwrites the file · **DP4** ghost is visual-only (never `m_player1/2`) ·
  **DP7** vehicle mode-switch via `toggle*Mode(…, noEffects=true)`, cube-only fallback if fiddly.

---

## 8. Open items

- **REST API base URL is undefined** (SDS placeholder `https://your-api.com/...`) — decide it (ideally a
  Geode mod setting in `mod.json`) before Phase 4.
- Template metadata (`about.md`, `changelog.md`, `support.md`, `README.md`) still placeholder.
- Phase 2 vehicle mode-switching + opacity-cascade + z-order need in-game confirmation.
- **Future — Replay / Spectate & Compare** (CLAUDE.md §8 "Phase 5"): watch a run without playing +
  overlay two runs to compare (time-driven, no lead). Shows **full visuals** (racing ghost stays
  icon-only). Main risk = camera-follow-without-a-live-player; trails/particles need the ghost's
  `update` driven. Click-pattern analytics needs the deferred input-event track (D3).

---

## 9. Where the truth lives

`CLAUDE.md` (coding rules + roadmap + DoD) · `docs/SRS.md` / `docs/SDS.md` (spec) · `docs/plans/*`
(per-phase spec/plan with decisions + verification records) · Claude memory
(`gd-binding-gotchas`, `build-environment`, `phase-workflow`).
