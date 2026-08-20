-- Ghost Victors — Supabase schema (P4a)
-- Paste into the Supabase SQL Editor and Run. Idempotent-ish (guards where possible).
-- See docs/plans/victors-platform.design.md and docs/plans/phase4a-backend.plan.md.

-- ---- enums ----
do $$ begin
  create type run_source as enum ('human','macro','bot');
exception when duplicate_object then null; end $$;

do $$ begin
  create type legit_state as enum ('unverified','legit','rejected');
exception when duplicate_object then null; end $$;

-- ---- runs table ----
create table if not exists public.runs (
  id              uuid primary key default gen_random_uuid(),
  level_id        bigint not null,
  victor_name     text        not null default 'Anonymous',
  source          run_source  not null default 'human',
  legit_status    legit_state not null default 'unverified',  -- reserved; flipped in P7 (verification)
  youtube_url     text,                                        -- reserved (P7)
  raw_footage_url text,                                        -- reserved (P7)
  aredl_rank      int,                                         -- reserved (P6/P7)
  frame_count     int    not null default 0,
  duration_sec    int    not null default 0,
  format_version  int    not null default 1,
  gghost_path     text   not null,                             -- storage key in the `gghosts` bucket
  file_size       int    not null default 0,
  created_at      timestamptz not null default now(),          -- upload order = "First Victors First" (AC-05)
  verified_by     text,                                        -- reserved (P7)
  verified_at     timestamptz                                  -- reserved (P7)
);

-- fast "victors for this level, upload order"
create index if not exists runs_level_created_idx on public.runs (level_id, created_at);

-- ---- row level security ----
alter table public.runs enable row level security;

-- Anyone (anon/authenticated) may READ. There are deliberately NO write policies, so direct
-- inserts/updates/deletes with the anon key are denied. The `submit` Edge Function uses the
-- service_role key, which BYPASSES RLS, so it is the only writer.
drop policy if exists runs_public_read on public.runs;
create policy runs_public_read on public.runs for select using (true);

-- ---- storage ----
-- Create the `gghosts` bucket in the dashboard (Storage → New bucket → name: gghosts, Public).
-- No anon write policy is needed: the Edge Function writes via service_role. Public bucket = public read.
