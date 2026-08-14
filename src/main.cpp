#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/binding/GameManager.hpp>
#include <Geode/binding/GJAccountManager.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <Geode/binding/GJGameLevel.hpp>
#include <Geode/binding/StartPosObject.hpp>

#include "DataTypes.hpp"
#include "GhostManager.hpp"
#include "ReplaySerializer.hpp"

#include <cstring>
#include <filesystem>
#include <string>

using namespace geode::prelude;

// Map a PlayerObject's active vehicle flags to the packed gameMode enum
// (0=Cube, 1=Ship, 2=Ball, 3=UFO, 4=Wave, 5=Robot, 6=Spider, 7=Swing).
static uint8_t ghostCurrentGameMode(PlayerObject* p) {
    if (!p) return 0;
    if (p->m_isShip) return 1;
    if (p->m_isBall) return 2;
    if (p->m_isBird) return 3;   // UFO
    if (p->m_isDart) return 4;   // Wave
    if (p->m_isRobot) return 5;
    if (p->m_isSpider) return 6;
    if (p->m_isSwing) return 7;
    return 0;                    // Cube (all flags false)
}

// ==========================================
// 1. Level Info Layer Hook (UI Button)
// ==========================================
class $modify(MyLevelInfoLayer, LevelInfoLayer) {
    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;

        log::info("Ghost Victors: Loaded LevelInfoLayer for level ID: {}", level->m_levelID);

        // UI button setup will go here (Phase 3)
        return true;
    }
};

// ==========================================
// 2. PlayLayer Hook (Recording Logic — Phase 1)
// ==========================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRecording = false;   // gate result for the current attempt
        double m_playTime = 0.0;      // accumulated play-time this attempt (seconds)
        int m_lastCaptureTick = -1;   // last tick we sampled (throttle to ~60 Hz)
        bool m_isGhostActive = false; // reserved for Phase 2 playback
    };

    // Recording gate (FR-2.1): Normal Mode only, only runs starting from 0%.
    // There is no m_isFrom0 in the bindings — a live StartPos sets m_startPosObject,
    // so "from 0%" == no start-position object and not in practice.
    bool ghostShouldRecord() {
        return !m_isPracticeMode && m_startPosObject == nullptr;
    }

    void ghostBeginAttempt(const char* where) {
        m_fields->m_isRecording = ghostShouldRecord();
        m_fields->m_playTime = 0.0;
        m_fields->m_lastCaptureTick = -1;
        GhostManager::get().getRecordingBuffer().clear();
        log::info("Ghost Victors: recording {} at {} (buffer cleared)",
                  m_fields->m_isRecording ? "STARTED (Normal, from 0%)"
                                          : "SKIPPED (practice/startpos)",
                  where);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        log::info("Ghost Victors: Initialized PlayLayer for level: {}", level->m_levelName);
        log::info("Ghost Victors: GhostManager ready (active victor: {})",
                  GhostManager::get().hasActiveVictor());

        ghostBeginAttempt("init");
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        // FR-2.3: flush the buffer on every attempt reset (death / restart),
        // and re-evaluate the gate for the new attempt.
        ghostBeginAttempt("resetLevel");
    }

    // Capture runs in postUpdate, NOT update: on Windows PlayLayer has no own update()
    // (the loop is GJBaseGameLayer::update), so a PlayLayer::update hook never fires there.
    // PlayLayer::postUpdate is a real PlayLayer override that runs once per frame. (D8)
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);

        if (!m_fields->m_isRecording || !m_player1) return;

        // D4/D7: derive a running tick from accumulated play-time (GD physics is 240 TPS).
        // We accumulate dt ourselves because GJBaseGameLayer::m_currentStep reads 0 / is not
        // cumulative inside postUpdate. This is monotonic and framerate-independent. Sample
        // every 4th tick (~60 Hz) to stay within NFR-2.
        m_fields->m_playTime += dt;
        const int tick = static_cast<int>(m_fields->m_playTime * 240.0);
        if (m_fields->m_lastCaptureTick >= 0 && tick < m_fields->m_lastCaptureTick + 4) return;

        const bool firstFrame = (m_fields->m_lastCaptureTick < 0);

        FrameData frame;
        frame.tick = static_cast<uint32_t>(tick);
        frame.x = m_player1->getPositionX();
        frame.y = m_player1->getPositionY();
        frame.rotation = static_cast<float>(m_player1->getRotation());
        frame.gameMode = ghostCurrentGameMode(m_player1);

        GhostManager::get().getRecordingBuffer().push_back(frame);
        m_fields->m_lastCaptureTick = tick;

        if (firstFrame) {
            log::info("Ghost Victors: capture started (first frame at tick {})", tick);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();

        if (!m_fields->m_isRecording) {
            log::info("Ghost Victors: Recording SKIPPED — no file saved (practice/startpos run)");
            return;
        }

        auto& buffer = GhostManager::get().getRecordingBuffer();
        if (buffer.empty()) {
            log::warn("Ghost Victors: recording buffer empty on completion — nothing saved");
            return;
        }

        // --- Build the header (FR-2.4) ---
        ReplayHeader header; // magic "GGST" + formatVersion 1 come from defaults

        const int levelID = m_level ? m_level->m_levelID.value() : 0;
        header.levelID = static_cast<uint32_t>(levelID);

        if (auto* acc = GJAccountManager::sharedState()) {
            std::strncpy(header.victorName, acc->m_username.c_str(), sizeof(header.victorName) - 1);
            header.victorName[sizeof(header.victorName) - 1] = '\0';
        }

        if (auto* gm = GameManager::sharedState()) {
            header.cubeID   = static_cast<uint16_t>(gm->getPlayerFrame());
            header.shipID   = static_cast<uint16_t>(gm->getPlayerShip());
            header.ballID   = static_cast<uint16_t>(gm->getPlayerBall());
            header.ufoID    = static_cast<uint16_t>(gm->getPlayerBird());
            header.waveID   = static_cast<uint16_t>(gm->getPlayerDart());
            header.robotID  = static_cast<uint16_t>(gm->getPlayerRobot());
            header.spiderID = static_cast<uint16_t>(gm->getPlayerSpider());
            header.swingID  = static_cast<uint16_t>(gm->getPlayerSwing());

            const ccColor3B c1 = gm->colorForIdx(gm->getPlayerColor());
            const ccColor3B c2 = gm->colorForIdx(gm->getPlayerColor2());
            header.color1_R = c1.r; header.color1_G = c1.g; header.color1_B = c1.b;
            header.color2_R = c2.r; header.color2_G = c2.g; header.color2_B = c2.b;
        }

        header.totalFrames = static_cast<uint32_t>(buffer.size());

        // --- Save (replays/<levelID>.gghost in the mod save dir) ---
        const auto path = Mod::get()->getSaveDir() / "replays"
                          / fmt::format("{}.gghost", levelID);

        if (!ReplaySerializer::saveToFile(path, header, buffer)) {
            log::error("Ghost Victors: Recording save FAILED -> {}", path.string());
            return;
        }

        std::error_code ec;
        const auto sizeBytes = std::filesystem::file_size(path, ec);
        log::info("Ghost Victors: Recording SAVED {} frames -> {} ({} bytes)",
                  buffer.size(), path.string(), ec ? 0 : static_cast<uint64_t>(sizeBytes));

        // --- Save→reload self-verify (P1-AC4) ---
        ReplayHeader vHeader;
        std::vector<FrameData> vFrames;
        if (ReplaySerializer::loadFromFile(path, vHeader, vFrames)) {
            log::info("Ghost Victors: Verify reloaded OK — magic GGST, v{}, {} frames",
                      vHeader.formatVersion, vFrames.size());
        } else {
            log::error("Ghost Victors: Verify FAILED to reload {}", path.string());
        }
    }
};
