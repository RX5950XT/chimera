#pragma once

#include <QObject>
#include <QString>
#include <memory>

namespace chimera::instance {

/**
 * @brief Hyper-V Host Compute Service (HCS) wrapper for GPU-PV.
 *
 * Provides Windows-native VM creation with GPU paravirtualization.
 * Falls back to QEMU/emulator.exe when HCS is unavailable.
 *
 * Requires:
 *   - Windows 10 Pro/Enterprise build 19041+ or Windows 11
 *   - Hyper-V enabled
 *   - Compatible GPU (Intel Gen 9+, AMD RX 5000+, NVIDIA RTX 20+)
 */
class HyperVManager : public QObject {
    Q_OBJECT

public:
    enum GpuPartitionMode {
        GpuNone = 0,      // No GPU passthrough
        GpuPartition = 1, // GPU-PV (shared partition)
        GpuDDA = 2        // Discrete Device Assignment (exclusive)
    };
    Q_ENUM(GpuPartitionMode)

    struct HcsConfig {
        QString name;
        int cpus = 4;
        int ramMB = 4096;
        int gpuPartitionCount = 0; // 0 = auto-detect
        GpuPartitionMode gpuMode = GpuPartition;
        QString systemImagePath;
        QString dataImagePath;
    };

    explicit HyperVManager(QObject *parent = nullptr);
    ~HyperVManager();

    /**
     * @brief Check if HCS is available on this system.
     */
    static bool isAvailable();

    /**
     * @brief Check if GPU-PV is supported by the GPU.
     */
    static bool isGpuPartitionSupported();

    /**
     * @brief Get GPU partition info.
     */
    static int availableGpuPartitions();

    /**
     * @brief Create a VM using HCS (experimental).
     */
    bool createVm(const HcsConfig &config);

    /**
     * @brief Start the VM.
     */
    bool startVm();

    /**
     * @brief Stop the VM.
     */
    bool stopVm();

    bool isRunning() const;
    QString lastError() const;

signals:
    void stateChanged(bool running);
    void error(const QString &message);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera::instance
