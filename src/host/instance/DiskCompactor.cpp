#include "DiskCompactor.h"
#include <QDebug>
#include <fstream>
#include <vector>
#include <algorithm>

using namespace chimera::instance;

namespace fs = std::filesystem;

namespace {

constexpr uint64_t MB = 1024 * 1024;

bool isCacheFile(const fs::path &p) {
    std::string name = p.filename().string();
    return name == "cache.img" || name == "cache.img.tmp";
}

bool isLogFile(const fs::path &p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".log" || ext == ".dmp" || ext == ".crash";
}

bool isTempFile(const fs::path &p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".tmp" || ext == ".temp" || ext == ".bak";
}

uint64_t fileSize(const fs::path &p) {
    try {
        return static_cast<uint64_t>(fs::file_size(p));
    } catch (...) {
        return 0;
    }
}

} // anonymous namespace

fs::path DiskCompactor::getInstanceDataDir(const std::string &instanceName) {
    const char *userProfile = std::getenv("USERPROFILE");
    if (!userProfile) userProfile = ".";
    return fs::path(userProfile) / ".android" / "avd" / (instanceName + ".avd");
}

DiskCompactor::SizeReport DiskCompactor::analyzeDirectory(const fs::path &path) {
    SizeReport report{};
    if (!fs::exists(path) || !fs::is_directory(path)) {
        return report;
    }

    try {
        for (const auto &entry : fs::recursive_directory_iterator(path, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;

            uint64_t sz = fileSize(entry.path());
            report.totalBytes += sz;

            if (isCacheFile(entry.path())) {
                report.cacheBytes += sz;
            } else if (isLogFile(entry.path())) {
                report.logBytes += sz;
            } else if (isTempFile(entry.path())) {
                report.tempBytes += sz;
            } else {
                report.otherBytes += sz;
            }
        }
    } catch (const std::exception &e) {
        qWarning() << "[DiskCompactor] analyzeDirectory error:" << e.what();
    }

    return report;
}

DiskCompactor::SizeReport DiskCompactor::analyzeInstance(const std::string &instanceName) {
    return analyzeDirectory(getInstanceDataDir(instanceName));
}

DiskCompactor::CompactionResult DiskCompactor::compactInstance(const std::string &instanceName) {
    CompactionResult result;
    fs::path dir = getInstanceDataDir(instanceName);

    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        result.message = "Instance directory not found: " + dir.string();
        return result;
    }

    result.bytesBefore = analyzeDirectory(dir).totalBytes;

    uint64_t reclaimed = 0;
    try {
        for (const auto &entry : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied)) {
            if (!entry.is_regular_file()) continue;

            bool shouldDelete = isCacheFile(entry.path()) || isLogFile(entry.path()) || isTempFile(entry.path());
            if (shouldDelete) {
                uint64_t sz = fileSize(entry.path());
                try {
                    fs::remove(entry.path());
                    reclaimed += sz;
                    qDebug() << "[DiskCompactor] Deleted:" << QString::fromStdString(entry.path().string())
                             << "size:" << (sz / MB) << "MB";
                } catch (const std::exception &e) {
                    qWarning() << "[DiskCompactor] Failed to delete"
                               << QString::fromStdString(entry.path().string())
                               << ":" << e.what();
                }
            }
        }
    } catch (const std::exception &e) {
        result.message = std::string("Compaction error: ") + e.what();
        return result;
    }

    result.bytesAfter = analyzeDirectory(dir).totalBytes;
    result.bytesReclaimed = result.bytesBefore - result.bytesAfter;
    result.success = true;
    result.message = "Reclaimed " + std::to_string(result.bytesReclaimed / MB) + " MB";
    return result;
}

uint64_t DiskCompactor::zeroFillFreeSpace(const std::string &instanceName, uint64_t maxFillMB) {
    fs::path dir = getInstanceDataDir(instanceName);
    if (!fs::exists(dir) || !fs::is_directory(dir)) {
        qWarning() << "[DiskCompactor] zeroFill: directory not found";
        return 0;
    }

    fs::path fillFile = dir / "_chimera_zero_fill.tmp";
    constexpr uint64_t CHUNK = 8 * MB;
    const uint64_t maxBytes = maxFillMB * MB;
    std::vector<char> zeros(CHUNK, 0);

    uint64_t written = 0;
    try {
        std::ofstream ofs(fillFile, std::ios::binary | std::ios::app);
        if (!ofs) {
            qWarning() << "[DiskCompactor] zeroFill: cannot create temp file";
            return 0;
        }

        while (written < maxBytes) {
            ofs.write(zeros.data(), static_cast<std::streamsize>(CHUNK));
            if (!ofs) break;
            written += CHUNK;
        }
        ofs.close();

        fs::remove(fillFile);
        qDebug() << "[DiskCompactor] Zero-filled" << (written / MB) << "MB in"
                 << QString::fromStdString(instanceName);
    } catch (const std::exception &e) {
        qWarning() << "[DiskCompactor] zeroFill exception:" << e.what();
        try { fs::remove(fillFile); } catch (...) {}
    }

    return written;
}
