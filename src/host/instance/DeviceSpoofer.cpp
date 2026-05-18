#include "DeviceSpoofer.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <set>

namespace chimera::instance {

DeviceSpoofer &DeviceSpoofer::instance() {
    static DeviceSpoofer inst;
    return inst;
}

std::vector<DeviceProfile> DeviceSpoofer::getBuiltinProfiles() {
    return {
        {
            "Samsung Galaxy S24 Ultra",
            "samsung",
            "SM-S928U",
            "e3q",
            "e3qsqw",
            "samsung",
            "snapdragon",
            34,
            "14",
            480,
            3120,
            1440
        },
        {
            "OnePlus 12",
            "OnePlus",
            "CPH2581",
            "waffle",
            "waffle",
            "OnePlus",
            "snapdragon",
            34,
            "14",
            480,
            3168,
            1440
        },
        {
            "ASUS ROG Phone 8",
            "asus",
            "AI2401",
            "AI2401",
            "WW_AI2401",
            "asus",
            "snapdragon",
            34,
            "14",
            480,
            2448,
            1080
        },
        {
            "Xiaomi 14 Pro",
            "Xiaomi",
            "23116PN5BC",
            "shennong",
            "shennong",
            "Xiaomi",
            "snapdragon",
            34,
            "14",
            480,
            3200,
            1440
        },
        {
            "Google Pixel 8 Pro",
            "google",
            "GC3VE",
            "husky",
            "husky",
            "google",
            "tensor",
            34,
            "14",
            480,
            2992,
            1344
        }
    };
}

std::filesystem::path DeviceSpoofer::getAvdDirectory(const std::string &avdName) {
    auto avdHome = std::getenv("ANDROID_AVD_HOME");
    if (!avdHome) {
        auto userProfile = std::getenv("USERPROFILE");
        if (!userProfile) return {};
        return std::filesystem::path(userProfile) / ".android" / "avd" / (avdName + ".avd");
    }
    return std::filesystem::path(avdHome) / (avdName + ".avd");
}

std::map<std::string, std::string> DeviceSpoofer::readBuildProp(const std::string &avdName) {
    std::map<std::string, std::string> props;
    auto avdDir = getAvdDirectory(avdName);
    if (avdDir.empty()) return props;

    // Try system partition first (read-only, but we can read)
    auto buildProp = avdDir / "system/build.prop";
    if (!std::filesystem::exists(buildProp)) {
        // Fallback to root build.prop in system image
        buildProp = avdDir / "build.prop";
    }

    std::ifstream f(buildProp);
    if (!f.is_open()) return props;

    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto pos = line.find('=');
        if (pos != std::string::npos) {
            props[line.substr(0, pos)] = line.substr(pos + 1);
        }
    }
    return props;
}

bool DeviceSpoofer::modifyBuildProp(const std::filesystem::path &buildPropPath,
                                    const std::map<std::string, std::string> &overrides) {
    if (!std::filesystem::exists(buildPropPath)) {
        // Create new file
        std::ofstream out(buildPropPath);
        if (!out.is_open()) return false;
        for (auto &[key, val] : overrides) {
            out << key << "=" << val << "\n";
        }
        return true;
    }

    // Read existing
    std::ifstream in(buildPropPath);
    if (!in.is_open()) return false;
    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    in.close();

    // Apply overrides
    std::set<std::string> modified;
    for (auto &l : lines) {
        if (l.empty() || l[0] == '#') continue;
        auto pos = l.find('=');
        if (pos != std::string::npos) {
            std::string key = l.substr(0, pos);
            auto it = overrides.find(key);
            if (it != overrides.end()) {
                l = key + "=" + it->second;
                modified.insert(key);
            }
        }
    }

    // Append any new keys
    for (auto &[key, val] : overrides) {
        if (!modified.count(key)) {
            lines.push_back(key + "=" + val);
        }
    }

    // Write back
    std::ofstream out(buildPropPath);
    if (!out.is_open()) return false;
    for (auto &l : lines) {
        out << l << "\n";
    }
    return true;
}

bool DeviceSpoofer::applyProfile(const DeviceProfile &profile, const std::string &avdName) {
    auto avdDir = getAvdDirectory(avdName);
    if (avdDir.empty() || !std::filesystem::exists(avdDir / "config.ini")) return false;

    // Create overlay directory for build.prop modifications
    auto overlayDir = avdDir / "overlay" / "system";
    std::filesystem::create_directories(overlayDir);

    std::map<std::string, std::string> overrides = {
        {"ro.product.manufacturer", profile.manufacturer},
        {"ro.product.model", profile.model},
        {"ro.product.device", profile.device},
        {"ro.product.name", profile.product},
        {"ro.product.brand", profile.brand},
        {"ro.product.board", profile.board},
        {"ro.build.version.sdk", std::to_string(profile.sdkVersion)},
        {"ro.build.version.release", profile.release},
        {"ro.sf.lcd_density", std::to_string(profile.dpi)},
    };

    auto buildProp = overlayDir / "build.prop";
    if (!modifyBuildProp(buildProp, overrides)) return false;

    std::cout << "Device spoofing applied: " << profile.name << " to " << avdName << "\n";
    return true;
}

} // namespace chimera::instance
