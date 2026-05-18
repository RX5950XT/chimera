#include "InputMapper.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>

namespace chimera::input {

using json = nlohmann::json;

static bool isSafeSchemeName(const std::string &name) {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}

InputMapper &InputMapper::instance() {
    static InputMapper inst;
    return inst;
}

bool InputMapper::loadScheme(const std::string &packageName) {
    if (!isSafeSchemeName(packageName)) return false;

    auto path = std::filesystem::path("configs/input") / (packageName + ".json");
    std::ifstream f(path);
    if (!f.is_open()) return false;
    try {
        json j;
        f >> j;
        m_mappings.clear();
        for (auto &item : j["mappings"]) {
            InputMapping m;
            m.type = item.value("type", "tap");
            m.x = item.value("x", 0.0f);
            m.y = item.value("y", 0.0f);
            m.width = item.value("width", 5.0f);
            m.height = item.value("height", 5.0f);
            m.key = item.value("key", "");
            m.altKey = item.value("altKey", "");
            m.guidance = item.value("guidance", "");
            m.guidanceCategory = item.value("guidanceCategory", "");
            m_mappings.push_back(m);
        }
        m_currentPackage = packageName;
        return true;
    } catch (...) {
        return false;
    }
}

bool InputMapper::saveScheme(const std::string &packageName) const {
    if (!isSafeSchemeName(packageName)) return false;

    auto dir = std::filesystem::path("configs/input");
    std::filesystem::create_directories(dir);
    auto path = dir / (packageName + ".json");
    std::ofstream f(path);
    if (!f.is_open()) return false;
    json j;
    j["package"] = packageName;
    json arr = json::array();
    for (auto &m : m_mappings) {
        json item;
        item["type"] = m.type;
        item["x"] = m.x;
        item["y"] = m.y;
        item["width"] = m.width;
        item["height"] = m.height;
        item["key"] = m.key;
        item["altKey"] = m.altKey;
        item["guidance"] = m.guidance;
        item["guidanceCategory"] = m.guidanceCategory;
        arr.push_back(item);
    }
    j["mappings"] = arr;
    f << j.dump(2);
    return true;
}

void InputMapper::addMapping(const InputMapping &mapping) {
    m_mappings.push_back(mapping);
}

void InputMapper::insertMapping(size_t index, const InputMapping &mapping) {
    if (index >= m_mappings.size()) {
        m_mappings.push_back(mapping);
    } else {
        m_mappings.insert(m_mappings.begin() + static_cast<std::ptrdiff_t>(index), mapping);
    }
}

void InputMapper::removeMapping(size_t index) {
    if (index < m_mappings.size()) {
        m_mappings.erase(m_mappings.begin() + index);
    }
}

void InputMapper::clearMappings() {
    m_mappings.clear();
}

const InputMapping *InputMapper::findMappingByKey(const std::string &key) const {
    for (auto &m : m_mappings) {
        if (m.key == key || m.altKey == key) return &m;
    }
    return nullptr;
}

int InputMapper::normToPixel(float norm, int dimension) {
    return static_cast<int>((norm / 100.0f) * dimension);
}

} // namespace chimera::input
