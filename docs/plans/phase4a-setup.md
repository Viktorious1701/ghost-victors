# P4a — Supabase setup walkthrough

Follow these once to stand up the backend. It takes ~15 min and needs no local tooling (dashboard-only).
Files referenced: `supabase/schema.sql`, `supabase/functions/submit/index.ts`.

> 🔐 **Key safety:** the mod only ever uses the **anon** key. The **service_role** key is secret — it
> stays in the Supabase dashboard / the function runtime. Never put service_role in the mod, the repo,
> or a chat.

## 1. Create the project
1. Sign up at <https://supabase.com> → **New project**.
2. Pick an **org**, a **name** (e.g. `ghost-victors`), set a **database password** (save it), choose a
   **region** near your players → **Create new project**. Wait ~2 min for provisioning.

## 2. Copy your keys
1. Left sidebar → **Project Settings → API**.
2. Copy the **Project URL** (e.g. `https://abcxyz.supabase.co`).
3. Copy the **anon `public`** key (a long JWT). *(Leave `service_role` alone — it's secret.)*
4. Keep both handy — you'll paste them to me at the end for P4b.

## 3. Create the table (SQL)
1. Left sidebar → **SQL Editor → New query**.
2. Open `supabase/schema.sql` from the repo, copy all of it, paste into the editor.
3. Click **Run**. You should see "Success".
4. Check **Table Editor → `runs`** exists with the columns.

## 4. Create the storage bucket
1. Left sidebar → **Storage → New bucket**.
2. Name: **`gghosts`** (exactly). Toggle **Public bucket** ON. → **Create bucket**.

## 5. Deploy the `submit` function
1. Left sidebar → **Edge Functions → Create a function** (a.k.a. "Deploy via Editor").
2. Name it **`submit`** (exactly).
3. Replace the template with the contents of `supabase/functions/submit/index.ts`. → **Deploy**.
   - ⚠ If your dashboard has no in-browser function editor, use the CLI instead:
     `supabase login` → `supabase link --project-ref <ref>` → `supabase functions deploy submit`.
   - The function auto-receives `SUPABASE_URL` and `SUPABASE_SERVICE_ROLE_KEY` — no secrets to set.

## 6. Test the round-trip (curl)
Replace `<URL>` and `<ANON>` with your values. For the file, use any real `.gghost` you've recorded
(e.g. from `%LocalAppData%\GeometryDash\geode\mods\vikkie.ghost-victors\replays\...`).

**Submit:**
```bash
curl -X POST "https://hlrrsctbapvcffpiiupy.supabase.co/functions/v1/submit?level_id=12345&victor_name=Tester&source=human&frame_count=100&duration_sec=2&format_version=1" \
  -H "apikey: eyJ... -H "Authorization: Bearer eyJ..." \
  -H "Content-Type: application/octet-stream" \
  --data-binary "@/path/to/some.gghost"
# → {"id":"...","gghost_path":"12345/....gghost"}1
```

**List (should be upload-ascending):**
```bash
curl "<URL>/rest/v1/runs?level_id=eq.12345&order=created_at.asc&select=*" \
  -H "apikey: <ANON>" -H "Authorization: Bearer <ANON>"
# → [{ ... "legit_status":"unverified" ... }]
```

**Download (open in a browser or curl):**
```
<URL>/storage/v1/object/public/gghosts/<gghost_path>
```

## 7. Hand off
Paste your **Project URL** + **anon key** back to me. I'll wire them into `mod.json` settings and start
**P4b** (the mod fetching/downloading/uploading against this).

### Done when
- `runs` table + `gghosts` public bucket exist · `submit` deployed · the three curls all succeed ·
  the listed row is `legit_status: unverified` · the blob downloads.
