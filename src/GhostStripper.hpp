#pragma once
#include <Geode/Geode.hpp>
#include <Geode/binding/PlayerObject.hpp>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — icon-only visual stripper
// See docs/SDS.md §3.1 and docs/plans/phase2-playback.plan.md (FR-3.2 / AC-04)
//
// Hides the ghost's motion streaks and stops+hides every particle system. Idempotent and cheap —
// call it once at spawn and again every frame (a vehicle mode-switch can respawn systems).
// Member names verified against geode-sdk/bindings 2.2074 (the SDS's m_shipBoostParticles /
// m_dragParticles / m_ufoParticles names do not exist; the real particles live in m_particleSystems).
// ==========================================

inline void stripGhostVisuals(PlayerObject* ghost) {
    if (!ghost) return;

    // Motion streaks / trails (not CCParticleSystems).
    if (ghost->m_regularTrail) ghost->m_regularTrail->setVisible(false);
    if (ghost->m_shipStreak)   ghost->m_shipStreak->setVisible(false);
    if (ghost->m_waveTrail)    ghost->m_waveTrail->setVisible(false);

    // All particle emitters (boost, drag, dash, land, click, burst, ground, …).
    if (ghost->m_particleSystems) {
        for (int i = 0; i < ghost->m_particleSystems->count(); ++i) {
            auto* p = static_cast<CCParticleSystem*>(ghost->m_particleSystems->objectAtIndex(i));
            if (p) {
                p->stopSystem();
                p->setVisible(false);
            }
        }
    }
}
