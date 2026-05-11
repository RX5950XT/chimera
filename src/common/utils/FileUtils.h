#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace chimera::utils {

class FileUtils {
public:
    static bool readFile(const std::filesystem::path &path, std::string &outData);
    static bool writeFile(const std::filesystem::path &path, const std::string &data);
    static bool ensureDir(const std::filesystem::path &path);
    static std::vector<std::filesystem::path> listFiles(const std::filesystem::path &dir, const std::string &ext = {});
    static std::filesystem::path appDataDir();
};

} // namespace chimera::utils
