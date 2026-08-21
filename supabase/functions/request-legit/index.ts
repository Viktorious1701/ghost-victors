// Ghost Victors — legit-request Edge Function (P7)
//
// A player attaches a YouTube completion (+ Google-Drive raw footage) to an existing run so Vikkie can
// review it. The mod calls this with the ANON key; the function writes via SERVICE_ROLE (never shipped).
// It does NOT set legit_status — the row stays 'unverified' and is reviewed manually in the dashboard.
//
// Request:  POST /functions/v1/request-legit?run_id=..&youtube_url=..&raw_footage_url=..(optional)
//           headers: apikey + Authorization: Bearer <anon>   (query params, like submit; no body)
// Response: 200 { ok:true } | 400 { error } (bad input / raw footage required) | 404 { error } | 5xx

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const AREDL_BASE = "https://api.aredl.net/v2/api/aredl";
const MAX_URL = 500;

const cors = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, apikey, content-type",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const json = (body: unknown, status = 200) =>
  new Response(JSON.stringify(body), {
    status,
    headers: { ...cors, "Content-Type": "application/json" },
  });

const isHttpUrl = (s: string) => /^https?:\/\/\S+$/i.test(s);

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: cors });
  if (req.method !== "POST") return json({ error: "POST only" }, 405);

  const q = new URL(req.url).searchParams;
  const runId = (q.get("run_id") ?? "").trim();
  const youtube = (q.get("youtube_url") ?? "").trim();
  const raw = (q.get("raw_footage_url") ?? "").trim();

  if (!runId) return json({ error: "missing run_id" }, 400);
  if (!youtube || !isHttpUrl(youtube) || youtube.length > MAX_URL)
    return json({ error: "a valid YouTube URL is required" }, 400);
  if (raw && (!isHttpUrl(raw) || raw.length > MAX_URL))
    return json({ error: "raw footage must be a valid URL" }, 400);

  const supabase = createClient(
    Deno.env.get("SUPABASE_URL")!,
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
  );

  // The run must exist; bots are never legit.
  const run = await supabase
    .from("runs")
    .select("id, level_id, source")
    .eq("id", runId)
    .single();
  if (run.error || !run.data) return json({ error: "run not found" }, 404);
  if (run.data.source === "bot") return json({ error: "bot runs cannot be legit" }, 400);

  // Raw footage is required iff the level's AREDL entry says so. If AREDL can't be reached or the level
  // isn't listed, we don't block (manual review is the backstop) — YouTube is always required above.
  let requiresRaw = false;
  try {
    const r = await fetch(`${AREDL_BASE}/levels/${run.data.level_id}`);
    if (r.ok) {
      const lvl = await r.json();
      requiresRaw = lvl?.requires_raw_footage === true;
    }
  } catch {
    requiresRaw = false;
  }
  if (requiresRaw && !raw)
    return json({ error: "this level requires raw footage (Google Drive link)" }, 400);

  const upd = await supabase
    .from("runs")
    .update({ youtube_url: youtube, raw_footage_url: raw || null }) // legit_status left 'unverified'
    .eq("id", runId)
    .select("id")
    .single();
  if (upd.error) return json({ error: "db: " + upd.error.message }, 500);

  return json({ ok: true }, 200);
});
