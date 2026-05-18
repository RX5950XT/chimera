#include "MemoryTrimmer.h"
#include "ProcessLauncher.h"

#include <QDebug>
#include <sstream>
#include <chrono>
#include <thread>
#include <cstdlib>

using namespace chimera::instance;

namespace {

/**
 * @brief Parse /proc/meminfo output to extract a value in kB.
 */
bool parseMeminfoValue(const std::string &meminfo, const std::string &key, qint64 &outKb) {
    size_t pos = meminfo.find(key);
    if (pos == std::string::npos) return false;

    pos += key.length();
    // Skip whitespace and colon
    while (pos < meminfo.size() && (meminfo[pos] == ':' || meminfo[pos] == ' ' || meminfo[pos] == '\t')) {
        ++pos;
    }

    char *end = nullptr;
    const char *start = meminfo.c_str() + pos;
    long long val = std::strtoll(start, &end, 10);
    if (end == start) return false;

    outKb = static_cast<qint64>(val);
    return true;
}

MemoryTrimmer::PressureLevel calculatePressureLevel(qint64 availableKB, qint64 totalKB) {
    if (totalKB <= 0) return MemoryTrimmer::PressureNone;
    double ratio = static_cast<double>(availableKB) / static_cast<double>(totalKB);
    if (ratio < MemoryTrimmer::THRESHOLD_CRITICAL) return MemoryTrimmer::PressureCritical;
    if (ratio < MemoryTrimmer::THRESHOLD_LOW) return MemoryTrimmer::PressureLow;
    if (ratio < MemoryTrimmer::THRESHOLD_MODERATE) return MemoryTrimmer::PressureModerate;
    return MemoryTrimmer::PressureNone;
}

} // anonymous namespace

MemoryTrimmer::MemoryTrimmer(QObject *parent)
    : QObject(parent) {
}

MemoryTrimmer::~MemoryTrimmer() {
    stopMonitoring();
}

void MemoryTrimmer::startMonitoring(int intervalMs) {
    if (m_monitoring.load()) return;
    if (intervalMs < 1000) intervalMs = 1000;

    m_stopRequested.store(false);
    m_monitoring.store(true);
    emit monitoringChanged(true);

    m_worker = std::thread(&MemoryTrimmer::monitorLoop, this, intervalMs);
}

void MemoryTrimmer::stopMonitoring() {
    if (!m_monitoring.load()) return;

    m_stopRequested.store(true);
    if (m_worker.joinable()) {
        m_worker.join();
    }
    m_monitoring.store(false);
    emit monitoringChanged(false);
}

void MemoryTrimmer::monitorLoop(int intervalMs) {
    using clock = std::chrono::steady_clock;
    const auto sleepDuration = std::chrono::milliseconds(intervalMs);

    while (!m_stopRequested.load()) {
        auto nextWake = clock::now() + sleepDuration;

        qint64 totalKB = 0, availableKB = 0;
        if (fetchMemoryStats(totalKB, availableKB)) {
            qint64 total = totalKB / 1024;
            qint64 available = availableKB / 1024;
            qint64 used = total - available;
            if (used < 0) used = 0;

            m_totalMB.store(total);
            m_usedMB.store(used);
            m_availableMB.store(available);
            emit memoryStatsUpdated(total, used, available);

            int newLevel = calculatePressureLevel(availableKB, totalKB);
            int oldLevel = m_pressureLevel.exchange(newLevel);
            if (newLevel != oldLevel) {
                emit memoryPressureChanged(newLevel);
                qDebug() << "[MemoryTrimmer] Pressure changed:" << oldLevel << "->" << newLevel
                         << "(available:" << available << "MB / total:" << total << "MB)";
            }

            // Auto-trim on critical pressure
            if (newLevel == PressureCritical && oldLevel != PressureCritical) {
                qDebug() << "[MemoryTrimmer] Auto-trimming due to critical pressure";
                trimMemory(PressureCritical);
            }
        }

        std::this_thread::sleep_until(nextWake);
    }
}

bool MemoryTrimmer::fetchMemoryStats(qint64 &totalKB, qint64 &availableKB) {
    // ADB path discovery: prefer bundled adb, fallback to PATH
    std::string adb = "adb";
    const char *androidHome = std::getenv("ANDROID_HOME");
    if (androidHome) {
        adb = std::string(androidHome) + "\\platform-tools\\adb.exe";
    }

    auto result = ProcessLauncher::runSync(adb, {"shell", "cat", "/proc/meminfo"});
    if (result.exitCode != 0 || result.stdoutText.empty()) {
        return false;
    }

    qint64 memFree = 0, buffers = 0, cached = 0;
    if (!parseMeminfoValue(result.stdoutText, "MemTotal", totalKB)) return false;
    parseMeminfoValue(result.stdoutText, "MemFree", memFree);
    parseMeminfoValue(result.stdoutText, "Buffers", buffers);
    parseMeminfoValue(result.stdoutText, "Cached", cached);

    availableKB = memFree + buffers + cached;
    return true;
}

void MemoryTrimmer::trimMemory(int level) {
    if (!sendTrimCommand(level)) {
        qWarning() << "[MemoryTrimmer] Failed to send trim command";
    }
}

void MemoryTrimmer::aggressiveTrim() {
    // Drop caches first (works without root for level 1, needs root for level 3)
    dropCaches();

    // Send framework trim to all packages
    sendTrimCommand(PressureCritical);
}

bool MemoryTrimmer::sendTrimCommand(int level) {
    std::string adb = "adb";
    const char *androidHome = std::getenv("ANDROID_HOME");
    if (androidHome) {
        adb = std::string(androidHome) + "\\platform-tools\\adb.exe";
    }

    // Map our pressure level to Android memory-factor names (Android 12+).
    // "am memory-factor set <FACTOR>" triggers system-wide LRU reclamation without
    // needing a specific managed process PID.
    std::string factor;
    switch (level) {
        case PressureModerate: factor = "MODERATE"; break;
        case PressureLow:      factor = "LOW";      break;
        case PressureCritical: factor = "CRITICAL"; break;
        default:               factor = "MODERATE"; break;
    }

    auto result = ProcessLauncher::runSync(adb, {
        "-s", m_adbSerial, "shell", "am", "memory-factor", "set", factor
    });

    if (result.exitCode != 0) {
        qDebug() << "[MemoryTrimmer] memory-factor set failed:" << QString::fromStdString(result.stderrText);
    }
    return result.exitCode == 0;
}

bool MemoryTrimmer::dropCaches() {
    std::string adb = "adb";
    const char *androidHome = std::getenv("ANDROID_HOME");
    if (androidHome) {
        adb = std::string(androidHome) + "\\platform-tools\\adb.exe";
    }

    // Level 1: drop pagecache (safe, no root usually needed for emulator)
    auto result = ProcessLauncher::runSync(adb, {
        "shell", "su", "-c", "echo 1 > /proc/sys/vm/drop_caches"
    });

    if (result.exitCode != 0) {
        // Fallback without su (works on some emulator images with root shell)
        result = ProcessLauncher::runSync(adb, {
            "shell", "echo 1 > /proc/sys/vm/drop_caches"
        });
    }

    return result.exitCode == 0;
}
