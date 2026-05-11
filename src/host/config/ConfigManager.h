#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace chimera::config {

/**
 * @brief Central configuration manager, inspired by bluestacks.conf key-value format.
 *
 * Uses a flat key namespace (e.g., "instance.default.cpus", "graphics.engine")
 * stored in JSON for human readability and machine parseability.
 */
class ConfigManager {
public:
    static ConfigManager &instance();

    bool loadFromFile(const std::filesystem::path &path);
    bool saveToFile(const std::filesystem::path &path) const;

    // Getters with defaults
    std::string getString(const std::string &key, const std::string &defaultValue = {}) const;
    int getInt(const std::string &key, int defaultValue = 0) const;
    bool getBool(const std::string &key, bool defaultValue = false) const;
    double getDouble(const std::string &key, double defaultValue = 0.0) const;

    // Setters
    void setString(const std::string &key, const std::string &value);
    void setInt(const std::string &key, int value);
    void setBool(const std::string &key, bool value);
    void setDouble(const std::string &key, double value);

    // Instance-scoped keys (e.g., instance.<name>.ram)
    std::vector<std::string> listInstances() const;
    void createInstance(const std::string &name);
    void removeInstance(const std::string &name);

    // Merge external config (e.g., downloaded game-specific settings)
    void merge(const ConfigManager &other);

private:
    ConfigManager() = default;
    std::unordered_map<std::string, std::string> m_data;
};

} // namespace chimera::config
