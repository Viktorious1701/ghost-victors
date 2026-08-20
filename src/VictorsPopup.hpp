#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

#include "DataTypes.hpp"
#include "GhostManager.hpp"
#include "ReplaySerializer.hpp"
#include "NetworkManager.hpp"
#include "SeedingTargetsPopup.hpp"

#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — Victors popup (Phase 3 + P4b)
// Online section (server victors, upload order, Race=download+select) + Your runs (local, Race + Upload).
// Geode 5.9 Popup is a non-template FLAlertLayer subclass (init(w,h), m_mainLayer, m_size).
// ==========================================

class VictorsPopup : public Popup {
protected:
    enum class Online { Disabled, Loading, Loaded, Failed };

    int m_levelID = 0;
    float m_listW = 0.f;
    ScrollLayer* m_scroll = nullptr;

    bool initPopup(int levelID) {
        if (!Popup::init(380.f, 260.f)) return false;
        m_levelID = levelID;
        this->setTitle("Victors");

        const CCSize listSize = {m_size.width - 40.f, m_size.height - 55.f};
        m_listW = listSize.width;
        m_scroll = ScrollLayer::create(listSize);
        m_scroll->setPosition((m_size - listSize) / 2.f);
        m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout());
        m_mainLayer->addChild(m_scroll);

        // Admin-only: a "Seeding targets" button (shown only when a seeding admin key is set).
        if (NetworkManager::isAdmin()) {
            auto menu = CCMenu::create();
            auto spr = ButtonSprite::create("Seeding targets", "bigFont.fnt", "GJ_button_04.png", 0.6f);
            spr->setScale(0.5f);
            auto btn = CCMenuItemExt::createSpriteExtra(spr, [](CCMenuItemSpriteExtra*) {
                if (auto p = SeedingTargetsPopup::create()) p->show();
            });
            menu->addChild(btn);
            menu->setPosition(m_size.width / 2.f, 15.f);  // below the scroll list (bottom strip)
            m_mainLayer->addChild(menu);
        }

        if (NetworkManager::enabled()) {
            rebuild(Online::Loading, {});
            Ref<VictorsPopup> self = this;
            NetworkManager::fetchVictors(levelID, [self](bool ok, std::vector<VictorMeta> list) {
                if (!self->getParent()) return; // popup was closed before the response arrived
                self->rebuild(ok ? Online::Loaded : Online::Failed, list);
            });
        } else {
            rebuild(Online::Disabled, {});
        }
        return true;
    }

    void rebuild(Online state, std::vector<VictorMeta> const& online) {
        auto content = m_scroll->m_contentLayer;
        content->removeAllChildren();

        // ---- Online ----
        if (state != Online::Disabled) {
            addLabelRow("-- Online --");
            if (state == Online::Loading) {
                addLabelRow("Loading...");
            } else if (state == Online::Loaded) {
                if (online.empty()) addLabelRow("No online victors yet");
                for (auto const& v : online) addOnlineRow(v);
            } else { // Failed
                addLabelRow("(offline - showing downloaded)");
                addCachedRows();
            }
        }

        // ---- Local ----
        addLabelRow("-- Your runs (local) --");
        addSelectRow("None (no ghost)", "", "Off");
        for (auto const& p : localRuns()) addLocalRow(p);

        content->updateLayout();
    }

    // ---------- data helpers ----------
    std::vector<std::filesystem::path> localRuns() {
        std::vector<std::filesystem::path> out;
        const auto dir = Mod::get()->getSaveDir() / "replays" / std::to_string(m_levelID);
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (e.is_regular_file() && e.path().extension() == ".gghost") out.push_back(e.path());
            }
        }
        return out;
    }

    static std::string labelFor(std::filesystem::path const& p) {
        ReplayHeader h; std::vector<FrameData> f;
        if (ReplaySerializer::loadFromFile(p, h, f))
            return std::string(h.victorName[0] ? h.victorName : "?") + " - " +
                   std::to_string(static_cast<int>(f.size()) / 60) + "s";
        return p.filename().string();
    }

    // ---------- row builders ----------
    CCMenu* newRow(float h = 30.f) {
        auto row = CCMenu::create();
        row->setContentSize({m_listW, h});
        return row;
    }
    void addLabelRow(std::string const& text) {
        auto row = CCNode::create();
        row->setContentSize({m_listW, 22.f});
        auto label = CCLabelBMFont::create(text.c_str(), "goldFont.fnt");
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({6.f, 11.f});
        row->addChild(label);
        m_scroll->m_contentLayer->addChild(row);
    }
    void rowLabel(CCMenu* row, std::string const& text) {
        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(0.45f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({6.f, 15.f});
        row->addChild(label);
    }
    CCMenuItemSpriteExtra* rowButton(CCMenu* row, char const* text, float xFromRight,
                                     std::function<void(CCMenuItemSpriteExtra*)> cb) {
        auto spr = ButtonSprite::create(text, "bigFont.fnt", "GJ_button_01.png", 0.6f);
        spr->setScale(0.55f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, std::move(cb));
        btn->setAnchorPoint({1.f, 0.5f});
        btn->setPosition({m_listW - xFromRight, 15.f});
        row->addChild(btn);
        return btn;
    }

    // A row that selects `path` directly (local run, cached remote, or "" = none) then closes.
    void addSelectRow(std::string const& text, std::string const& path, char const* btnText) {
        auto row = newRow();
        rowLabel(row, text);
        const int lvl = m_levelID;
        Ref<VictorsPopup> self = this;
        rowButton(row, btnText, 6.f, [self, lvl, path](CCMenuItemSpriteExtra*) {
            GhostManager::get().setSelectedRun(lvl, path);
            self->onClose(nullptr);
        });
        m_scroll->m_contentLayer->addChild(row);
    }

    // Local run: Race (select directly) + Upload (submit to server).
    void addLocalRow(std::filesystem::path const& path) {
        auto row = newRow();
        rowLabel(row, labelFor(path));
        const int lvl = m_levelID;
        Ref<VictorsPopup> self = this;
        const std::string pathStr = path.string();
        rowButton(row, "Race", 6.f, [self, lvl, pathStr](CCMenuItemSpriteExtra*) {
            GhostManager::get().setSelectedRun(lvl, pathStr);
            self->onClose(nullptr);
        });
        rowButton(row, NetworkManager::isAdmin() ? "Seed" : "Upload", 60.f,
                  [self, lvl, pathStr](CCMenuItemSpriteExtra*) {
            ReplayHeader h; std::vector<FrameData> f;
            if (!ReplaySerializer::loadFromFile(pathStr, h, f)) {
                FLAlertLayer::create("Ghost Victors", "Could not read this run.", "OK")->show();
                return;
            }
            const int fc = static_cast<int>(f.size());
            const bool asBot = NetworkManager::isAdmin();

            // fires after the upload completes; refreshes the online list on success.
            std::function<void(bool, std::string)> done =
                [self, lvl, asBot](bool ok, std::string err) {
                    FLAlertLayer::create("Ghost Victors",
                        ok ? (asBot ? "Seeded as Bot Vikkie!" : "Uploaded!")
                           : ("Upload failed: " + err), "OK")->show();
                    if (ok && self->getParent()) {
                        NetworkManager::fetchVictors(lvl, [self](bool ok2, std::vector<VictorMeta> list) {
                            if (self->getParent()) self->rebuild(ok2 ? Online::Loaded : Online::Failed, list);
                        });
                    }
                };

            if (asBot) {
                // Seed as "Bot Vikkie": override the name, look up the AREDL rank, upload as source=bot.
                ReplayHeader hb = h;
                std::memset(hb.victorName, 0, sizeof(hb.victorName));
                std::strncpy(hb.victorName, "Bot Vikkie", sizeof(hb.victorName) - 1);
                NetworkManager::fetchAredlRank(lvl, [lvl, hb, fc, pathStr, done](bool, int rank) {
                    NetworkManager::submitRun(lvl, hb, fc, pathStr, done, "bot", rank);
                });
            } else {
                NetworkManager::submitRun(lvl, h, fc, pathStr, done);
            }
        });
        m_scroll->m_contentLayer->addChild(row);
    }

    // Online victor: Race downloads (or uses cache) then selects.
    void addOnlineRow(VictorMeta const& v) {
        auto row = newRow();
        const std::string tag = (v.source == "bot") ? " (BOT)" : "";
        rowLabel(row, v.victorName + tag + " - " + std::to_string(v.durationSec) + "s");
        const int lvl = m_levelID;
        Ref<VictorsPopup> self = this;
        const std::string gpath = v.gghostPath;
        rowButton(row, "Race", 6.f, [self, lvl, gpath](CCMenuItemSpriteExtra*) {
            const std::string base = gpath.substr(gpath.find_last_of('/') + 1);
            const auto dest = Mod::get()->getSaveDir() / "replays" / std::to_string(lvl) / "remote" / base;
            NetworkManager::downloadGhost(gpath, dest, [self, lvl, dest](bool ok) {
                if (ok) {
                    GhostManager::get().setSelectedRun(lvl, dest.string());
                    if (self->getParent()) self->onClose(nullptr);
                    FLAlertLayer::create("Ghost Victors", "Ghost selected - play the level to race it!", "OK")->show();
                } else {
                    FLAlertLayer::create("Ghost Victors", "Download failed.", "OK")->show();
                }
            });
        });
        m_scroll->m_contentLayer->addChild(row);
    }

    // Offline fallback: already-downloaded remote ghosts still selectable.
    void addCachedRows() {
        const auto dir = Mod::get()->getSaveDir() / "replays" / std::to_string(m_levelID) / "remote";
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec)) return;
        for (auto const& e : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!e.is_regular_file() || e.path().extension() != ".gghost") continue;
            addSelectRow(labelFor(e.path()) + " (cached)", e.path().string(), "Race");
        }
    }

public:
    static VictorsPopup* create(int levelID) {
        auto ret = new VictorsPopup();
        if (ret && ret->initPopup(levelID)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};
