#include "FileUtils.h"
#include <fstream>
#include <windows.h>
#include <shlobj.h>

namespace chimera::utils {

bool FileUtils::readFile(const std::filesystem::path &path, std::string &outData) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    outData.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    return true;
}

bool FileUtils::writeFile(const std::filesystem::path &path, const std::string &data) {
    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(data.data(), data.size());
    return f.good();
}

bool FileUtils::ensureDir(const std::filesystem::path &path) {
    return std::filesystem::create_directories(path) || std::filesystem::is_directory(path);
}

std::vector<std::filesystem::path> FileUtils::listFiles(const std::filesystem::path &dir, const std::string &ext) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(dir)) return result;
    for (auto &entry : std::filesystem::directory_iterator(dir)) {
        if (entry.is_regular_file()) {
            if (ext.empty() || entry.path().extension() == ext) {
                result.push_back(entry.path());
            }
        }
    }
    return result;
}

std::filesystem::path FileUtils::appDataDir() {
    char path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, path))) {
        return std::filesystem::path(path) / "Chimera";
    }
    return std::filesystem::path(".");
}

} // namespace chimera::utils
