#pragma once

#include <string>
#include <filesystem>
#include <functional>

namespace chimera::storage {

/**
 * @brief Host↔Guest file sharing.
 *
 * v1: ADB-based Downloads sync (see docs/ADR-001-shared-folder.md).
 * QEMU backend: -virtfs mounts via toQemuArgs().
 */
class SharedFolder {
public:
    struct Mount {
        std::string tag;           // Guest mount tag (e.g., "shared")
        std::filesystem::path hostPath;
        std::string guestPath;     // Guest mount point (e.g., "/mnt/shared")
        bool readOnly = false;
    };

    static SharedFolder &instance();

    // QEMU backend mounts
    bool addMount(const Mount &mount);
    void removeMount(const std::string &tag);
    std::vector<Mount> listMounts() const;
    std::vector<std::string> toQemuArgs() const;  // generates -virtfs args

    // v1 ADB-based sync (emulator.exe backend)
    // Must call setAdbConfig() before using push/pull.
    void setAdbConfig(const std::filesystem::path &adbExe, const std::string &serial);

    // Host → Guest: adb push <hostFile> /sdcard/Download/<filename>
    // Returns true on success (exit code 0).
    bool pushToGuest(const std::filesystem::path &hostFile) const;

    // Guest → Host: adb pull /sdcard/Download/<guestFilename> <hostDir>
    // Returns true on success.
    bool pullFromGuest(const std::string &guestFilename,
                       const std::filesystem::path &hostDir) const;

private:
    SharedFolder() = default;
    std::vector<Mount> m_mounts;
    std::filesystem::path m_adbExe;
    std::string m_adbSerial;
};

} // namespace chimera::storage
