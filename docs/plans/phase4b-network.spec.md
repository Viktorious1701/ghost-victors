# P4b — Client networking · Spec

> Gate 1 artifact (spec). Reviewed & approved. Teaches the in-game **mod** to talk to the P4a Supabase
> backend (`https://hlrrsctbapvcffpiiupy.supabase.co`). The mod stays the client (in-game); the server
> is just shared storage + a list. Back to mod C++ → ends with `geode build` + in-game test.

**Status:** approved · **FR:** FR-4.1, FR-4.2, FR-4.3 · **AC:** AC-05 (upload-order list), AC-07 (offline fallback) · **SDS:** §4.2.C (NetworkManager) · **Design:** `docs/plans/victors-platform.design.md` · **Depends on:** P4a (backend), Phase 3 (Victors popup)

**Summary.** The mod becomes a client of the Supabase backend: async-fetch a level's victors, render
them in the Victors popup (Online section, badged, upload order) next to local runs, download+cache a
chosen online ghost so it races via the existing engine, fall back to cache when offline, and upload
local runs through the `submit` Edge Function.

---

## 1. Acceptance criteria

| ID | Procedure | Expected |
|----|-----------|----------|
| AC-05 | Open Victors on a level with ≥2 online runs. | Online list is **upload-ascending** (first uploader = #1). |
| AC-07 | Download an online ghost, then disconnect internet and re-open the level. | The **cached** ghost still loads and races; no error. |
| P4b-AC1 | Open the Victors popup. | Shows an **Online** section (server victors + 🤖 Bot / 👤 human badges, duration) and a **Your runs (local)** section. |
| P4b-AC2 | Select an online victor, then play. | It downloads+caches (once) and becomes the ghost you race. |
| P4b-AC3 | Hit **Upload** on a local run. | `submit` succeeds; the run appears in the Online list on refresh. |
| P4b-AC4 | Clear/omit the server settings. | No online list (graceful); local runs still work. |
| P4b-AC5 | Slow/failed network. | Async, non-blocking — gameplay/menus never freeze or crash. |

---

## 2. What we will build
- `src/NetworkManager.hpp` — async `web::WebRequest` helpers: `fetchVictors(levelID, cb)`,
  `downloadGhost(gghostPath, destPath, cb)`, `submitRun(levelID, header, filePath, cb)`; reads the
  Supabase URL + anon key from settings.
- **`mod.json` settings:** `server-url` (default the live project URL), `server-anon-key` (default the
  anon key — it's public), and an `enable-online` toggle (default on).
- **`VictorsPopup`** gains an **Online section**: async load → rows (victor name · badge · duration ·
  `Race` button that downloads+caches then selects). Loading / empty / offline states. Local section
  keeps its `Race`/`Off` rows **plus an `Upload` button** per local run.
- **Download + cache:** to `replays/<levelID>/remote/<uuid>.gghost`; selecting sets the GhostManager
  selection (reuse DP16) to that cached path. Skip re-download if cached (enables AC-07 offline).
- **Upload:** `Upload` → read the local `.gghost` (header + size) → POST to `submit` with metadata
  (level_id, victor_name from header, source=`human`, frame_count, duration_sec, format_version) + bytes.

---

## 3. Functional / non-functional
- FR-4.1 query victors on opening the level UI · FR-4.2 upload-order ranking (AC-05) · FR-4.3 async
  download + local cache, no duplicate requests · AC-07 offline fallback · non-blocking (NFR-1 spirit).

---

## 4. Decisions
- **Online + Local in one popup** (two sections); online select = download→cache→select.
- **Upload per local run**, `source=human` for now (macro/bot tagging = P6 seeding).
- **URL + anon key as mod settings**, defaulting to the live project (anon key is public, safe to ship).
- **Cache reuse = offline fallback (AC-07):** cached `remote/*.gghost` remain playable with no network;
  if `fetchVictors` fails, the popup still lists already-cached online ghosts.
- **Badges:** 🤖 `bot` · 👤 `human` (the ✔ legit badge/filter is deferred to P7).
- Old pre-keep-multiple flat files (`replays/<id>.gghost`) are ignored (harmless).
- ⚠ Confirm Geode `web::WebRequest` API at gate 2 from local headers (`web::WebRequest`, `.get/.post`,
  headers, body bytes `ByteVector`, `.listen`, `WebResponse::data()/string()/code()`).

**Out of scope:** seeding/macro tagging (P6), verification + legit filter (P7), AREDL, spectate (P5).
