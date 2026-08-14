#pragma once
#include "DataTypes.hpp"
#include <Geode/Geode.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

using namespace geode::prelude;

// ==========================================
// Ghost Victors — .gghost binary serializer
// See docs/SDS.md §4.2.B and docs/plans/phase1-recording.plan.md
//
// Layout on disk (formatVersion 1): ReplayHeader (68 B) followed by
// header.totalFrames * FrameData (17 B each). Binary-independent of any GD binding.
// ==========================================

class ReplaySerializer {
public:
    // Write header (68 B) + frame array to `path`, creating parent directories as needed.
    // Returns true on success.
    static bool saveToFile(const std::filesystem::path& path,
                           const ReplayHeader& header,
                           const std::vector<FrameData>& frames) {
        std::error_code ec;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                log::error("ReplaySerializer: cannot create dir {}: {}",
                           path.parent_path().string(), ec.message());
                return false;
            }
        }

        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            log::error("ReplaySerializer: cannot open for write: {}", path.string());
            return false;
        }

        out.write(reinterpret_cast<const char*>(&header), sizeof(ReplayHeader));
        if (!frames.empty()) {
            out.write(reinterpret_cast<const char*>(frames.data()),
                      static_cast<std::streamsize>(frames.size() * sizeof(FrameData)));
        }
        out.flush();

        const bool ok = out.good();
        if (!ok) log::error("ReplaySerializer: write failed: {}", path.string());
        return ok;
    }

    // Read `path`, verifying magic, formatVersion, and that the body size matches totalFrames.
    // On success fills outHeader/outFrames and returns true; on any mismatch logs + returns false.
    static bool loadFromFile(const std::filesystem::path& path,
                             ReplayHeader& outHeader,
                             std::vector<FrameData>& outFrames) {
        std::error_code ec;
        const auto fileSize = std::filesystem::file_size(path, ec);
        if (ec) {
            log::error("ReplaySerializer: cannot stat {}: {}", path.string(), ec.message());
            return false;
        }
        if (fileSize < sizeof(ReplayHeader)) {
            log::error("ReplaySerializer: file smaller than header ({} B): {}",
                       fileSize, path.string());
            return false;
        }

        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) {
            log::error("ReplaySerializer: cannot open for read: {}", path.string());
            return false;
        }

        in.read(reinterpret_cast<char*>(&outHeader), sizeof(ReplayHeader));
        if (!in.good()) {
            log::error("ReplaySerializer: header read failed: {}", path.string());
            return false;
        }

        // Verify magic bytes "GGST".
        if (std::memcmp(outHeader.magic, "GGST", 4) != 0) {
            log::error("ReplaySerializer: bad magic (not a .gghost file): {}", path.string());
            return false;
        }
        // Verify format version.
        if (outHeader.formatVersion != 1) {
            log::error("ReplaySerializer: unsupported formatVersion {}: {}",
                       outHeader.formatVersion, path.string());
            return false;
        }
        // Verify body size matches the declared frame count.
        const uint64_t expectedBody =
            static_cast<uint64_t>(outHeader.totalFrames) * sizeof(FrameData);
        if (static_cast<uint64_t>(fileSize) - sizeof(ReplayHeader) != expectedBody) {
            log::error("ReplaySerializer: body size mismatch (have {} B, expect {} B): {}",
                       static_cast<uint64_t>(fileSize) - sizeof(ReplayHeader), expectedBody,
                       path.string());
            return false;
        }

        outFrames.resize(outHeader.totalFrames);
        if (outHeader.totalFrames > 0) {
            in.read(reinterpret_cast<char*>(outFrames.data()),
                    static_cast<std::streamsize>(expectedBody));
            if (!in.good()) {
                log::error("ReplaySerializer: frame read failed: {}", path.string());
                return false;
            }
        }
        return true;
    }
};
