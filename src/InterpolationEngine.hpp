#pragma once
#include "DataTypes.hpp"
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — X-progress keyframe playback
// See docs/SDS.md §3.3 and docs/plans/phase2-playback.plan.md (FR-3.3, DP12)
//
// The ghost is driven by the player's actual X-progress, NOT by wall-clock time. GD forward motion is
// monotonic in X, so `frames` are sorted ascending by x. We find where the player currently is along
// the recording (fractional frame index), advance by `leadFrames` (the pacer lead), and lerp the
// ghost's x/y/rotation around that target index. This is drift-free (no time↔X mismatch), which keeps
// the lead constant and stops the mirror-section shake that time-indexing produced.
//
// Returns the target frame's raw gameMode byte (low nibble = mode, bit4 = upside-down, bit5 = mini).
// ==========================================

inline uint8_t updateGhostByProgress(PlayerObject* ghost, const std::vector<FrameData>& frames,
                                     float playerX, float leadFrames) {
    if (!ghost || frames.empty()) return 0;

    const int n = static_cast<int>(frames.size());

    // 1. Locate the player's current X within the recording → fractional index pIdx.
    auto it = std::lower_bound(frames.begin(), frames.end(), playerX,
        [](const FrameData& f, float x) { return f.x < x; });

    float pIdx;
    if (it == frames.begin()) {
        pIdx = 0.0f;
    } else if (it == frames.end()) {
        pIdx = static_cast<float>(n - 1);
    } else {
        const int j = static_cast<int>(it - frames.begin()); // first frame with x >= playerX
        const float x0 = frames[j - 1].x, x1 = frames[j].x;
        const float denom = x1 - x0;
        const float frac = denom > 0.0f ? (playerX - x0) / denom : 0.0f;
        pIdx = static_cast<float>(j - 1) + frac;
    }

    // 2. Advance by the pacer lead and clamp into range.
    float target = pIdx + leadFrames;
    if (target < 0.0f) target = 0.0f;
    if (target > static_cast<float>(n - 1)) target = static_cast<float>(n - 1);

    // 3. Lerp x/y/rotation between the two frames around the target index.
    const int i0 = static_cast<int>(std::floor(target));
    const int i1 = std::min(i0 + 1, n - 1);
    const float t = target - static_cast<float>(i0);
    const FrameData& a = frames[i0];
    const FrameData& b = frames[i1];

    ghost->setPosition({a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t});
    ghost->setRotation(a.rotation + (b.rotation - a.rotation) * t);
    return a.gameMode;
}
