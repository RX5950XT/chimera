#pragma once

#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace chimera::input {

/**
 * @brief JSON-based input mapping scheme, inspired by BlueStacks .cfg format.
 *
 * Each game package has its own scheme file:
 *   configs/input/<package_name>.json
 */
struct InputMapping {
    std::string type;       // "tap", "swipe", "dpad", "moba_skill", "native_cursor"
    float x, y;             // Normalized position [0.0, 100.0] (percentage of screen)
    float width, height;    // Normalized size
    std::string key;        // Primary key (e.g., "A", "Space", "GamepadA")
    std::string altKey;     // Alternative binding
    std::string guidance;   // Human-readable label
    std::string guidanceCategory;
};

class InputMapper {
public:
    static InputMapper &instance();

    bool loadScheme(const std::string &packageName);
    bool saveScheme(const std::string &packageName) const;

    const std::vector<InputMapping> &getMappings() const { return m_mappings; }
    void addMapping(const InputMapping &mapping);
    void removeMapping(size_t index);
    void clearMappings();

    // Query which mapping (if any) is triggered by a key press
    const InputMapping *findMappingByKey(const std::string &key) const;

    // Convert normalized coordinates to absolute pixel coordinates
    static int normToPixel(float norm, int dimension);

private:
    InputMapper() = default;
    std::vector<InputMapping> m_mappings;
    std::string m_currentPackage;
};

} // namespace chimera::input
