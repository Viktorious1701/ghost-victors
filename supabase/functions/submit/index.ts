// Ghost Victors — run submission Edge Function (P4a)
//
// The mod calls this with the ANON key. It validates the .gghost blob and writes using the
// SERVICE_ROLE key (auto-injected into the function runtime — never shipped in the mod).
//
// Request:  POST /functions/v1/submit?level_id=..&victor_name=..&source=..&frame_count=..
//                 &duration_sec=..&format_version=..&aredl_rank=..(optional)
//           headers: apikey + Authorization: Bearer <anon>, Content-Type: application/octet-stream
//                    x-admin-key: <secret> (P6 — required to seed source=bot / the "Bot Vikkie" name)
//           body: raw .gghost bytes
// Response: 200 { id, gghost_path }  |  403 { error } (bot seed without a valid admin key)  |  4xx/5xx { error }

import { createClient } from "https://esm.sh/@supabase/supabase-js@2";

const MAX_BYTES = 250 * 1024;                 // NFR-2
const MIN_BYTES = 68;                          // at least a ReplayHeader
const GGST = [0x47, 0x47, 0x53, 0x54];         // "GGST"
const ALLOWED_SOURCE = new Set(["human", "macro", "bot"]);

const cors = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Headers": "authorization, apikey, content-type, x-admin-key",
  "Access-Control-Allow-Methods": "POST, OPTIONS",
};

const RESERVED_NAME = "bot vikkie"; // reserved for admin seeds (case-insensitive)

const json = (body: unknown, status = 200) =>
  new Response(JSON.stringify(body), {
    status,
    headers: { ...cors, "Content-Type": "application/json" },
  });

Deno.serve(async (req) => {
  if (req.method === "OPTIONS") return new Response("ok", { headers: cors });
  if (req.method !== "POST") return json({ error: "POST only" }, 405);

  const q = new URL(req.url).searchParams;

  const levelId = Number(q.get("level_id"));
  if (!Number.isFinite(levelId) || levelId <= 0) return json({ error: "bad level_id" }, 400);

  const victorName = (q.get("victor_name") || "Anonymous").slice(0, 31);
  let source = (q.get("source") || "human").toLowerCase();
  if (!ALLOWED_SOURCE.has(source)) source = "human";
  const frameCount = Number(q.get("frame_count") ?? "0") | 0;
  const durationSec = Number(q.get("duration_sec") ?? "0") | 0;
  const formatVersion = Number(q.get("format_version") ?? "1") | 0;

  // aredl_rank (P6): optional; only stored when a positive int is provided.
  const rawRank = Number(q.get("aredl_rank"));
  const aredlRank = Number.isFinite(rawRank) && rawRank > 0 ? (rawRank | 0) : null;

  // Admin gate (P6): the mod sends x-admin-key; the ADMIN_KEY secret is the authority. Seeding as a
  // bot OR using the reserved "Bot Vikkie" name requires a valid key — otherwise reject (no silent
  // downgrade). Normal human uploads are unaffected.
  const adminKey = req.headers.get("x-admin-key") ?? "";
  const serverKey = Deno.env.get("ADMIN_KEY") ?? "";
  const isAdmin = serverKey.length > 0 && adminKey === serverKey;
  const wantsBot = source === "bot" || victorName.trim().toLowerCase() === RESERVED_NAME;
  if (wantsBot && !isAdmin) return json({ error: "admin key required for bot seeds" }, 403);

  const bytes = new Uint8Array(await req.arrayBuffer());
  if (bytes.length < MIN_BYTES || bytes.length > MAX_BYTES) return json({ error: "bad size" }, 400);
  if (bytes[0] !== GGST[0] || bytes[1] !== GGST[1] || bytes[2] !== GGST[2] || bytes[3] !== GGST[3])
    return json({ error: "bad magic (not a .gghost)" }, 400);

  const supabase = createClient(
    Deno.env.get("SUPABASE_URL")!,
    Deno.env.get("SUPABASE_SERVICE_ROLE_KEY")!,
  );

  const id = crypto.randomUUID();
  const path = `${levelId}/${id}.gghost`;

  const up = await supabase.storage.from("gghosts").upload(path, bytes, {
    contentType: "application/octet-stream",
    upsert: false,
  });
  if (up.error) return json({ error: "storage: " + up.error.message }, 500);

  const ins = await supabase
    .from("runs")
    .insert({
      id,
      level_id: levelId,
      victor_name: victorName,
      source,
      frame_count: frameCount,
      duration_sec: durationSec,
      format_version: formatVersion,
      aredl_rank: aredlRank,
      gghost_path: path,
      file_size: bytes.length,
    })
    .select("id, gghost_path")
    .single();

  if (ins.error) {
    await supabase.storage.from("gghosts").remove([path]); // best-effort cleanup
    return json({ error: "db: " + ins.error.message }, 500);
  }

  return json(ins.data, 200);
});
