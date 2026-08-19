#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/utils/cocos.hpp>
#include <Geode/binding/ButtonSprite.hpp>

#include "DataTypes.hpp"
#include "GhostManager.hpp"
#include "ReplaySerializer.hpp"

#include <filesystem>
#include <string>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — Victors popup (Phase 3, local runs picker)
// Lists replays/<levelID>/*.gghost and lets the player choose which run to race,
// or "None" to disable the ghost. Selection is stored in GhostManager (DP16).
//
// NOTE: Geode 5.9.0's geode::Popup is a plain (non-template) FLAlertLayer subclass with a protected
// init(width, height, ...); there is no Popup<Args>/setup()/initAnchored(). Content goes on m_mainLayer.
// ==========================================

class VictorsPopup : public Popup {
protected:
    int m_levelID = 0;

    bool initPopup(int levelID) {
        if (!Popup::init(360.f, 240.f)) return false;
        m_levelID = levelID;
        this->setTitle("Victors");

        const CCSize listSize = {m_size.width - 40.f, m_size.height - 60.f};
        auto scroll = ScrollLayer::create(listSize);
        scroll->setPosition((m_size - listSize) / 2.f);
        scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout());
        m_mainLayer->addChild(scroll);

        // "None" row — disable the ghost for this level.
        addActionRow(scroll->m_contentLayer, listSize.width, "None (no ghost)", "", true);

        // One row per saved run.
        int count = 0;
        const auto dir = Mod::get()->getSaveDir() / "replays" / std::to_string(levelID);
        std::error_code ec;
        if (std::filesystem::exists(dir, ec)) {
            for (auto const& entry : std::filesystem::directory_iterator(dir, ec)) {
                if (ec) break;
                if (!entry.is_regular_file() || entry.path().extension() != ".gghost") continue;

                ReplayHeader header;
                std::vector<FrameData> frames;
                if (!ReplaySerializer::loadFromFile(entry.path(), header, frames)) continue;

                const int seconds = static_cast<int>(frames.size()) / 60;
                const std::string label =
                    fmt::format("{} - {}s", header.victorName[0] ? header.victorName : "?", seconds);
                addActionRow(scroll->m_contentLayer, listSize.width, label,
                             entry.path().string(), false);
                count++;
            }
        }

        if (count == 0) {
            auto empty = CCLabelBMFont::create("No victors yet - finish a run!", "bigFont.fnt");
            empty->setScale(0.45f);
            empty->setPosition(m_size / 2.f);
            m_mainLayer->addChild(empty);
        }

        scroll->m_contentLayer->updateLayout();
        return true;
    }

    // A row: label + a "Race"/"Off" button that sets the selection and closes.
    void addActionRow(CCNode* content, float width, const std::string& text,
                      const std::string& path, bool isNone) {
        auto row = CCMenu::create();
        row->setContentSize({width, 30.f});

        auto label = CCLabelBMFont::create(text.c_str(), "bigFont.fnt");
        label->setScale(0.5f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({6.f, 15.f});
        row->addChild(label);

        auto spr = ButtonSprite::create(isNone ? "Off" : "Race", "bigFont.fnt", "GJ_button_01.png", 0.6f);
        spr->setScale(0.6f);

        const int levelID = m_levelID;
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this, levelID, path](CCMenuItemSpriteExtra*) {
            GhostManager::get().setSelectedRun(levelID, path); // empty path = "none"
            this->onClose(nullptr);
        });
        btn->setAnchorPoint({1.f, 0.5f});
        btn->setPosition({width - 6.f, 15.f});
        row->addChild(btn);

        content->addChild(row);
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
