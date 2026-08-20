# P4a: Supabase backend & data model

> **FR:** FR-4.1/4.2/4.3 (server) · **AC:** AC-05 (upload order), AC-07 (enables P4b cache) · **Design:** `docs/plans/victors-platform.design.md` · **Spec:** `docs/plans/phase4a-backend.spec.md` · **Status:** ✅ done — live & curl-verified · **Project:** `https://hlrrsctbapvcffpiiupy.supabase.co` (anon key held for P4b)

Stand up the Supabase backend the mod (P4b) consumes: a `runs` table, a public-read `gghosts` bucket,
RLS (anon read-only), and a `submit` Edge Function. Server-side only; no mod code.

## Source-tree layout
```text
supabase/
├── schema.sql                 # enums + runs table + index + RLS
└── functions/submit/index.ts  # Deno Edge Function: validate + store + insert
docs/plans/phase4a-setup.md    # numbered dashboard walkthrough + curl tests
```

## schema.sql
Two enums (`run_source`, `legit_state`), `runs` table (all columns incl. reserved verification ones),
index on `(level_id, created_at)`, RLS enabled with a public **read** policy only. Writes happen solely
through the Edge Function (service_role bypasses RLS). See the file for exact SQL.

## submit Edge Function
Deno; metadata via query params, raw `.gghost` bytes as the body. Validates `GGST` magic + size
(68 B ≤ n ≤ 250 KB), uploads to `gghosts/<level_id>/<uuid>.gghost` with the service_role client,
inserts a `runs` row (`legit_status='unverified'`), returns `{id, gghost_path}`. Whitelists `source`,
truncates `victor_name` to 31, cleans up the blob if the insert fails.

## API contract (for P4b)
- **List:** `GET {URL}/rest/v1/runs?level_id=eq.<id>&order=created_at.asc&select=*` (`apikey`+`Authorization: Bearer` = anon).
- **Download:** `GET {URL}/storage/v1/object/public/gghosts/<gghost_path>`.
- **Submit:** `POST {URL}/functions/v1/submit?level_id=..&victor_name=..&source=..&frame_count=..&duration_sec=..&format_version=..` (anon headers, `Content-Type: application/octet-stream`, body = bytes) → `{id, gghost_path}`.

## Rules & invariants
Anon read-only (RLS) · writes only via `submit` · validate GGST magic + ≤250 KB · service_role stays in
the function runtime · list ordered `created_at asc` (AC-05).

## Tasks
| ID | Task | File | Status |
|----|------|------|--------|
| T01 | `schema.sql` | `supabase/schema.sql` | done |
| T02 | `submit` Edge Function | `supabase/functions/submit/index.ts` | done |
| T03 | Dashboard walkthrough + curl tests | `docs/plans/phase4a-setup.md` | done |
| T04 | User: create project → run SQL → bucket → deploy fn → test → share URL+anon key | (dashboard) | pending |

- [x] T01 [x] T02 [x] T03 [x] T04

## Definition of Done
- [x] `runs` + enums + index + RLS created (P4a-AC1/AC3).
- [x] `gghosts` public bucket exists (P4a-AC2).
- [x] `submit` deployed; validates GGST+size; stores blob + inserts unverified row (P4a-AC4).
- [x] curl round-trip: submit → list (created_at asc) → download (P4a-AC5).
- [x] Project URL + anon key captured for P4b (P4a-AC6).
- (No `geode build`/in-game — backend phase.)

## Verification
✅ Live-tested via curl (2026-08-19): `submit` returned `{id, gghost_path}` for level 20808516 (8347 B
`.gghost`); the list endpoint returned the row upload-ascending with `source=human`,
`legit_status=unverified`, matching `file_size`; storage upload succeeded (public blob served).
Project: `https://hlrrsctbapvcffpiiupy.supabase.co`.

## Dev Agent Record — File List
| File | C/M | Role |
|------|-----|------|
| `supabase/schema.sql` | Created | table + enums + RLS |
| `supabase/functions/submit/index.ts` | Created | upload/validate/insert function |
| `docs/plans/phase4a-setup.md` | Created | dashboard setup guide |

**Not in scope:** mod client P4b, seeding P6, verification P7, AREDL.
