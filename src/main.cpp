#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

#include "DataTypes.hpp"
#include "GhostManager.hpp"

using namespace geode::prelude;

// ==========================================
// 1. Level Info Layer Hook (UI Button)
// ==========================================
class $modify(MyLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        log::info("Ghost Victors: Loaded LevelInfoLayer for level ID: {}", level->m_levelID);

        // UI button setup will go here
        return true;
    }
};

// ==========================================
// 2. PlayLayer Hook (Ghost & Recording Logic)
// ==========================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRecording = false;
        bool m_isGhostActive = false;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        log::info("Ghost Victors: Initialized PlayLayer for level: {}", level->m_levelName);

        // Phase 0 anchor: ensure GhostManager singleton is linked & reachable.
        log::info("Ghost Victors: GhostManager ready (active victor: {})",
                  GhostManager::get().hasActiveVictor());

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();
        // Reset buffers on restart
    }

    void update(float dt) {
        PlayLayer::update(dt);
        // Telemetry capture and ghost lerp playback will go here
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        // 100% completion save logic will go here
    }
};