#pragma once
#include <cstdint>

// ==========================================
// Ghost Victors — .gghost binary data model
// See docs/SDS.md §2 and docs/plans/phase0-data-state.plan.md
// ==========================================

#pragma pack(push, 1) // Force 1-byte alignment to prevent compiler padding gaps

// File Header: exactly 68 bytes (packed).
// NOTE: SDS §2.1's diagram labels this "64 bytes / 12B Icon Config", but §2.2 declares 8 icon IDs
// (16B). GD 2.2 has 8 gamemodes, so all 8 are kept and the true packed size is 68 (decision D1).
struct ReplayHeader {
    char magic[4] = {'G', 'G', 'S', 'T'}; // File signature
    uint16_t formatVersion = 1;           // Format version
    uint32_t levelID = 0;                 // Target level ID
    char victorName[32] = {0};            // Author / victor name string

    // Icon configuration (one per gamemode)
    uint16_t cubeID = 1;
    uint16_t shipID = 1;
    uint16_t ballID = 1;
    uint16_t ufoID = 1;
    uint16_t waveID = 1;
    uint16_t robotID = 1;
    uint16_t spiderID = 1;
    uint16_t swingID = 1;

    // Color configuration
    uint8_t color1_R = 255, color1_G = 255, color1_B = 255;
    uint8_t color2_R = 255, color2_G = 255, color2_B = 255;

    uint32_t totalFrames = 0;             // Total keyframes contained in body
};

// Frame keyframe: exactly 17 bytes per tick sample.
// NOTE: SRS FR-2.2 mentions "scale" but SDS §2.2's layout omits it; we follow SDS (decision D2).
struct FrameData {
    uint32_t tick;       // GD physics tick count
    float x;             // World X position
    float y;             // World Y position
    float rotation;      // Rotation angle in degrees
    uint8_t gameMode;    // Active gamemode enum (0=Cube, 1=Ship, 2=Ball, ...)
};

#pragma pack(pop)

static_assert(sizeof(ReplayHeader) == 68, "ReplayHeader must be exactly 68 bytes (packed).");
static_assert(sizeof(FrameData) == 17, "FrameData must be exactly 17 bytes (packed).");
