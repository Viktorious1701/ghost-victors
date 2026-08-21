# P6 — Seeding ("Bot Vikkie") · Spec

> Gate 1 artifact (spec). Reviewed & approved. Seeds hard levels (extreme demons) with admin-uploaded
> **Bot Vikkie** ghosts, integrates **AREDL** to know what to seed, and enforces admin identity
> **server-side**. Part of the Victors Platform (`docs/plans/victors-platform.design.md` §5).

**Status:** approved · **Depends on:** P4a/P4b · **Deferred siblings:** P7 (legit verification), P5 (spectate)

**AREDL API (confirmed against live responses):** `GET https://api.aredl.net/v2/api/aredl/levels` →
JSON array of `{id, name, position, level_id, points, status, requires_raw_footage, tags, …}`
(`position` 1 = hardest; `level_id` = the GD level id (int); **main list = `status == "MainList"`** —
there is no `legacy` field). Single level: `GET .../aredl/levels/{level_id}` (also carries `position`).

**Summary.** A seeding-mode toggle switches the client into bot-upload intent; the `submit` Edge Function
**verifies a private admin key** before it will store a run as `source=bot` / `victor_name="Bot Vikkie"`.
AREDL is fetched to show which main-list demons still need a bot ghost and to stamp `aredl_rank`.
Recording is unchanged — play a Normal-mode macro 0→100%, it records, upload it as a bot seed. Ongoing:
the admin capability stays permanently, inert for anyone without the key.

---

## 1. Acceptance criteria
| ID | Procedure | Expected |
|----|-----------|----------|
| P6-AC1 | Seeding mode ON **with a valid admin key**, Upload a run. | Server stores it as `source=bot`, `victor_name="Bot Vikkie"`; shows the BOT badge online. |
| P6-AC2 | Bot upload **without** a valid admin key. | Server **rejects the bot label** (refuse, or accept only as `source=human`) — no fake official seeds. |
| P6-AC3 | Play a Normal-mode **macro** 0→100%. | Records a `.gghost` (existing engine, no gate change) → uploadable as a bot seed. |
| P6-AC4 | Open the **Seeding targets** view. | Lists AREDL main-list demons (`status == "MainList"`) with a **seeded/unseeded** mark (a `source=bot` run exists on the server or not). |
| P6-AC5 | Upload a seed for an AREDL level. | `aredl_rank` (= AREDL `position`) is stored on the row. |
| P6-AC6 | Normal player (empty admin key). | No behavior change; **seeding UI never renders** (gated on non-empty key); can't post bot seeds. |

---

## 2. What we will build

**Two gates, distinct jobs:**
- **Security gate (server) — the lock:** `submit` verifies the `admin-key` against the `ADMIN_KEY`
  secret. Sole authority for storing `source=bot` / the "Bot Vikkie" name.
- **UI-visibility gate (client) — UX only:** the seeding UI (Seeding-targets popup + bot-upload path)
  renders **only when the `admin-key` setting is non-empty**. Normal users ship empty → they never see
  any seeding UI. (Not a security boundary — the server gate is; this just keeps the UI clean.)

- **`mod.json`:** `seeding-mode` bool (default **off**) + `admin-key` string (default **empty**; NOT
  shipped with a value — only Vikkie fills it in privately).
- **Upload path (extends P4b):** when seeding-mode ON → submit with `source=bot`,
  `victor_name="Bot Vikkie"`, `aredl_rank`, **and the admin key**; else unchanged (human, no key).
- **`submit` Edge Function (server):** verify the admin key against a Supabase secret **`ADMIN_KEY`**
  before honoring `source=bot`/the reserved name; accept + insert `aredl_rank`; **reserve the name
  "Bot Vikkie"** (reject non-admin uploads using it). Change + redeploy.
- **AREDL client:** `GET .../aredl/levels` (worker thread + matjson; `position`/`name`/`level_id`, main
  list = `status == "MainList"`) → a **Seeding targets popup** listing demons + **seeded?** (cross-ref
  our `runs` where `source=bot`). Entry point shown only in seeding mode.

---

## 3. FR / NFR
Extends FR-4.x (upload/list) with an admin-gated bot source + AREDL metadata. Non-blocking fetch
(worker thread, like P4b). No new gameplay perf impact.

---

## 4. Decisions
- **Admin identity is server-enforced:** bot uploads require the `admin-key`, verified by `submit`
  against the `ADMIN_KEY` Supabase secret. The client toggle alone is NOT trusted. Key is empty in the
  public build; Vikkie sets it privately. Capability stays permanently (ongoing seeding), inert without
  the key.
- **Seeding-mode setting** just switches the client into bot-upload intent; the server is the gate.
- **Normal-mode macros only** — no practice capture, no recording-gate change.
- **AREDL fetched client-side** (mod → AREDL API); `aredl_rank` = AREDL `position`; main list filters
  `status == "MainList"`. (Server-side `levels` cache is a later optimization.)
- **Seeded = a `runs` row with `source=bot` exists for that `level_id`.**
- ⚠ Confirm the Seeding-targets popup entry point (e.g. a button in the Victors popup / main menu, shown
  only when seeding mode is on) at gate 2.

**Identity note (resolved at P7 — no account check).** The uploaded `victor_name` is a client-chosen
string the server trusts blindly, and GD username uniqueness doesn't make it authentic. This is
**intentionally accepted**: names are display/attribution only, and P7 legit status is earned by a
human review of submitted **YouTube + raw footage** (the footage is the proof), so faking a name never
yields legit. No GD-account-ownership check is built in P6 **or** P7 (the earlier "evaluate Argon"
idea was dropped). See `docs/plans/phase7-verification.spec.md`.

**Out of scope:** legit verification/filter (P7), practice/StartPos capture, AREDL server-side caching,
spectate (P5).
