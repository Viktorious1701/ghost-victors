# P4a — Supabase backend & data model · Spec

> Gate 1 artifact (spec). Reviewed & approved. First slice of the Victors Platform
> (`docs/plans/victors-platform.design.md`). **Server-side only — no mod/C++ this phase.**

**Status:** approved · **FR:** FR-4.1 / 4.2 / 4.3 (server side) · **AC:** AC-05 (upload-order source), AC-07 (enables offline cache in P4b) · **Design:** `docs/plans/victors-platform.design.md` · **Depends on:** `.gghost` format (Phase 0)

**Summary.** Create the hosted store + API for victor ghosts: a Postgres `runs` table (with reserved
verification columns), a public-read `gghosts` storage bucket, RLS that allows anon **read** but no
direct anon **write**, and a `submit` Edge Function that validates + stores a run. Delivered as
version-controlled files in the repo (`supabase/…`) plus a dashboard setup guide. Setup is
**dashboard-paste**; uploads are **Edge-Function-only** (mod ships only the anon key).

---

## 1. Acceptance criteria (server-side; verified by curl/dashboard, no in-game)

| ID | Procedure | Expected |
|----|-----------|----------|
| P4a-AC1 | Run `supabase/schema.sql` in the SQL Editor. | `runs` table exists with all columns + `source`/`legit_status` enums; `created_at default now()`; index on `(level_id, created_at)`. |
| P4a-AC2 | Create the `gghosts` storage bucket. | Bucket exists, **public read**. |
| P4a-AC3 | Inspect RLS. | anon can `SELECT` `runs`; anon **cannot** insert/update/delete directly; storage not anon-writable. |
| P4a-AC4 | Deploy + call the `submit` Edge Function with a small `.gghost` + metadata. | Validates `GGST` magic + size ≤ 250 KB; stores blob at `gghosts/<level_id>/<uuid>.gghost`; inserts a `runs` row (`legit_status='unverified'`, `source` from payload); returns `{id, gghost_path}`. |
| P4a-AC5 | `GET /rest/v1/runs?level_id=eq.<id>&order=created_at.asc` with the anon key; then open the blob's public URL. | Returns the row(s) upload-ascending (AC-05); the blob downloads. |
| P4a-AC6 | Record project URL + anon key. | Captured for P4b `mod.json` settings (not committed as secrets). |

---

## 2. What we will build
- `supabase/schema.sql` — enums, `runs` table, indexes, RLS policies (paste into SQL Editor).
- `supabase/functions/submit/index.ts` — Edge Function (Deno/TS): validate `GGST` magic + size, upload
  blob (service-role, server-side), insert row, return id/path.
- `docs/plans/phase4a-setup.md` — the **numbered dashboard walkthrough** (create project → run SQL →
  create bucket → set policies → deploy function → test with curl → copy URL + anon key).
- No mod/C++ changes.

---

## 3. Requirements
- FR-4.2 list order = `created_at` ascending ("First Victors First", AC-05).
- Blob validation: first 4 bytes `GGST`, size ≤ 250 KB (NFR-2).
- Security: mod uses **anon key only**; the function uses the **service_role** key from its own runtime
  env (never shipped/committed); storage public-read, writes only via the function.

---

## 4. Decisions
- **Dashboard-paste** primary; files kept in `supabase/` for version control.
- **Edge-Function-only** writes; anon = read-only via RLS.
- **Public-read** `gghosts` bucket (ghosts aren't sensitive; simplest downloads).
- `legit_status` **enum**, default `unverified`; verification workflow **deferred (P7)** — columns
  reserved so no later migration.
- **Submit payload shape** (exact form finalized in gate 2): metadata (level_id, victor_name, source,
  frame_count, duration_sec, format_version) + the raw `.gghost` bytes — likely `multipart/form-data`
  or JSON+base64, whichever is cleanest for Geode's `web::WebRequest` in P4b.
- **Out of scope:** mod client (P4b), seeding (P6), verification form/review + AREDL (P6/P7).
