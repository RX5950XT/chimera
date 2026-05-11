#pragma once

#include <string>
#include <filesystem>
#include <functional>

namespace chimera::storage {

/**
 * @brief Manages host-guest shared folders (9pfs / VirtIO-FS).
 *
 * Mounts host directories into the guest Android filesystem.
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

    bool addMount(const Mount &mount);
    void removeMount(const std::string &tag);
    std::vector<Mount> listMounts() const;

    // Generate QEMU -virtfs command line arguments
    std::vector<std::string> toQemuArgs() const;

private:
    SharedFolder() = default;
    std::vector<Mount> m_mounts;
};

} // namespace chimera::storage
