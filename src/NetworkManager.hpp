#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

#include "DataTypes.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — Supabase client (P4b)
// Async by running a *synchronous* web request on a detached worker thread, then delivering the
// result on the main thread via geode::queueInMainThread (so callbacks can touch UI safely). This
// avoids the 5.9 arc-coroutine surface. Endpoints per docs/plans/phase4a-backend.plan.md §7.
// ==========================================

struct VictorMeta {
    std::string id;
    std::string victorName;
    std::string source;       // human | macro | bot
    std::string legitStatus;  // unverified | legit | rejected
    std::string gghostPath;   // storage key inside the gghosts bucket
    int levelId = 0;
    int frameCount = 0;
    int durationSec = 0;
};

// One AREDL main-list demon (P6 seeding). See docs/plans/phase6-seeding.plan.md / aredl-api memory.
struct AredlLevel {
    int position = 0;         // AREDL rank (1 = hardest) → stored as aredl_rank
    std::string name;
    int levelId = 0;          // the GD level id (matches GJGameLevel::m_levelID.value())
};

namespace NetworkManager {

    // AREDL is a separate public API (different host from our Supabase backend).
    inline constexpr const char* kAredlBase = "https://api.aredl.net/v2/api/aredl";

    inline std::string baseUrl() {
        auto s = Mod::get()->getSettingValue<std::string>("server-url");
        while (!s.empty() && s.back() == '/') s.pop_back();
        return s;
    }
    inline std::string anonKey() {
        return Mod::get()->getSettingValue<std::string>("server-anon-key");
    }
    inline bool enabled() {
        return Mod::get()->getSettingValue<bool>("enable-online")
            && !baseUrl().empty() && !anonKey().empty();
    }

    // ---- P6 seeding admin state (client side) ----
    inline bool seedingMode() { return Mod::get()->getSettingValue<bool>("seeding-mode"); }
    inline std::string adminKey() { return Mod::get()->getSettingValue<std::string>("admin-key"); }
    // Client gate for SHOWING seeding UI. The real gate is the server's key check (submit fn).
    inline bool isAdmin() { return seedingMode() && !adminKey().empty(); }

    // GET a level's victors, upload-ascending (AC-05). Callback fires on the main thread.
    inline void fetchVictors(int levelID, std::function<void(bool, std::vector<VictorMeta>)> cb) {
        if (!enabled()) { cb(false, {}); return; }
        const std::string url = baseUrl() + "/rest/v1/runs?level_id=eq." + std::to_string(levelID)
                              + "&order=created_at.asc&select=*";
        const std::string key = anonKey();
        std::thread([url, key, cb]() {
            web::WebResponse res = web::WebRequest()
                .header("apikey", key)
                .header("Authorization", "Bearer " + key)
                .getSync(url);

            bool ok = res.ok();
            std::vector<VictorMeta> out;
            if (ok) {
                auto parsed = res.json();
                if (parsed) {
                    auto arr = std::move(parsed).unwrap();
                    if (arr.isArray()) {
                        for (auto const& o : arr) {
                            VictorMeta m;
                            m.id          = o["id"].asString().unwrapOr(std::string(""));
                            m.victorName  = o["victor_name"].asString().unwrapOr(std::string("?"));
                            m.source      = o["source"].asString().unwrapOr(std::string("human"));
                            m.legitStatus = o["legit_status"].asString().unwrapOr(std::string("unverified"));
                            m.gghostPath  = o["gghost_path"].asString().unwrapOr(std::string(""));
                            m.levelId     = static_cast<int>(o["level_id"].asInt().unwrapOr(0));
                            m.frameCount  = static_cast<int>(o["frame_count"].asInt().unwrapOr(0));
                            m.durationSec = static_cast<int>(o["duration_sec"].asInt().unwrapOr(0));
                            out.push_back(std::move(m));
                        }
                    }
                } else {
                    ok = false;
                }
            }
            queueInMainThread([cb, ok, out = std::move(out)]() mutable {
                cb(ok, std::move(out));
            });
        }).detach();
    }

    // Download a public blob to `dest` (skips if already cached → AC-07). Callback on the main thread.
    inline void downloadGhost(std::string gghostPath, std::filesystem::path dest,
                              std::function<void(bool)> cb) {
        std::error_code ec0;
        if (std::filesystem::exists(dest, ec0)) { cb(true); return; } // cache hit
        if (!enabled()) { cb(false); return; }
        const std::string url = baseUrl() + "/storage/v1/object/public/gghosts/" + gghostPath;
        const std::string key = anonKey();
        std::thread([url, key, dest, cb]() {
            web::WebResponse res = web::WebRequest().header("apikey", key).getSync(url);
            bool ok = res.ok();
            if (ok) {
                std::error_code ec;
                std::filesystem::create_directories(dest.parent_path(), ec);
                ByteVector const& bytes = res.data();
                std::ofstream f(dest, std::ios::binary | std::ios::trunc);
                if (f.is_open() && !bytes.empty()) {
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
                    f.flush();
                    ok = f.good();
                } else {
                    ok = false;
                }
            }
            queueInMainThread([cb, ok]() { cb(ok); });
        }).detach();
    }

    // POST a local .gghost to the submit Edge Function. Callback on the main thread.
    // `source` is "human" (default) or "bot" (P6 seeding); `aredlRank` (>0) stamps aredl_rank.
    // The admin key (if set) is sent as x-admin-key; the server verifies it before honoring source=bot.
    inline void submitRun(int levelID, ReplayHeader header, int frameCount,
                          std::filesystem::path file, std::function<void(bool, std::string)> cb,
                          std::string source = "human", int aredlRank = 0) {
        if (!enabled()) { cb(false, "online disabled"); return; }

        std::ifstream in(file, std::ios::binary);
        if (!in.is_open()) { cb(false, "cannot read file"); return; }
        ByteVector bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        in.close();
        if (bytes.empty()) { cb(false, "empty file"); return; }

        const std::string victor(header.victorName,
                                 strnlen(header.victorName, sizeof(header.victorName)));
        const int durationSec = frameCount / 60;
        const int fmtVer = static_cast<int>(header.formatVersion);
        const std::string endpoint = baseUrl() + "/functions/v1/submit";
        const std::string key = anonKey();
        const std::string admin = adminKey();  // "" for normal players

        std::thread([endpoint, key, admin, levelID, victor, source, aredlRank, frameCount,
                     durationSec, fmtVer, bytes = std::move(bytes), cb]() mutable {
            auto req = web::WebRequest();
            req.header("apikey", key)
               .header("Authorization", "Bearer " + key)
               .header("Content-Type", "application/octet-stream");
            if (!admin.empty()) req.header("x-admin-key", admin);
            req.param("level_id", levelID)                // .param() URL-encodes the query values
               .param("victor_name", victor)
               .param("source", source)
               .param("frame_count", frameCount)
               .param("duration_sec", durationSec)
               .param("format_version", fmtVer);
            if (aredlRank > 0) req.param("aredl_rank", aredlRank);

            web::WebResponse res = req.body(std::move(bytes)).postSync(endpoint);

            const bool ok = res.ok();
            std::string err;
            if (!ok) err = "HTTP " + std::to_string(res.code()) + ": " + res.string().unwrapOr(std::string(""));
            queueInMainThread([cb, ok, err]() { cb(ok, err); });
        }).detach();
    }

    // ---- AREDL + seeding-target helpers (P6) ----

    // Fetch the AREDL main list (status == "MainList"), sorted by the API's own order (position asc).
    // Independent of our Supabase backend — plain GET, no apikey. Callback on the main thread.
    inline void fetchAredlList(std::function<void(bool, std::vector<AredlLevel>)> cb) {
        const std::string url = std::string(kAredlBase) + "/levels";
        std::thread([url, cb]() {
            web::WebResponse res = web::WebRequest().getSync(url);
            bool ok = res.ok();
            std::vector<AredlLevel> out;
            if (ok) {
                auto parsed = res.json();
                if (parsed) {
                    auto arr = std::move(parsed).unwrap();
                    if (arr.isArray()) {
                        for (auto const& o : arr) {
                            if (o["status"].asString().unwrapOr(std::string("")) != "MainList") continue;
                            AredlLevel l;
                            l.position = static_cast<int>(o["position"].asInt().unwrapOr(0));
                            l.name     = o["name"].asString().unwrapOr(std::string("?"));
                            l.levelId  = static_cast<int>(o["level_id"].asInt().unwrapOr(0));
                            if (l.levelId > 0) out.push_back(std::move(l));
                        }
                    }
                } else {
                    ok = false;
                }
            }
            queueInMainThread([cb, ok, out = std::move(out)]() mutable { cb(ok, std::move(out)); });
        }).detach();
    }

    // Which level_ids already have a bot seed (source=bot). One query to our backend.
    inline void fetchSeededLevelIds(std::function<void(bool, std::vector<int>)> cb) {
        if (!enabled()) { cb(false, {}); return; }
        const std::string url = baseUrl() + "/rest/v1/runs?source=eq.bot&select=level_id";
        const std::string key = anonKey();
        std::thread([url, key, cb]() {
            web::WebResponse res = web::WebRequest()
                .header("apikey", key)
                .header("Authorization", "Bearer " + key)
                .getSync(url);
            bool ok = res.ok();
            std::vector<int> out;
            if (ok) {
                auto parsed = res.json();
                if (parsed) {
                    auto arr = std::move(parsed).unwrap();
                    if (arr.isArray())
                        for (auto const& o : arr)
                            out.push_back(static_cast<int>(o["level_id"].asInt().unwrapOr(0)));
                } else {
                    ok = false;
                }
            }
            queueInMainThread([cb, ok, out = std::move(out)]() mutable { cb(ok, std::move(out)); });
        }).detach();
    }

    // Look up a single level's AREDL rank (position). cb(found, rank). Not-on-list → cb(false, 0).
    inline void fetchAredlRank(int levelID, std::function<void(bool, int)> cb) {
        const std::string url = std::string(kAredlBase) + "/levels/" + std::to_string(levelID);
        std::thread([url, cb]() {
            web::WebResponse res = web::WebRequest().getSync(url);
            int rank = 0;
            if (res.ok()) {
                auto parsed = res.json();
                if (parsed) {
                    auto obj = std::move(parsed).unwrap();
                    rank = static_cast<int>(obj["position"].asInt().unwrapOr(0));
                }
            }
            const bool found = rank > 0;
            queueInMainThread([cb, found, rank]() { cb(found, rank); });
        }).detach();
    }

} // namespace NetworkManager
