#include "ConfigManager.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace chimera::config {

using json = nlohmann::json;

ConfigManager &ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

bool ConfigManager::loadFromFile(const std::filesystem::path &path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        if (j.is_object()) {
            for (auto &[k, v] : j.items()) {
                if (v.is_string()) m_data[k] = v.get<std::string>();
                else if (v.is_number_integer()) m_data[k] = std::to_string(v.get<int>());
                else if (v.is_boolean()) m_data[k] = v.get<bool>() ? "1" : "0";
                else if (v.is_number_float()) m_data[k] = std::to_string(v.get<double>());
            }
        }
    } catch (...) {
        return false;
    }
    return true;
}

bool ConfigManager::saveToFile(const std::filesystem::path &path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    json j;
    for (auto &[k, v] : m_data) j[k] = v;
    f << j.dump(2);
    return true;
}

std::string ConfigManager::getString(const std::string &key, const std::string &defaultValue) const {
    auto it = m_data.find(key);
    return (it != m_data.end()) ? it->second : defaultValue;
}

int ConfigManager::getInt(const std::string &key, int defaultValue) const {
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    try { return std::stoi(it->second); } catch (...) { return defaultValue; }
}

bool ConfigManager::getBool(const std::string &key, bool defaultValue) const {
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    return it->second == "1" || it->second == "true" || it->second == "True";
}

double ConfigManager::getDouble(const std::string &key, double defaultValue) const {
    auto it = m_data.find(key);
    if (it == m_data.end()) return defaultValue;
    try { return std::stod(it->second); } catch (...) { return defaultValue; }
}

void ConfigManager::setString(const std::string &key, const std::string &value) {
    m_data[key] = value;
}

void ConfigManager::setInt(const std::string &key, int value) {
    m_data[key] = std::to_string(value);
}

void ConfigManager::setBool(const std::string &key, bool value) {
    m_data[key] = value ? "1" : "0";
}

void ConfigManager::setDouble(const std::string &key, double value) {
    m_data[key] = std::to_string(value);
}

std::vector<std::string> ConfigManager::listInstances() const {
    std::vector<std::string> names;
    for (auto &[k, v] : m_data) {
        if (k.starts_with("instance.")) {
            auto pos = k.find('.', 9);
            if (pos != std::string::npos) {
                std::string name = k.substr(9, pos - 9);
                if (std::find(names.begin(), names.end(), name) == names.end()) {
                    names.push_back(name);
                }
            }
        }
    }
    return names;
}

void ConfigManager::createInstance(const std::string &name) {
    std::string prefix = "instance." + name + ".";
    setInt(prefix + "cpus", 4);
    setInt(prefix + "ram", 4096);
    setInt(prefix + "dpi", 240);
    setInt(prefix + "fb_width", 1920);
    setInt(prefix + "fb_height", 1080);
    setString(prefix + "graphics_engine", "angle");
    setString(prefix + "graphics_renderer", "d3d11");
    setInt(prefix + "max_fps", 60);
    setBool(prefix + "enable_vsync", false);
    setBool(prefix + "game_controls_enabled", true);
    setBool(prefix + "enable_notifications", true);
}

void ConfigManager::removeInstance(const std::string &name) {
    std::string prefix = "instance." + name + ".";
    for (auto it = m_data.begin(); it != m_data.end();) {
        if (it->first.starts_with(prefix)) it = m_data.erase(it);
        else ++it;
    }
}

void ConfigManager::merge(const ConfigManager &other) {
    for (auto &[k, v] : other.m_data) {
        m_data[k] = v;
    }
}

} // namespace chimera::config
