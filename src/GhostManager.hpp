#pragma once
#include "DataTypes.hpp"
#include <string>
#include <vector>
#include <optional>

// ==========================================
// Ghost Victors — global state singleton
// See docs/SDS.md §4.2.A and docs/plans/phase0-data-state.plan.md
// ==========================================

// Central manager holding active state, selected victor metadata, and replay buffers across layers.
class GhostManager {
private:
    GhostManager() = default;

    std::optional<std::string> m_activeVictorID;
    std::vector<FrameData> m_loadedGhostFrames;
    std::vector<FrameData> m_recordingBuffer;
    bool m_isGhostVisibleInPause = true;

public:
    // Single global instance — non-copyable, non-movable.
    GhostManager(const GhostManager&) = delete;
    GhostManager& operator=(const GhostManager&) = delete;
    GhostManager(GhostManager&&) = delete;
    GhostManager& operator=(GhostManager&&) = delete;

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
