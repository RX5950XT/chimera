#pragma once

#include <string>
#include <cstdint>
#include <filesystem>

namespace chimera::instance {

/**
 * @brief Analyzes and compacts instance data directories to reclaim disk space.
 *
 * Safe operations:
 *   - Report directory size breakdown
 *   - Remove *.tmp, *.log, crash dump files
 *   - Delete cache.img (Android rebuilds it on boot)
 *
 * Advanced (optional):
 *   - Zero-fill free space inside instance directory to allow
 *     host-level sparse file / VHDX compaction.
 */
class DiskCompactor {
public:
    struct SizeReport {
        uint64_t totalBytes = 0;
        uint64_t cacheBytes = 0;
        uint64_t logBytes = 0;
        uint64_t tempBytes = 0;
        uint64_t otherBytes = 0;
    };

    struct CompactionResult {
        bool success = false;
        uint64_t bytesBefore = 0;
        uint64_t bytesAfter = 0;
        uint64_t bytesReclaimed = 0;
        std::string message;
    };

    /**
     * @brief Analyze an instance directory without modifying anything.
     */
    static SizeReport analyzeInstance(const std::string &instanceName);

    /**
     * @brief Perform safe cleanup: temp files, logs, cache.img.
     */
    static CompactionResult compactInstance(const std::string &instanceName);

    /**
     * @brief Zero-fill free space in the instance directory.
     *
     * Creates a large temporary file filled with zeros, then deletes it.
     * This allows sparse files / VHDX to reclaim unused blocks on the host.
     *
     * @param instanceName Target instance
     * @param maxFillMB    Maximum megabytes to write (safety cap)
     * @return Number of bytes actually zero-filled
     */
    static uint64_t zeroFillFreeSpace(const std::string &instanceName, uint64_t maxFillMB = 1024);

private:
    static std::filesystem::path getInstanceDataDir(const std::string &instanceName);
    static SizeReport analyzeDirectory(const std::filesystem::path &path);
};

} // namespace chimera::instance
