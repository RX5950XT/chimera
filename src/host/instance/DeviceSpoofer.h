#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace chimera::instance {

/**
 * @brief Modifies Android build.prop to spoof a different device model.
 *
 * Games often lock graphics settings (FPS, resolution, quality) behind
 * device model detection. By modifying build.prop, we can unlock these.
 */
struct DeviceProfile {
    std::string name;
    std::string manufacturer;
    std::string model;
    std::string device;
    std::string product;
    std::string brand;
    std::string board;
    int sdkVersion = 34;
    std::string release = "14";
    int dpi = 480;
    int screenWidth = 1920;
    int screenHeight = 1080;
};

class DeviceSpoofer {
public:
    static DeviceSpoofer &instance();

    // Predefined profiles matching popular gaming phones
    static std::vector<DeviceProfile> getBuiltinProfiles();

    // Apply profile to AVD's build.prop
    bool applyProfile(const DeviceProfile &profile, const std::string &avdName);

    // Read current build.prop values
    std::map<std::string, std::string> readBuildProp(const std::string &avdName);

private:
    DeviceSpoofer() = default;
    std::filesystem::path getAvdDirectory(const std::string &avdName);
    bool modifyBuildProp(const std::filesystem::path &buildPropPath,
                         const std::map<std::string, std::string> &overrides);
};

} // namespace chimera::instance
