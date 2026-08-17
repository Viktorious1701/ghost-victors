#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — start-line opacity state machine
// See docs/SDS.md §3.2 and docs/plans/phase2-playback.plan.md (FR-1.2 … FR-1.4)
//
// Opacity as a function of the *player's* completion percent P:
//   P < 3.0            -> hidden, opacity 0
//   3.0 <= P < 5.0     -> visible, opacity = floor(128 * (P - 3) / 2)   (linear 0 -> 128)
//   P >= 5.0           -> visible, opacity 128 (50%)
// The ghost has setCascadeOpacityEnabled(true) so this reaches its child icon sprites.
// ==========================================

inline void applyGhostOpacity(PlayerObject* ghost, float percent) {
    if (!ghost) return;

    if (percent < 3.0f) {
        ghost->setVisible(false);
        ghost->setOpacity(0);
    } else if (percent < 5.0f) {
        const float t = (percent - 3.0f) / 2.0f;                 // [0, 1)
        ghost->setVisible(true);
        ghost->setOpacity(static_cast<GLubyte>(t * 128.0f));     // [0, 128)
    } else {
        ghost->setVisible(true);
        ghost->setOpacity(128);                                  // locked 50%
    }
}
