#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/ButtonSprite.hpp>
#include <Geode/utils/cocos.hpp>

#include "../VictorsPopup.hpp"

using namespace geode::prelude;

// ==========================================
// Ghost Victors — "Victors" button on the level page (Phase 3)
// (Moved out of main.cpp — DP18.)
// ==========================================
class $modify(GVLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        log::info("Ghost Victors: Loaded LevelInfoLayer for level ID: {}", level->m_levelID);

        // geode.node-ids exposes "left-side-menu"; base init must run first for the IDs to exist.
        if (auto menu = this->getChildByID("left-side-menu")) {
            auto spr = ButtonSprite::create("Victors", "bigFont.fnt", "GJ_button_01.png", 0.6f);
            spr->setScale(0.7f);

            auto btn = CCMenuItemExt::createSpriteExtra(spr, [level](CCMenuItemSpriteExtra*) {
                if (auto popup = VictorsPopup::create(level->m_levelID.value())) popup->show();
            });
            btn->setID("victors-button"_spr);

            menu->addChild(btn);
            menu->updateLayout();
        }

        return true;
    }
};
