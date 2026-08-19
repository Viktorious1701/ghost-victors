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

    // Phase 3: the run the player picked in the Victors popup, per level.
    // has_value() + empty string = explicit "no ghost"; has_value() + path = load that file;
    // nullopt = no explicit choice → PlayLayer defaults to the most recent run.
    int m_selectedLevelID = 0;
    std::optional<std::string> m_selectedRunPath;

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

    // Phase 3 (DP16): per-level selected run. Pass an empty path to mean "no ghost".
    void setSelectedRun(int levelID, const std::string& path) {
        m_selectedLevelID = levelID;
        m_selectedRunPath = path;
    }
    void clearSelectedRun() { m_selectedRunPath.reset(); }
    // Returns the chosen path (may be empty = "none") if a choice was made for this level, else nullopt.
    std::optional<std::string> getSelectedRunFor(int levelID) const {
        if (m_selectedRunPath.has_value() && m_selectedLevelID == levelID) return m_selectedRunPath;
        return std::nullopt;
    }
};
