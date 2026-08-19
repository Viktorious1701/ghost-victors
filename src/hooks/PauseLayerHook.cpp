#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>
#include <Geode/binding/CCMenuItemToggler.hpp>

#include "../GhostManager.hpp"

using namespace geode::prelude;

// ==========================================
// Ghost Victors — mid-game ghost visibility toggle (Phase 3 / AC-08)
// A checkbox in the pause menu wired to GhostManager::setGhostVisibleInPause.
// PlayLayer::postUpdate reads the flag and hides/shows the ghost immediately (DP17).
// ==========================================
class $modify(GVPauseLayer, PauseLayer) {
    void customSetup() {
        PauseLayer::customSetup();

        // geode.node-ids exposes "center-button-menu"; base customSetup must run first.
        auto menu = this->getChildByID("center-button-menu");
        if (!menu) return;

        auto off = CCSprite::createWithSpriteFrameName("GJ_checkOff_001.png");
        auto on = CCSprite::createWithSpriteFrameName("GJ_checkOn_001.png");
        auto toggler = CCMenuItemToggler::create(
            off, on, this, menu_selector(GVPauseLayer::onToggleGhost));
        toggler->setID("ghost-visible-toggle"_spr);
        // Checked (on) = ghost visible. Reflect the current GhostManager state.
        toggler->toggle(GhostManager::get().isGhostVisibleInPause());

        menu->addChild(toggler);
        menu->updateLayout();
    }

    // CCMenuItemToggler flips its own visual on click; mirror that into GhostManager
    // (GhostManager is the source of truth — postUpdate acts on it next frame).
    void onToggleGhost(CCObject*) {
        const bool nowVisible = !GhostManager::get().isGhostVisibleInPause();
        GhostManager::get().setGhostVisibleInPause(nowVisible);
        log::info("Ghost Victors: pause toggle — ghost {}", nowVisible ? "SHOWN" : "HIDDEN");
    }
};
