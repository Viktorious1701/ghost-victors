# P4b: Client networking

> **FR:** FR-4.1/4.2/4.3 · **AC:** AC-05, AC-07 (+P4b-AC1…5) · **SDS:** §4.2.C · **Spec:** `docs/plans/phase4b-network.spec.md` · **Status:** ✅ done — build + in-game green

The mod talks to the P4a Supabase backend: fetch/list victors, download+cache a chosen ghost, upload
local runs. Async = **sync web on a detached worker thread + `geode::queueInMainThread`** (non-blocking,
UI-safe). Confirmed APIs: `web::WebRequest` (`.header/.param/.body/.getSync/.postSync`), `WebResponse`
(`.ok/.code/.json→Result<matjson::Value>/.data→ByteVector/.string`), matjson (`operator[]`, `isArray`,
range-for, `asString()/asInt()→Result`), `ByteVector=vector<uint8_t>`, `queueInMainThread`.

## Source-tree
```text
src/NetworkManager.hpp   # (new) fetchVictors / downloadGhost / submitRun (worker thread + queueInMainThread)
src/VictorsPopup.hpp     # (mod) Online section + Upload buttons + offline (cached) fallback
mod.json                 # (mod) server-url / server-anon-key / enable-online settings
# main.cpp / GhostManager.hpp reused — a remote cached path selects via the existing DP16 load flow
```

## Behavior
- **Online section:** if `enabled()`, `fetchVictors` → rows `"{name}{ (BOT)} - {dur}s"` + **Race** →
  `downloadGhost` to `replays/<levelID>/remote/<file>.gghost` (skips if cached) → `setSelectedRun` → close.
- **Offline/failed fetch:** list cached `remote/*.gghost` (header-read names) so they still race (AC-07).
- **Local section:** `None` + each run's **Race** (select) and **Upload** (`submitRun`, source=human) →
  alert + refresh online list.
- **Settings:** `server-url` + `server-anon-key` default to the live project (anon is public);
  `enable-online` toggle. Empty/disabled → no Online section, local still works (P4b-AC4).

## API contract used (P4a)
List `GET /rest/v1/runs?level_id=eq.<id>&order=created_at.asc&select=*` · Download
`GET /storage/v1/object/public/gghosts/<path>` · Submit `POST /functions/v1/submit?…` (octet-stream body).

## Rules & invariants
Worker-thread HTTP (non-blocking) · UI only via `queueInMainThread` · popup guarded with `Ref` +
`getParent()` check · download cache reuse (FR-4.3) · server order = upload asc (AC-05).

## Tasks
| ID | Task | File | Status |
|----|------|------|--------|
| T01 | `NetworkManager.hpp` | `src/NetworkManager.hpp` | done |
| T02 | `mod.json` settings | `mod.json` | done |
| T03 | Online section (loading/rows/badges/Race→download→select) | `src/VictorsPopup.hpp` | done |
| T04 | Offline fallback (cached remote rows) | `src/VictorsPopup.hpp` | done |
| T05 | Local Upload button → submitRun + feedback/refresh | `src/VictorsPopup.hpp` | done |
| T06 | `geode build` green | — | pending |

- [x] T01 [x] T02 [x] T03 [x] T04 [x] T05 [x] T06

## Definition of Done
- [x] `geode build` green (user's machine); loads, no regression; struct `static_assert`s pass; §14 filled; non-blocking.
- [x] AC-05 — online list upload-ascending.
- [x] AC-07 — cached remote ghost races offline.
- [x] P4b-AC1 (Online + Local sections) · AC-2 (Race downloads/caches/races) · AC-3 (Upload → appears online) · AC-4 (graceful when disabled) · AC-5 (non-blocking).

## Verification
✅ Build green + in-game verified (2026-08-19): Online section lists server victors; **Upload** of a
local run succeeds and shows in the Online list; **Race** on an online victor downloads/caches/races;
recording→playback intact. (Also fixed during this phase: DP19 cube-start icon.) Deferred: selected-run
indicator + button decoration (UI-polish pass, `CLAUDE.md` §9).

## Dev Agent Record — File List
| File | C/M | Role |
|------|-----|------|
| `src/NetworkManager.hpp` | Created | worker-thread Supabase client (fetch/download/submit) |
| `src/VictorsPopup.hpp` | Modified | Online section + Upload + offline fallback |
| `mod.json` | Modified | server-url / server-anon-key / enable-online |

**Not in scope:** seeding/macro tag (P6), verification+legit filter (P7), AREDL, spectate (P5).

**Deferred UI polish** (user feedback, do in one pass after P6/P7 finalize the popup content):
selected-run indicator (mark the row + "Now racing: <name>", real usability) · decorate the "Victors"
button (cosmetic). See `CLAUDE.md` §9.
