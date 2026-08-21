#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/TextInput.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/binding/FLAlertLayer.hpp>
#include <Geode/utils/cocos.hpp>

#include "NetworkManager.hpp"

#include <string>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — Legit request popup (P7)
// A player attaches a YouTube completion (+ Google-Drive raw footage) to one of their online runs so
// Vikkie can review it. Raw footage is marked required when the level's AREDL entry requires it; the
// server (request-legit fn) enforces the same rule. The run stays 'unverified' until reviewed.
// ==========================================

class LegitRequestPopup : public Popup {
protected:
    std::string m_runId;
    int m_levelID = 0;
    bool m_requiresRaw = false;
    TextInput* m_ytInput = nullptr;
    TextInput* m_rawInput = nullptr;
    CCLabelBMFont* m_rawLabel = nullptr;

    bool initPopup(std::string runId, int levelID) {
        if (!Popup::init(360.f, 230.f)) return false;
        m_runId = runId;
        m_levelID = levelID;
        this->setTitle("Request Legit");

        const float cx = m_size.width / 2.f;

        auto ytLabel = CCLabelBMFont::create("YouTube completion (required)", "bigFont.fnt");
        ytLabel->setScale(0.4f);
        ytLabel->setAnchorPoint({0.f, 0.5f});
        ytLabel->setPosition({30.f, m_size.height - 55.f});
        m_mainLayer->addChild(ytLabel);

        m_ytInput = TextInput::create(m_size.width - 60.f, "https://youtube.com/...", "chatFont.fnt");
        m_ytInput->setCommonFilter(CommonFilter::Any);
        m_ytInput->setMaxCharCount(500);
        m_ytInput->setPosition({cx, m_size.height - 75.f});
        m_mainLayer->addChild(m_ytInput);

        m_rawLabel = CCLabelBMFont::create("Raw footage (Google Drive, optional)", "bigFont.fnt");
        m_rawLabel->setScale(0.4f);
        m_rawLabel->setAnchorPoint({0.f, 0.5f});
        m_rawLabel->setPosition({30.f, m_size.height - 110.f});
        m_mainLayer->addChild(m_rawLabel);

        m_rawInput = TextInput::create(m_size.width - 60.f, "https://drive.google.com/...", "chatFont.fnt");
        m_rawInput->setCommonFilter(CommonFilter::Any);
        m_rawInput->setMaxCharCount(500);
        m_rawInput->setPosition({cx, m_size.height - 130.f});
        m_mainLayer->addChild(m_rawInput);

        auto menu = CCMenu::create();
        menu->setPosition(0.f, 0.f);
        auto spr = ButtonSprite::create("Submit", "bigFont.fnt", "GJ_button_01.png", 0.7f);
        spr->setScale(0.7f);
        auto btn = CCMenuItemExt::createSpriteExtra(spr, [this](CCMenuItemSpriteExtra*) { onSubmit(); });
        btn->setPosition({cx, 32.f});
        menu->addChild(btn);
        m_mainLayer->addChild(menu);

        // Discover whether this level requires raw footage (AREDL) and reflect it in the label.
        Ref<LegitRequestPopup> self = this;
        NetworkManager::fetchAredlMeta(levelID, [self](NetworkManager::AredlMeta meta) {
            if (!self->getParent()) return;
            self->m_requiresRaw = meta.requiresRawFootage;
            if (meta.requiresRawFootage && self->m_rawLabel)
                self->m_rawLabel->setString("Raw footage (REQUIRED for this level)");
        });
        return true;
    }

    void onSubmit() {
        const std::string yt = m_ytInput->getString();
        const std::string raw = m_rawInput->getString();
        if (yt.empty()) {
            FLAlertLayer::create("Ghost Victors", "A YouTube completion link is required.", "OK")->show();
            return;
        }
        if (m_requiresRaw && raw.empty()) {
            FLAlertLayer::create("Ghost Victors",
                "This level requires <cy>raw footage</c> (Google Drive link).", "OK")->show();
            return;
        }
        Ref<LegitRequestPopup> self = this;
        NetworkManager::requestLegit(m_runId, yt, raw, [self](bool ok, std::string err) {
            FLAlertLayer::create("Ghost Victors",
                ok ? "Submitted for review!" : ("Failed: " + err), "OK")->show();
            if (ok && self->getParent()) self->onClose(nullptr);
        });
    }

public:
    static LegitRequestPopup* create(std::string runId, int levelID) {
        auto ret = new LegitRequestPopup();
        if (ret && ret->initPopup(runId, levelID)) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};
