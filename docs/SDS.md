# Software Design Specification (SDS)
## Project Name: Ghost Victors (Geometry Dash Mod)
**Target Framework:** Geode SDK (C++20, CMake, Cocos2d-x)  
**Author:** Vikkie  
**Document Version:** 1.0  
**Status:** Approved Technical Architecture  

---

## 1. System Architecture Overview

The **Ghost Victors** architecture is decoupled into five core modules:

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                            GEODE ENGINE HOOKS                                │
│        LevelInfoLayer           PlayLayer                PauseLayer          │
└──────────────┬─────────────────────┬──────────────────────────┬──────────────┘
               │                     │                          │
               ▼                     ▼                          ▼
┌─────────────────────────┐ ┌──────────────────┐ ┌─────────────────────────────┐
│      UI CONTROLLER      │ │ GHOST ENGINE     │ │  RECORDING / CAPTURE        │
│ (VictorsPopup / Button) │ │ (Lerp, Opacity,  │ │  (Telemetry Buffer,         │
│                         │ │  Stripper)       │ │   Completion Trigger)       │
└──────────────┬──────────┘ └────────┬─────────┘ └──────────────┬──────────────┘
               │                     │                          │
               ▼                     ▼                          ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                            CACHE & STORAGE MANAGER                           │
│                     (Binary Serializer / `.gghost` IO)                       │
└──────────────────────────────────────┬───────────────────────────────────────┘
                                       │
                                       ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                            NETWORK CONTROLLER                                │
│                 (Async HTTP Client via `web::WebRequest`)                    │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Memory Model & Binary File Format Specification (`.gghost`)

To ensure maximum I/O efficiency, replay files are stored in a packed binary format (`.gghost`). This eliminates JSON string parsing overhead during gameplay.

### 2.1 Binary Memory Layout

```text
+-------------------------------------------------------------------+
| HEADER (64 Bytes)                                                 |
| Magic (4B) | Version (2B) | Level ID (4B) | Victor Name (32B)     |
| Icon Config (12B) | Colors (6B) | Total Frames (4B)               |
+-------------------------------------------------------------------+
| BODY (FrameData Array: N x 17 Bytes)                              |
| Frame 0: Tick (4B) | X (4B) | Y (4B) | Rot (4B) | Mode (1B)        |
| Frame 1: Tick (4B) | X (4B) | Y (4B) | Rot (4B) | Mode (1B)        |
| ...                                                               |
+-------------------------------------------------------------------+
```

### 2.2 C++ Memory Structures (`DataTypes.hpp`)

```cpp
#pragma once
#include <cstdint>
#include <cocos2d.h>

#pragma pack(push, 1) // Force 1-byte alignment to prevent compiler padding gaps

// File Header: Exactly 64 Bytes
struct ReplayHeader {
    char magic[4] = {'G', 'G', 'S', 'T'}; // File Signature
    uint16_t formatVersion = 1;           // Version 1
    uint32_t levelID = 0;                 // Target Level ID
    char victorName[32] = {0};            // Author/Victor Name string

    // Icon Configuration
    uint16_t cubeID = 1;
    uint16_t shipID = 1;
    uint16_t ballID = 1;
    uint16_t ufoID = 1;
    uint16_t waveID = 1;
    uint16_t robotID = 1;
    uint16_t spiderID = 1;
    uint16_t swingID = 1;

    // Color Configuration
    uint8_t color1_R = 255, color1_G = 255, color1_B = 255;
    uint8_t color2_R = 255, color2_G = 255, color2_B = 255;

    uint32_t totalFrames = 0;              // Total keyframes contained in body
};

// Frame Keyframe: Exactly 17 Bytes per tick sample
struct FrameData {
    uint32_t tick;       // GD physics tick count
    float x;             // World X position
    float y;             // World Y position
    float rotation;      // Rotation angle in degrees
    uint8_t gameMode;    // Active gamemode enum (0=Cube, 1=Ship, 2=Ball, etc.)
};

#pragma pack(pop)
```

---

## 3. Ghost Rendering & Interpolation Engine Architecture

### 3.1 Particle & Trail Stripping Routine

When creating a ghost `PlayerObject`, we must detach/disable particle emitters and trail renderers to maintain **Icon-Only** visuals:

```cpp
// GhostStripper.hpp
#pragma once
#include <Geode/Geode.hpp>

using namespace geode::prelude;

inline void stripGhostVisuals(PlayerObject* ghost) {
    if (!ghost) return;

    // 1. Lock base opacity to 0 on init (managed by fade-in state machine later)
    ghost->setOpacity(0);
    ghost->setVisible(false);

    // 2. Hide Motion Streaks & Wave Trails
    if (ghost->m_waveTrail) ghost->m_waveTrail->setVisible(false);
    if (ghost->m_regularTrail) ghost->m_regularTrail->setVisible(false);

    // 3. Disable Particle Emitters
    if (ghost->m_shipBoostParticles) {
        ghost->m_shipBoostParticles->stopSystem();
        ghost->m_shipBoostParticles->setVisible(false);
    }
    if (ghost->m_dragParticles) {
        ghost->m_dragParticles->stopSystem();
        ghost->m_dragParticles->setVisible(false);
    }
    if (ghost->m_ufoParticles) {
        ghost->m_ufoParticles->stopSystem();
        ghost->m_ufoParticles->setVisible(false);
    }

    // 4. Disable Collision & Ground Particles
    if (ghost->m_landParticles0) ghost->m_landParticles0->setVisible(false);
    if (ghost->m_landParticles1) ghost->m_landParticles1->setVisible(false);
}
```

---

### 3.2 Dynamic Opacity State Machine (3%–5% Threshold)

To eliminate visual clutter at the starting line, the ghost opacity is calculated as a function of the current player's completion percentage ($P$):

$$\text{Opacity}(P) = \begin{cases} 
0 & \text{if } P < 3.0 \\
\left\lfloor 128 \times \frac{P - 3.0}{5.0 - 3.0} \right\rfloor & \text{if } 3.0 \le P < 5.0 \\
128 & \text{if } P \ge 5.0 
\end{cases}$$

```cpp
// OpacityStateMachine.hpp
inline void applyGhostOpacityStateMachine(PlayerObject* ghost, float currentPercent) {
    if (!ghost) return;

    if (currentPercent < 3.0f) {
        ghost->setVisible(false);
        ghost->setOpacity(0);
    } 
    else if (currentPercent >= 3.0f && currentPercent < 5.0f) {
        ghost->setVisible(true);
        float progress = (currentPercent - 3.0f) / 2.0f; // Range [0.0, 1.0]
        GLubyte alpha = static_cast<GLubyte>(progress * 128); // Range [0, 128]
        ghost->setOpacity(alpha);
    } 
    else {
        ghost->setVisible(true);
        ghost->setOpacity(128); // Lock at 50% opacity
    }
}
```

---

### 3.3 Position Interpolation Mathematics (`lerp`)

> **Implementation note (Phase 2, DP12):** the shipped engine indexes keyframes by the player's
> **X-progress**, not by tick/time. Time-indexing drifts (the lead wanders → acceleration/shake, worst
> in mirror-portal sections which negate X). We binary-search frames by `x` for the player's current X,
> advance by a lead (in frames), and lerp x/y/rotation around that index — the `lerp` math below still
> applies, but `t` is derived from X-position, not `Tick`. Also, `FrameData.gameMode` is a **packed
> byte** (bits 0-3 gamemode, bit 4 upside-down/gravity, bit 5 mini — DP11). See
> `src/InterpolationEngine.hpp`.

Because GD framerates vary (60Hz, 144Hz, 240Hz, 360Hz), the ghost playback position between two keyframes $A$ and $B$ is calculated using linear interpolation:

$$\vec{P}_{\text{ghost}} = \vec{P}_A + (\vec{P}_B - \vec{P}_A) \times t$$
$$\theta_{\text{ghost}} = \theta_A + (\theta_B - \theta_A) \times t$$

where $t = \frac{\text{Tick}_{\text{current}} - \text{Tick}_A}{\text{Tick}_B - \text{Tick}_A}$.

```cpp
// InterpolationEngine.hpp
inline void updateGhostLerp(PlayerObject* ghost, const std::vector<FrameData>& frames, uint32_t currentTick) {
    if (frames.empty() || !ghost) return;

    // Binary search to locate current tick interval [Frame A, Frame B]
    auto it = std::lower_bound(frames.begin(), frames.end(), currentTick, 
        [](const FrameData& frame, uint32_t tick) {
            return frame.tick < tick;
        });

    if (it == frames.end()) {
        // End of replay reached: set to final recorded frame
        const auto& last = frames.back();
        ghost->setPosition({last.x, last.y});
        ghost->setRotation(last.rotation);
        return;
    }

    if (it == frames.begin()) {
        const auto& first = frames.front();
        ghost->setPosition({first.x, first.y});
        ghost->setRotation(first.rotation);
        return;
    }

    const FrameData& frameB = *it;
    const FrameData& frameA = *(it - 1);

    // Calculate normalized progress t in range [0.0, 1.0]
    float t = static_cast<float>(currentTick - frameA.tick) / static_cast<float>(frameB.tick - frameA.tick);

    // Interpolate Position
    float interpolatedX = frameA.x + (frameB.x - frameA.x) * t;
    float interpolatedY = frameA.y + (frameB.y - frameA.y) * t;
    float interpolatedRot = frameA.rotation + (frameB.rotation - frameA.rotation) * t;

    ghost->setPosition({interpolatedX, interpolatedY});
    ghost->setRotation(interpolatedRot);
}
```

---

## 4. Class & Component Specification

### 4.1 Class Structure Diagram

```
                       ┌─────────────────────────┐
                       │      GhostManager       │  <-- Global Singleton
                       └────────────┬────────────┘
                                    │
           ┌────────────────────────┼────────────────────────┐
           ▼                        ▼                        ▼
┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────────┐
│  ReplaySerializer   │  │   NetworkManager    │  │    CacheManager     │
│ (Binary File I/O)   │  │ (Async HTTP Client) │  │ (Local Filesystem)  │
└─────────────────────┘  └─────────────────────┘  └─────────────────────┘
```

---

### 4.2 Class Definitions

#### A. Singleton Manager (`GhostManager.hpp`)
Central manager handling active state, selected victor metadata, and replay buffers across layers.

```cpp
#pragma once
#include "DataTypes.hpp"
#include <string>
#include <vector>
#include <optional>

class GhostManager {
private:
    GhostManager() = default;
    
    std::optional<std::string> m_activeVictorID;
    std::vector<FrameData> m_loadedGhostFrames;
    std::vector<FrameData> m_recordingBuffer;
    bool m_isGhostVisibleInPause = true;

public:
    static GhostManager& get() {
        static GhostManager instance;
        return instance;
    }

    void setActiveVictor(const std::string& victorID) { m_activeVictorID = victorID; }
    void clearActiveVictor() { m_activeVictorID.reset(); }
    bool hasActiveVictor() const { return m_activeVictorID.has_value(); }

    std::vector<FrameData>& getLoadedFrames() { return m_loadedGhostFrames; }
    std::vector<FrameData>& getRecordingBuffer() { return m_recordingBuffer; }

    bool isGhostVisibleInPause() const { return m_isGhostVisibleInPause; }
    void setGhostVisibleInPause(bool visible) { m_isGhostVisibleInPause = visible; }
};
```

---

#### B. Binary Serializer (`ReplaySerializer.hpp`)
Handles binary reading and writing of `.gghost` files.

```cpp
#pragma once
#include "DataTypes.hpp"
#include <Geode/Geode.hpp>
#include <filesystem>
#include <vector>

using namespace geode::prelude;

class ReplaySerializer {
public:
    static bool saveToFile(const std::filesystem::path& path, const ReplayHeader& header, const std::vector<FrameData>& frames) {
        std::ofstream outFile(path, std::ios::binary);
        if (!outFile.is_open()) return false;

        // 1. Write Header (64 Bytes)
        outFile.write(reinterpret_cast<const char*>(&header), sizeof(ReplayHeader));

        // 2. Write Frame Array
        outFile.write(reinterpret_cast<const char*>(frames.data()), frames.size() * sizeof(FrameData));

        outFile.close();
        return true;
    }

    static bool loadFromFile(const std::filesystem::path& path, ReplayHeader& outHeader, std::vector<FrameData>& outFrames) {
        std::ifstream inFile(path, std::ios::binary);
        if (!inFile.is_open()) return false;

        // 1. Read Header
        inFile.read(reinterpret_cast<char*>(&outHeader), sizeof(ReplayHeader));

        // Verify magic bytes "GGST"
        if (std::string(outHeader.magic, 4) != "GGST") {
            log::error("Invalid replay magic signature!");
            return false;
        }

        // 2. Read Frame Array
        outFrames.resize(outHeader.totalFrames);
        inFile.read(reinterpret_cast<char*>(outFrames.data()), outHeader.totalFrames * sizeof(FrameData));

        inFile.close();
        return true;
    }
};
```

---

#### C. Network & Cache Controller (`NetworkManager.hpp`)
Handles asynchronous REST API interaction using Geode's `web::WebRequest`.

```cpp
#pragma once
#include <Geode/Geode.hpp>
#include <Geode/utils/web.hpp>

using namespace geode::prelude;

class NetworkManager {
public:
    static void fetchVictorsList(int levelID, std::function<void(const std::string& jsonResponse)> callback) {
        std::string url = fmt::format("https://your-api.com/api/v1/levels/{}/victors?sort=upload_asc", levelID);

        web::WebRequest req;
        req.get(url).listen([callback](web::WebResponse* res) {
            if (res && res->ok()) {
                callback(res->string().unwrapOrDefault("{}"));
            } else {
                callback("{}");
            }
        });
    }

    static void downloadReplay(const std::string& downloadUrl, const std::filesystem::path& destPath, std::function<void(bool)> callback) {
        web::WebRequest req;
        req.get(downloadUrl).listen([destPath, callback](web::WebResponse* res) {
            if (res && res->ok()) {
                file::writeBinary(destPath, res->data());
                callback(true);
            } else {
                callback(false);
            }
        });
    }
};
```

---

## 5. Geode Modifier Hooks Implementation Map

### 5.1 `PlayLayer` Hook Architecture (`src/hooks/PlayLayerHook.cpp`)

```cpp
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include "../GhostManager.hpp"
#include "../GhostStripper.hpp"
#include "../InterpolationEngine.hpp"
#include "../OpacityStateMachine.hpp"

using namespace geode::prelude;

class $modify(GhostPlayLayer, PlayLayer) {
    struct Fields {
        PlayerObject* m_ghostPlayer = nullptr;
    };

    bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
        if (!PlayLayer::init(level, useReplay, dontCreateObjects)) return false;

        auto& gm = GhostManager::get();
        if (gm.hasActiveVictor()) {
            // Instantiate Ghost PlayerObject
            m_fields->m_ghostPlayer = PlayerObject::create(1, 1, this, m_objectLayer, false);
            
            // Strip particles & trails
            stripGhostVisuals(m_fields->m_ghostPlayer);
            
            // Add to Object Layer behind main player
            m_objectLayer->addChild(m_fields->m_ghostPlayer);
        }

        return true;
    }

    void resetLevel() {
        PlayLayer::resetLevel();

        // Clear recording buffer for new attempt
        GhostManager::get().getRecordingBuffer().clear();

        // Reset ghost position & visibility
        if (m_fields->m_ghostPlayer) {
            m_fields->m_ghostPlayer->setOpacity(0);
            m_fields->m_ghostPlayer->setVisible(false);
        }
    }

    void update(float dt) {
        PlayLayer::update(dt);

        auto& gm = GhostManager::get();

        // 1. Telemetry Capture (0% Normal Mode)
        if (m_player1 && !m_isPracticeMode && m_isFrom0) {
            FrameData frame;
            frame.tick = m_gameState->m_currentTick;
            frame.x = m_player1->getPositionX();
            frame.y = m_player1->getPositionY();
            frame.rotation = m_player1->getRotation();
            gm.getRecordingBuffer().push_back(frame);
        }

        // 2. Ghost Playback & Dynamic Opacity Calculation
        if (m_fields->m_ghostPlayer && gm.isGhostVisibleInPause()) {
            // Calculate player progress percentage
            float currentPercent = this->getCurrentPercent();

            // Apply 3%-5% opacity state machine
            applyGhostOpacityStateMachine(m_fields->m_ghostPlayer, currentPercent);

            // Interpolate position
            updateGhostLerp(m_fields->m_ghostPlayer, gm.getLoadedFrames(), m_gameState->m_currentTick);
        }
    }
};
```

---

## 6. Directory Layout & File Organization

Upon completing the design implementation, Vikkie's project repository will be structured as follows:

```text
ghost-victors/
├── CMakeLists.txt
├── mod.json
├── SRS.md
├── SDS.md
└── src/
    ├── main.cpp
    ├── DataTypes.hpp
    ├── GhostManager.hpp
    ├── GhostStripper.hpp
    ├── OpacityStateMachine.hpp
    ├── InterpolationEngine.hpp
    ├── ReplaySerializer.hpp
    ├── NetworkManager.hpp
    └── hooks/
        ├── PlayLayerHook.cpp
        ├── LevelInfoLayerHook.cpp
        └── PauseLayerHook.cpp
```

---

**Author Sign-Off:**  
**Vikkie** — *Lead Mod Developer & Architect*