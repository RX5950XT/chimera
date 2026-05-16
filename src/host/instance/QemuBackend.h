#pragma once

#include <QObject>
#include <QTimer>
#include <QString>
#include <string>
#include <vector>
#include <filesystem>
#include <windows.h>

namespace chimera::instance {

struct QemuInstanceConfig {
    std::filesystem::path qemuBinary;
    std::filesystem::path diskImage;
    std::filesystem::path cdromImage;   // optional ISO for Live CD / install
    std::string bootDevice = "c";       // "c"=disk, "d"=cdrom
    std::string vgaDevice  = "vmware";  // "vmware", "virtio", "std", etc.
    std::string machineType = "q35";
    std::string accel = "whpx";
    int cpus = 4;
    int ramMB = 4096;
    int vncDisplay = 0;     // :N → TCP port 5900 + N
    int qmpPort = 4444;
    int adbPort = 5560;
    bool enableAdb = true;
    std::string name;
    std::vector<std::string> extraArgs;
};

/**
 * @brief Manages a single stock QEMU process for Android-x86.
 *
 * Lifecycle: Stopped → Starting → Running → Stopped / Error.
 * Uses ProcessLauncher to spawn qemu-system-x86_64.exe with the validated
 * Phase 0 command line (WHPX, q35, XHCI USB, VNC, real QMP socket).
 */
class QemuBackend : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Error };

    explicit QemuBackend(const QemuInstanceConfig &config, QObject *parent = nullptr);
    ~QemuBackend() override;

    bool start();
    void stop();

    State state() const { return m_state; }
    int vncPort() const { return 5900 + m_config.vncDisplay; }
    int qmpPort() const { return m_config.qmpPort; }
    int adbPort() const { return m_config.adbPort; }
    QString errorMessage() const { return m_errorMessage; }

    std::vector<std::string> buildArgs() const;

signals:
    void stateChanged(State state);
    void errorOccurred(const QString &message);

private slots:
    void onHealthCheck();
    void onStartupTimeout();

private:
    void setState(State s);

    QemuInstanceConfig m_config;
    State m_state = State::Stopped;
    HANDLE m_processHandle = INVALID_HANDLE_VALUE;
    QString m_errorMessage;

    QTimer *m_healthCheckTimer = nullptr;
    QTimer *m_startupTimeoutTimer = nullptr;

    static constexpr int kStartupTimeoutMs = 30000;
    static constexpr int kHealthCheckIntervalMs = 2000;
};

} // namespace chimera::instance
