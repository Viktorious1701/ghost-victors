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
#include "GhostStripper.hpp"
#include "OpacityStateMachine.hpp"
#include "InterpolationEngine.hpp"

#include <algorithm>
#include <cmath>
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
// 2. PlayLayer Hook (Recording — Phase 1, Playback — Phase 2)
// ==========================================
class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        bool m_isRecording = false;   // recording gate result for the current attempt
        double m_playTime = 0.0;      // accumulated play-time this attempt (seconds); tick = *240
        int m_lastCaptureTick = -1;   // last recorded tick (throttle to ~60 Hz)
        PlayerObject* m_ghost = nullptr; // Phase 2 playback ghost (visual-only)
        int m_ghostGameMode = 0;      // ghost's current vehicle mode (0=cube)
        bool m_isGhostActive = false; // a ghost is loaded & spawned for this level
        bool m_inMirrorFlip = false;  // diagnostic throttle for mirror-transition logging (DP13)
    };

    // -------- Recording gate (FR-2.1): Normal Mode, from 0% (no StartPos) --------
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

    // -------- Phase 2: ghost vehicle mode switching (noEffects = no particle burst) --------
    void ghostToggleMode(int mode, bool enable) {
        auto g = m_fields->m_ghost;
        if (!g) return;
        switch (mode) {
            case 1: g->toggleFlyMode(enable, true); break;    // ship
            case 2: g->toggleRollMode(enable, true); break;   // ball
            case 3: g->toggleBirdMode(enable, true); break;   // ufo
            case 4: g->toggleDartMode(enable, true); break;   // wave
            case 5: g->toggleRobotMode(enable, true); break;
            case 6: g->toggleSpiderMode(enable, true); break;
            case 7: g->toggleSwingMode(enable, true); break;
            default: break;                                   // 0 = cube, nothing to toggle
        }
    }

    void ghostSetMode(int mode) {
        if (!m_fields->m_ghost || mode == m_fields->m_ghostGameMode) return;
        ghostToggleMode(m_fields->m_ghostGameMode, false); // leave the old mode
        ghostToggleMode(mode, true);                       // enter the new mode
        m_fields->m_ghostGameMode = mode;
    }

    // -------- Phase 2: apply recorded icons + colors to the ghost --------
    void ghostConfigureAppearance(const ReplayHeader& h) {
        auto g = m_fields->m_ghost;
        if (!g) return;
        g->updatePlayerFrame(h.cubeID);
        g->updatePlayerShipFrame(h.shipID);
        g->updatePlayerRollFrame(h.ballID);
        g->updatePlayerBirdFrame(h.ufoID);
        g->updatePlayerDartFrame(h.waveID);
        g->updatePlayerRobotFrame(h.robotID);
        g->updatePlayerSpiderFrame(h.spiderID);
        g->updatePlayerSwingFrame(h.swingID);
        g->setColor(ccColor3B{h.color1_R, h.color1_G, h.color1_B});
        g->setSecondColor(ccColor3B{h.color2_R, h.color2_G, h.color2_B});
        g->setCascadeOpacityEnabled(true); // so setOpacity reaches child icon sprites
    }

    // -------- Phase 2: load the level's saved .gghost and spawn the ghost --------
    // Idempotent: reachable from init() AND resetLevel() (GD's Retry calls resetLevel, not init),
    // so a ghost recorded this session appears on the next attempt without exit/re-enter.
    void ghostLoadAndSpawn(GJGameLevel* level) {
        if (m_fields->m_isGhostActive && m_fields->m_ghost) return; // already spawned — no double-spawn

        auto& frames = GhostManager::get().getLoadedFrames();
        frames.clear();
        m_fields->m_ghost = nullptr;
        m_fields->m_isGhostActive = false;
        m_fields->m_ghostGameMode = 0;
        GhostManager::get().clearActiveVictor();

        const int levelID = level ? level->m_levelID.value() : 0;
        const auto path = Mod::get()->getSaveDir() / "replays"
                          / fmt::format("{}.gghost", levelID);

        // No file yet (never recorded this level) is the normal first-play case — not an error.
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            log::info("Ghost Victors: no saved ghost for this level yet");
            return;
        }

        ReplayHeader header;
        if (!ReplaySerializer::loadFromFile(path, header, frames) || frames.empty()) {
            frames.clear();
            log::warn("Ghost Victors: ghost file present but failed to load: {}", path.string());
            return;
        }

        // NOTE: create()'s first two args are the player SLOT + defaults, not icon IDs — the real
        // icons are applied by ghostConfigureAppearance() via updatePlayer*Frame(). Passing the cube
        // ID as the first arg built a bad cube base (ship happened to survive), so the ghost's cube
        // came out wrong. Use (1, 1, ...) like the SDS and set icons explicitly. (DP9 fix)
        auto ghost = PlayerObject::create(1, 1, this, m_objectLayer, false);
        if (!ghost) {
            frames.clear();
            log::error("Ghost Victors: failed to create ghost PlayerObject");
            return;
        }
        m_fields->m_ghost = ghost;
        m_objectLayer->addChild(ghost, -1); // behind the player
        ghostConfigureAppearance(header);
        stripGhostVisuals(ghost);
        ghost->setVisible(false);
        ghost->setOpacity(0);

        GhostManager::get().setActiveVictor("local");
        m_fields->m_isGhostActive = true;
        log::info("Ghost Victors: ghost loaded — {} frames, racing saved run (victor '{}')",
                  frames.size(), header.victorName);
        // DP9 diagnostic: dump the header appearance so we can localize any icon mismatch
        // (capture side vs apply side).
        log::info("Ghost Victors: ghost header appearance — cube={} ship={} ball={} ufo={} wave={} "
                  "robot={} spider={} swing={} c1=({},{},{}) c2=({},{},{})",
                  header.cubeID, header.shipID, header.ballID, header.ufoID, header.waveID,
                  header.robotID, header.spiderID, header.swingID,
                  header.color1_R, header.color1_G, header.color1_B,
                  header.color2_R, header.color2_G, header.color2_B);
    }

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        log::info("Ghost Victors: Initialized PlayLayer for level: {}", level->m_levelName);
        log::info("Ghost Victors: GhostManager ready (active victor: {})",
                  GhostManager::get().hasActiveVictor());

        ghostBeginAttempt("init");
        ghostLoadAndSpawn(level);
        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        // FR-2.3: flush the recording buffer + re-evaluate the gate for the new attempt.
        ghostBeginAttempt("resetLevel");

        // Retry (resetLevel) doesn't re-run init(), so try to (lazy-)load the ghost here too — this is
        // what makes a run you just recorded show up when you press Retry. No-op once one is active.
        if (!m_fields->m_isGhostActive) {
            ghostLoadAndSpawn(m_level);
        }

        // FR-1.5 / AC-06: snap the ghost back to the start with the player.
        if (m_fields->m_ghost) {
            ghostSetMode(0); // revert vehicle to cube
            auto& frames = GhostManager::get().getLoadedFrames();
            if (!frames.empty()) {
                m_fields->m_ghost->setPosition({frames.front().x, frames.front().y});
                m_fields->m_ghost->setRotation(frames.front().rotation);
            }
            m_fields->m_ghost->setOpacity(0);
            m_fields->m_ghost->setVisible(false);
        }
    }

    // Per-frame work lives in postUpdate, NOT update: on Windows PlayLayer has no own update()
    // (the loop is GJBaseGameLayer::update), so a PlayLayer::update hook never fires there. (D8)
    void postUpdate(float dt) {
        PlayLayer::postUpdate(dt);
        if (!m_player1) return;

        // Shared play-time tick (240 TPS) — advances every frame; drives record AND playback.
        m_fields->m_playTime += dt;
        const int tick = static_cast<int>(m_fields->m_playTime * 240.0);

        // --- Phase 1: telemetry capture (~60 Hz keyframes) ---
        if (m_fields->m_isRecording &&
            (m_fields->m_lastCaptureTick < 0 || tick >= m_fields->m_lastCaptureTick + 4)) {
            const bool firstFrame = (m_fields->m_lastCaptureTick < 0);

            // Pack the vehicle mode (low nibble) + gravity-flip / mini flags (high bits) into
            // the gameMode byte — see DataTypes.hpp. Keeps FrameData at 17 B, format-compatible.
            uint8_t packedMode = ghostCurrentGameMode(m_player1) & GV_GAMEMODE_MASK;
            if (m_player1->m_isUpsideDown)  packedMode |= GV_FLAG_UPSIDEDOWN;
            if (m_player1->m_vehicleSize < 0.9f) packedMode |= GV_FLAG_MINI;

            FrameData frame;
            frame.tick = static_cast<uint32_t>(tick);
            frame.x = m_player1->getPositionX();
            frame.y = m_player1->getPositionY();
            frame.rotation = static_cast<float>(m_player1->getRotation());
            frame.gameMode = packedMode;

            GhostManager::get().getRecordingBuffer().push_back(frame);
            m_fields->m_lastCaptureTick = tick;

            if (firstFrame) {
                log::info("Ghost Victors: capture started (first frame at tick {})", tick);
            }
        }

        // --- Phase 2: ghost playback (X-progress lerp + mode/flip/mini + strip + opacity) ---
        if (m_fields->m_isGhostActive && m_fields->m_ghost &&
            GhostManager::get().isGhostVisibleInPause()) {
            // DP12: drive the ghost by the player's actual X-progress (not accumulated time) so the
            // lead stays constant and there's no drift/shake (mirror-safe). DP8: leadFrames is the
            // pacer lead (ghost-lead seconds × ~60 Hz); 0 = tracks the player's exact X.
            const double leadSeconds = Mod::get()->getSettingValue<double>("ghost-lead");
            float leadFrames = static_cast<float>(leadSeconds * 60.0);

            // DP13: mirror portals animate a horizontal flip of m_objectLayer (scaleX +1 → 0 → -1).
            // A leading ghost sits off the flip pivot and would swing ~2×lead across the screen during
            // the transition. Damp the lead by |scaleX| so it collapses to ~0 mid-flip (ghost rides the
            // pivot, no swing) and returns to full once stable (normal or fully mirrored).
            const float layerScaleX = m_objectLayer ? m_objectLayer->getScaleX() : 1.0f;
            const float mirrorFactor = std::min(1.0f, std::fabs(layerScaleX));
            leadFrames *= mirrorFactor;

            // Throttled diagnostic — confirm the flip is observable on m_objectLayer->getScaleX().
            if (mirrorFactor < 0.95f) {
                if (!m_fields->m_inMirrorFlip) {
                    m_fields->m_inMirrorFlip = true;
                    log::info("Ghost Victors: mirror flip detected — m_objectLayer scaleX={}",
                              layerScaleX);
                }
            } else {
                m_fields->m_inMirrorFlip = false;
            }

            const float playerX = m_player1->getPositionX();
            const uint8_t raw = updateGhostByProgress(m_fields->m_ghost,
                                                      GhostManager::get().getLoadedFrames(),
                                                      playerX, leadFrames);
            // DP11: unpack + apply the recorded gamemode, gravity-flip, and mini state.
            const int mode = raw & GV_GAMEMODE_MASK;
            const bool flip = (raw & GV_FLAG_UPSIDEDOWN) != 0;
            const bool mini = (raw & GV_FLAG_MINI) != 0;

            ghostSetMode(mode);
            const float size = mini ? 0.6f : 1.0f;
            m_fields->m_ghost->setScaleX(size);
            m_fields->m_ghost->setScaleY(flip ? -size : size); // negative Y = upside-down icon

            stripGhostVisuals(m_fields->m_ghost);
            applyGhostOpacity(m_fields->m_ghost, this->getCurrentPercent());
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
