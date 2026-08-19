# Ghost Victors — Online Platform Design

> Design doc (approved direction). Turns the mod from local-only into a client of a **Supabase** service
> that hosts and ranks victor ghosts, seeds **Bot** ghosts for hard levels, and lets victors submit runs.
> Resolves the long-open "REST API base URL undefined" (→ Supabase, as mod settings).
>
> **Scope now:** backend + client networking + macro **seeding** + clear **Bot** labeling + **submit**.
> **Deferred:** the **legit-verification** workflow (see §6) — its DB columns are reserved so it needs no
> migration later.

## 1. Principles
- `.gghost` stays the **telemetry blob** (positions). All richer data (source, legit, video links, rank,
  upload date) is **server metadata**, not in the binary header.
- **Legit is not detectable from the data** (we record positions, not inputs) → it can only ever be a
  **human-reviewed flag**. That's why verification is video-based and deferred to its own phase.
- The mod only **consumes**: list → download blob → render badges → optionally submit.

## 2. Data model (Supabase Postgres) — `runs`
| column | type | notes |
|--------|------|-------|
| `id` | uuid PK | |
| `level_id` | int | GD level ID |
| `victor_name` | text | e.g. "Bot Vikkie" for seeds |
| `source` | enum `human`\|`macro`\|`bot` | drives the badge |
| `legit_status` | enum `unverified`\|`legit`\|`rejected` | **reserved**; default `unverified` (verification phase flips it) |
| `youtube_url` | text null | **reserved** (verification) |
| `raw_footage_url` | text null | **reserved** (Drive; required for AREDL top-300) |
| `aredl_rank` | int null | level rank at submission |
| `frame_count` | int | |
| `duration_sec` | int | |
| `format_version` | int | from `.gghost` header |
| `gghost_path` | text | Storage key |
| `file_size` | int | |
| `created_at` | timestamptz | **upload order** → FR-4.2 "First Victors First" / AC-05 |
| `verified_by` / `verified_at` | text / timestamptz null | **reserved** (verification) |

Storage bucket `gghosts`: blobs at `gghosts/<level_id>/<uuid>.gghost` (public-read).
Optional `levels` cache table: `level_id`, `name`, `aredl_rank`, `aredl_top300` bool.

## 3. API (Supabase REST + one Edge Function)
- **List:** `GET /rest/v1/runs?level_id=eq.{id}&order=created_at.asc` (anon key, RLS read-only) — upload
  order (AC-05). Client badges/filters by `source` / `legit_status`.
- **Download:** public Storage URL of `gghost_path` (or a signed URL).
- **Submit (upload):** an **Edge Function `submit`** — the mod ships only the **anon** key (never
  `service_role`). Validates size + `GGST` magic, stores the blob, inserts the row as
  `legit_status='unverified'` with `source` from the client's honesty flag. Rate-limited.
- **AREDL:** pull the main list + ranks from the AREDL public API (for seeding targets + `aredl_rank`).

## 4. Client (mod) networking — P4b
- The **Victors popup** (Phase 3) gains a **remote list** via a new `NetworkManager` (async
  `web::WebRequest`): rows show **source badge** (🤖 Bot · 👤 human · • unverified), duration, rank,
  ordered by upload date. (A "legit only" filter is added with the verification phase.)
- **Download + cache** the chosen blob to `replays/<levelID>/remote/<id>.gghost`; it then plays through
  the existing engine. **Offline fallback** (AC-07): use the cache when the request fails.
- **Submit:** after a completion, offer "Upload run" → `submit` Edge Function with the `.gghost` + a
  **source** honesty flag (human/macro). Ships `unverified`.
- Supabase **project URL + anon key** as **mod settings** in `mod.json`.

## 5. Seeding — "Bot Vikkie" ghosts — P6
- A **normal-mode macro run already records** positions (the mod can't tell it's a macro). Add a mod
  setting **"allow practice/macro recording"** so practice/testing macro runs also capture.
- Workflow: play a community macro of a demon (Megahack/Eclipse) with the mod recording → `.gghost` →
  submit tagged `source=bot`, `victor_name="Bot Vikkie"`. A small **batch-upload helper** + an **AREDL
  target list** to know which demons to seed.
- Honesty: bot ghosts are always badged **🤖 Bot** and never `legit`.

## 6. Verification & moderation — ⏳ DEFERRED (later phase)
Not built now. Schema columns above are **reserved** so this adds no migration. Until built, every run
is `unverified` and shown by its `source` badge. When built:
- **Submission form** (web, outside the mod): pick your uploaded run, give a **YouTube** link (always);
  **Google-Drive raw footage** required when the level is **AREDL top-300**.
- **Review:** Vikkie compares video ↔ run, sets `legit` / `rejected`.
- Abuse controls: default `unverified`; RLS + rate limits; footage is **linked, never hosted**; never
  ship the `service_role` key.

## 7. Build order
**Now:** **P4a** backend & data model (Supabase project, `runs` table w/ reserved columns, storage,
RLS, `submit` fn) → **P4b** client networking (fetch/download/cache/offline + Bot badges + submit) →
**P6** seeding (macro-record setting + Bot Vikkie upload + AREDL targets).
**Deferred:** **P7** verification & moderation · **P5** Replay/Spectate & overlay (parallel future).

## 8. Decisions
- `legit_status` = **enum** (unverified/legit/rejected), reserved now.
- Popup shows **all runs, badged** (Bot/human), with a legit filter added in P7 — not hidden.
- Upload identity: **anonymous + submitted victor name** to start (moderation handles abuse).

## 9. Security notes
- Mod ships the **anon** key only; writes go through the `submit` Edge Function with server-side checks.
- `service_role` key stays server-side. Storage is public-read, write via the function.
- Validate blob (`GGST` magic, size cap ~250 KB per NFR-2) before insert.
