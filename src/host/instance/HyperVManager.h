#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

namespace chimera::instance {

/**
 * @brief Hyper-V Host Compute Service (HCS) wrapper for GPU-PV Android virtualization.
 *
 * Implements Phase 5 virtualization backend using Windows HCS API (computecore.dll).
 * Creates Hyper-V VMs with LinuxKernelDirect boot and GPU partition support.
 *
 * Requirements:
 *   - Windows 10 Pro/Enterprise build 19041+ or Windows 11
 *   - Hyper-V feature enabled (bcdedit /set hypervisorlaunchtype auto)
 *   - computecore.dll + computestorage.dll present (standard with Hyper-V)
 *   - GPU: Intel Gen 9+, AMD RX 5000+, NVIDIA RTX 20+ for GPU-PV
 */
class HyperVManager : public QObject {
    Q_OBJECT

public:
    enum GpuPartitionMode {
        GpuNone = 0,       // Software rendering (Mesa llvmpipe)
        GpuPartition = 1,  // GPU-PV shared partition (AssignmentMode: Mirror)
        GpuDDA = 2         // Discrete Device Assignment — exclusive GPU
    };
    Q_ENUM(GpuPartitionMode)

    enum class State {
        Idle,       // Not yet created
        Creating,   // HcsCreateComputeSystem in progress
        Stopped,    // Created but not running
        Starting,   // HcsStartComputeSystem in progress
        Running,    // Guest OS active
        Stopping,   // HcsShutDownComputeSystem in progress
        Terminated, // Force-terminated
        Error       // Fatal error — check lastError()
    };
    Q_ENUM(State)

    struct HcsConfig {
        QString name;                       // Instance name (used as VM ID seed)
        int cpus = 4;
        int ramMB = 4096;
        GpuPartitionMode gpuMode = GpuPartition;

        // LinuxKernelDirect boot (avoids UEFI/bootloader complexity — same as WSL2/WSA)
        QString kernelPath;                 // bzImage path on host
        QString initrdPath;                 // initrd.img path (empty = no initrd)
        QString kernelCmdLine;              // e.g. "root=/dev/sda rw console=hvc0"

        // SCSI disk attachments (VHDX required — qcow2/img not accepted by HCS)
        QStringList readonlyDiskPaths;      // system.vhdx, vendor.vhdx
        QString writableDiskPath;           // userdata.vhdx (per-instance overlay)
    };

    explicit HyperVManager(QObject *parent = nullptr);
    ~HyperVManager() override;

    // System capability queries
    static bool isAvailable();
    static bool isGpuPartitionSupported();
    static int  availableGpuPartitions();

    // VM lifecycle — all async; watch stateChanged() signal for result
    bool createVm(const HcsConfig &config);
    bool startVm();
    bool stopVm();
    bool terminateVm();

    State   state() const;
    bool    isRunning() const;
    QString lastError() const;
    QString vmId() const;         // GUID we assigned — passed to HcsCreateComputeSystem
    QString partitionId() const;  // Actual HV partition GUID — use this for AF_HYPERV SOCKADDR_HV

    // Returns the HCS JSON document (for debugging / offline testing)
    static QString buildHcsJsonString(const HcsConfig &config);

signals:
    void stateChanged(chimera::instance::HyperVManager::State newState);
    void error(const QString &message);
    void consoleOutput(const QString &line);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera::instance
