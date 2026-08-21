# P7 — Legit Verification & Moderation · Spec

> Gate 1 artifact (spec). Reviewed & approved. The last deferred sibling of the Victors Platform
> (`docs/plans/victors-platform.design.md` §6). Lets a player earn a **LEGIT** badge for a run by
> submitting a YouTube completion (+ raw footage when AREDL requires it) that Vikkie reviews by eye.

**Status:** approved · **Design:** `docs/plans/victors-platform.design.md` §6 · **Depends on:** P4a/P4b (P6 optional) · **No SRS AC** (platform extension; defines its own P7-AC IDs)

**Summary.** A player attaches a **YouTube completion** link (+ **Google-Drive raw footage** when the
level's AREDL entry requires it) to an uploaded run via an in-mod **"Request legit"** form. The request
stores the URLs on the run and leaves it `unverified` — that *is* the review queue. Vikkie compares
video↔run in the **Supabase dashboard** and flips `legit_status` to `legit` / `rejected` (+
`verified_by`/`verified_at`). Legit runs get a **LEGIT** badge and a **"legit only"** filter in the
Victors popup. **No account-ownership check** — the footage is the proof.

---

## 1. Acceptance criteria
| ID | Procedure | Expected |
|----|-----------|----------|
| P7-AC1 | On an online human run, tap **Request legit**, enter a YouTube URL, submit. | Server stores `youtube_url` (and `raw_footage_url` if given) on the row; `legit_status` stays `unverified`. |
| P7-AC2 | Request legit on a level whose AREDL `requires_raw_footage=true`, leaving raw footage empty. | Rejected — client blocks + server 400 (`raw footage required`). |
| P7-AC3 | In the Supabase dashboard set `legit_status='legit'` on that row. | The run shows a **LEGIT** badge in the Victors popup. |
| P7-AC4 | Toggle **"Legit only"** in the online list. | Only `legit` runs are listed; off → all runs shown, badged. |
| P7-AC5 | A row set to `legit_status='rejected'`. | Not badged legit; hidden under the "legit only" filter. |
| P7-AC6 | Bot rows / already-legit rows. | No **Request legit** button (bots are never legit; legit rows need no re-request). |

---

## 2. What we will build
- **Server — new Edge Function `request-legit`:** input = `run_id`, `youtube_url`, optional
  `raw_footage_url`. Validates the run exists; requires a non-empty YouTube URL; looks up the level's
  AREDL `requires_raw_footage` and **requires** raw footage when true. Writes the URLs via service_role;
  leaves `legit_status='unverified'`. (Anon key to call; RLS still blocks direct writes.)
- **Server — moderation = dashboard (no build).** Review queue = `runs` where `youtube_url is not null`
  and `legit_status='unverified'`. Approve/reject = `update … set legit_status=…, verified_by='Vikkie',
  verified_at=now()`. Exact SQL documented in the gate-2 plan.
- **Client — `NetworkManager`:** `requestLegit(runId, youtube, raw, cb)` (POST to the function);
  `fetchAredlMeta(levelID, cb)` → `{found, position, requiresRawFootage}` (generalizes P6's
  `fetchAredlRank`). `VictorMeta.legitStatus` is already parsed.
- **Client — `VictorsPopup`:** **Request legit** button on online **human, not-yet-legit** rows →
  opens a small **`LegitRequestPopup`** (two `geode::TextInput`s: YouTube + raw footage, the latter
  marked required when AREDL says so) → `requestLegit` → alert. **LEGIT** badge on `legit` rows. A
  **"Legit only"** filter toggle in the online section.

---

## 3. FR / NFR
Extends the platform (design §6). Non-blocking HTTP (worker thread + `queueInMainThread`, P4b pattern).
No gameplay perf impact. Footage is **linked, never hosted**.

---

## 4. Decisions
- **No account-ownership check** (user): footage review is the proof; anyone may request legit for any
  run; approval is manual. (Supersedes the earlier "evaluate Argon at P7" note — dropped for good.)
- **No new enum value:** a request keeps `legit_status='unverified'` + populated URLs = the pending
  queue. `legit`/`rejected` are set by the admin.
- **Raw footage required iff AREDL `requires_raw_footage`** — enforced **server-side** (the function
  looks it up) and mirrored client-side for UX.
- **Moderation via Supabase dashboard** in P7; an in-mod admin review UI is deferred (later, if volume
  grows) — mirrors P6's "defer complexity" call.
- **Request-legit shown on online `human` rows that aren't already `legit`.** Bots are never legit.
- ⚠ Gate-2: confirm `geode::TextInput` API for the two-field form; and whether the "legit only" filter
  is a header toggle vs. a segmented control.

**Out of scope:** in-mod admin review UI, rate limiting (deferred hardening → `CLAUDE.md` §9), any
account/identity proof (dropped), spectate (P5), hosting footage (always linked).
