#pragma once
#include <Geode/Geode.hpp>
#include <Geode/ui/Popup.hpp>
#include <Geode/ui/ScrollLayer.hpp>
#include <Geode/ui/Layout.hpp>
#include <Geode/binding/FLAlertLayer.hpp>

#include "NetworkManager.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — Seeding targets popup (P6, admin-only)
// Read-only overview of the AREDL main list with a seeded/unseeded mark per demon, so the admin can
// see what still needs a Bot Vikkie ghost. Opened from VictorsPopup only when NetworkManager::isAdmin().
// Geode 5.9 Popup is a non-template FLAlertLayer subclass (init(w,h), m_mainLayer, m_size).
// ==========================================

class SeedingTargetsPopup : public Popup {
protected:
    float m_listW = 0.f;
    ScrollLayer* m_scroll = nullptr;

    bool initPopup() {
        if (!Popup::init(400.f, 280.f)) return false;
        this->setTitle("Seeding Targets");

        const CCSize listSize = {m_size.width - 40.f, m_size.height - 55.f};
        m_listW = listSize.width;
        m_scroll = ScrollLayer::create(listSize);
        m_scroll->setPosition((m_size - listSize) / 2.f);
        m_scroll->m_contentLayer->setLayout(ScrollLayer::createDefaultListLayout());
        m_mainLayer->addChild(m_scroll);

        addLabelRow("Loading AREDL...");

        // Fetch the seeded set first, then the AREDL list, then render once both are in.
        Ref<SeedingTargetsPopup> self = this;
        NetworkManager::fetchSeededLevelIds([self](bool, std::vector<int> seeded) {
            if (!self->getParent()) return;
            auto seededSet = std::make_shared<std::unordered_set<int>>(seeded.begin(), seeded.end());
            NetworkManager::fetchAredlList([self, seededSet](bool ok, std::vector<AredlLevel> list) {
                if (!self->getParent()) return;
                self->render(ok, list, *seededSet);
            });
        });
        return true;
    }

    void render(bool ok, std::vector<AredlLevel>& list, std::unordered_set<int> const& seeded) {
        auto content = m_scroll->m_contentLayer;
        content->removeAllChildren();

        if (!ok) {
            addLabelRow("Failed to load AREDL list.");
            content->updateLayout();
            return;
        }
        if (list.empty()) {
            addLabelRow("No main-list demons returned.");
            content->updateLayout();
            return;
        }

        std::sort(list.begin(), list.end(),
                  [](AredlLevel const& a, AredlLevel const& b) { return a.position < b.position; });

        int seededCount = 0;
        for (auto const& l : list) if (seeded.count(l.levelId)) ++seededCount;
        addLabelRow("Seeded " + std::to_string(seededCount) + " / " + std::to_string((int)list.size()));

        for (auto const& l : list) {
            const bool isSeeded = seeded.count(l.levelId) > 0;
            addTargetRow(l, isSeeded);
        }
        content->updateLayout();
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

    void addTargetRow(AredlLevel const& l, bool seeded) {
        auto row = CCNode::create();
        row->setContentSize({m_listW, 26.f});

        auto label = CCLabelBMFont::create(
            ("#" + std::to_string(l.position) + "  " + l.name).c_str(), "bigFont.fnt");
        label->setScale(0.45f);
        label->setAnchorPoint({0.f, 0.5f});
        label->setPosition({6.f, 13.f});
        // keep long names from overrunning the seeded marker
        const float labelW = label->getContentSize().width;
        if (labelW > 0.f && labelW * label->getScale() > m_listW - 70.f)
            label->setScale((m_listW - 70.f) / labelW);
        row->addChild(label);

        auto mark = CCLabelBMFont::create(seeded ? "seeded" : "-",
                                          seeded ? "goldFont.fnt" : "bigFont.fnt");
        mark->setScale(0.45f);
        mark->setAnchorPoint({1.f, 0.5f});
        mark->setPosition({m_listW - 6.f, 13.f});
        if (!seeded) mark->setOpacity(120);
        row->addChild(mark);

        m_scroll->m_contentLayer->addChild(row);
    }

public:
    static SeedingTargetsPopup* create() {
        auto ret = new SeedingTargetsPopup();
        if (ret && ret->initPopup()) {
            ret->autorelease();
            return ret;
        }
        CC_SAFE_DELETE(ret);
        return nullptr;
    }
};
