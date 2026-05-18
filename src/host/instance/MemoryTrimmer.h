#pragma once

#include <QObject>
#include <QString>
#include <QMetaType>
#include <atomic>
#include <thread>
#include <cstdint>

namespace chimera::instance {

/**
 * @brief Monitors Android guest memory pressure and triggers trimming.
 *
 * Polls /proc/meminfo via ADB to detect low-memory conditions.
 * When pressure is elevated, sends trim commands to the Android framework
 * to free caches and background process memory.
 */
class MemoryTrimmer : public QObject {
    Q_OBJECT

    Q_PROPERTY(int memoryPressureLevel READ memoryPressureLevel NOTIFY memoryPressureChanged)
    Q_PROPERTY(qint64 totalMB READ totalMB NOTIFY memoryStatsUpdated)
    Q_PROPERTY(qint64 usedMB READ usedMB NOTIFY memoryStatsUpdated)
    Q_PROPERTY(qint64 availableMB READ availableMB NOTIFY memoryStatsUpdated)
    Q_PROPERTY(bool monitoring READ monitoring NOTIFY monitoringChanged)

public:
    enum PressureLevel {
        PressureNone = 0,
        PressureModerate = 1,
        PressureLow = 2,
        PressureCritical = 3
    };
    Q_ENUM(PressureLevel)

    explicit MemoryTrimmer(QObject *parent = nullptr);
    ~MemoryTrimmer() override;

    int memoryPressureLevel() const { return m_pressureLevel.load(); }
    qint64 totalMB() const { return m_totalMB.load(); }
    qint64 usedMB() const { return m_usedMB.load(); }
    qint64 availableMB() const { return m_availableMB.load(); }
    bool monitoring() const { return m_monitoring.load(); }

    void setAdbSerial(const std::string &serial) { m_adbSerial = serial; }
    const std::string &adbSerial() const { return m_adbSerial; }

public slots:
    void startMonitoring(int intervalMs = 5000);
    void stopMonitoring();

    /**
     * @brief Manually trigger a memory trim at the given level.
     *
     * @param level 0=None (drop caches), 1=Moderate, 2=Low, 3=Critical
     */
    void trimMemory(int level);

    /**
     * @brief Perform an aggressive trim: drop caches + kill background processes.
     *
     * Requires root access on the guest.
     */
    void aggressiveTrim();

signals:
    void memoryPressureChanged(int level);
    void memoryStatsUpdated(qint64 totalMB, qint64 usedMB, qint64 availableMB);
    void monitoringChanged(bool active);

private:
    void monitorLoop(int intervalMs);
    bool fetchMemoryStats(qint64 &totalKB, qint64 &availableKB);
    bool sendTrimCommand(int level);
    bool dropCaches();

    std::atomic<int> m_pressureLevel{PressureNone};
    std::atomic<qint64> m_totalMB{0};
    std::atomic<qint64> m_usedMB{0};
    std::atomic<qint64> m_availableMB{0};
    std::atomic<bool> m_monitoring{false};
    std::atomic<bool> m_stopRequested{false};
    std::thread m_worker;
    std::string m_adbSerial = "emulator-5554";

public:
    // Thresholds (available / total ratio)
    static constexpr double THRESHOLD_MODERATE = 0.25;
    static constexpr double THRESHOLD_LOW = 0.15;
    static constexpr double THRESHOLD_CRITICAL = 0.08;
};

} // namespace chimera::instance
