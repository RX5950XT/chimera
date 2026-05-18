#include "QemuBackend.h"
#include "ProcessLauncher.h"
#include <QDebug>

namespace chimera::instance {

QemuBackend::QemuBackend(const QemuInstanceConfig &config, QObject *parent)
    : QObject(parent)
    , m_config(config)
    , m_healthCheckTimer(new QTimer(this))
    , m_startupTimeoutTimer(new QTimer(this))
{
    m_healthCheckTimer->setInterval(kHealthCheckIntervalMs);
    m_healthCheckTimer->setSingleShot(false);
    connect(m_healthCheckTimer, &QTimer::timeout, this, &QemuBackend::onHealthCheck);

    m_startupTimeoutTimer->setInterval(kStartupTimeoutMs);
    m_startupTimeoutTimer->setSingleShot(true);
    connect(m_startupTimeoutTimer, &QTimer::timeout, this, &QemuBackend::onStartupTimeout);
}

QemuBackend::~QemuBackend() {
    stop();
}

bool QemuBackend::start() {
    if (m_state == State::Running || m_state == State::Starting) return false;
    if (m_config.qemuBinary.empty() || !std::filesystem::exists(m_config.qemuBinary)) {
        m_errorMessage = QStringLiteral("QEMU binary not found: %1")
                            .arg(QString::fromStdString(m_config.qemuBinary.string()));
        setState(State::Error);
        return false;
    }
    if (m_config.mode == "cuttlefish") {
        if (m_config.kernelPath.empty() || !std::filesystem::exists(m_config.kernelPath)) {
            m_errorMessage = QStringLiteral("Cuttlefish kernel not found: %1")
                                .arg(QString::fromStdString(m_config.kernelPath.string()));
            setState(State::Error);
            return false;
        }
    } else {
        const bool hasDisk  = !m_config.diskImage.empty()  && std::filesystem::exists(m_config.diskImage);
        const bool hasCdrom = !m_config.cdromImage.empty() && std::filesystem::exists(m_config.cdromImage);
        if (!hasDisk && !hasCdrom) {
            m_errorMessage = QStringLiteral("No bootable media: disk '%1' and cdrom '%2' both missing")
                                .arg(QString::fromStdString(m_config.diskImage.string()))
                                .arg(QString::fromStdString(m_config.cdromImage.string()));
            setState(State::Error);
            return false;
        }
    }

    setState(State::Starting);

    const auto args = buildArgs();
    const std::string binary = m_config.qemuBinary.string();

    qDebug() << "QemuBackend: launching" << QString::fromStdString(binary);
    for (const auto &a : args) {
        qDebug() << "  " << QString::fromStdString(a);
    }

    m_processHandle = ProcessLauncher::runAsync(
        binary, args,
        [](const std::string &line) { qDebug() << "[qemu stdout]" << QString::fromStdString(line); },
        [](const std::string &line) { qWarning() << "[qemu stderr]" << QString::fromStdString(line); },
        false
    );

    if (m_processHandle == INVALID_HANDLE_VALUE || m_processHandle == nullptr) {
        m_errorMessage = QStringLiteral("Failed to launch QEMU process");
        setState(State::Error);
        return false;
    }

    // QEMU typically writes to QMP within a few seconds; we poll for process health.
    // Callers connect QmpInput separately once stateChanged(Running) fires.
    setState(State::Running);
    m_healthCheckTimer->start();
    m_startupTimeoutTimer->start();
    return true;
}

void QemuBackend::stop() {
    m_healthCheckTimer->stop();
    m_startupTimeoutTimer->stop();

    if (m_processHandle != INVALID_HANDLE_VALUE && m_processHandle != nullptr) {
        if (ProcessLauncher::isRunning(m_processHandle)) {
            ProcessLauncher::terminate(m_processHandle);
            ProcessLauncher::waitForExit(m_processHandle, 5000);
        }
        CloseHandle(m_processHandle);
        m_processHandle = INVALID_HANDLE_VALUE;
    }

    if (m_state != State::Stopped) {
        setState(State::Stopped);
    }
}

std::vector<std::string> QemuBackend::buildArgs() const {
    std::vector<std::string> args;

    // Acceleration and machine
    args.push_back("-accel");
    args.push_back(m_config.accel);
    args.push_back("-machine");
    args.push_back(m_config.machineType);

    // CPU and RAM
    args.push_back("-smp");
    args.push_back(std::to_string(m_config.cpus));
    args.push_back("-m");
    args.push_back(std::to_string(m_config.ramMB));

    if (m_config.mode == "cuttlefish") {
        // Remove legacy VGA so virtio-gpu-pci is head 0 and VNC captures it
        args.push_back("-vga");
        args.push_back("none");

        // virtio-gpu: DRM KMS device for composer3-service.ranchu
        args.push_back("-device");
        args.push_back("virtio-gpu-pci,xres=" + std::to_string(m_config.displayWidth) +
                       ",yres=" + std::to_string(m_config.displayHeight));

        // Kernel direct-boot
        args.push_back("-kernel");
        args.push_back(m_config.kernelPath.string());
        if (!m_config.initrdPath.empty()) {
            args.push_back("-initrd");
            args.push_back(m_config.initrdPath.string());
        }
        if (!m_config.kernelCmdline.empty()) {
            args.push_back("-append");
            args.push_back(m_config.kernelCmdline);
        }

        // virtio-scsi controller + per-disk scsi-hd devices
        if (!m_config.scsiDisks.empty()) {
            args.push_back("-device");
            args.push_back("virtio-scsi-pci,id=scsi0");
            for (size_t i = 0; i < m_config.scsiDisks.size(); ++i) {
                const std::string drvId = "drv-sd" + std::string(1, char('a' + i));
                const bool ro = i < m_config.scsiDiskReadOnly.size() && m_config.scsiDiskReadOnly[i];
                // Detect format from extension: .qcow2 → qcow2, .img/.raw → raw, else vhdx
                const auto &diskPath = m_config.scsiDisks[i];
                std::string fmt = "vhdx";
                const auto ext = diskPath.extension().string();
                if (ext == ".qcow2") fmt = "qcow2";
                else if (ext == ".img" || ext == ".raw") fmt = "raw";
                args.push_back("-drive");
                args.push_back("file=" + diskPath.string() +
                               ",if=none,id=" + drvId + ",format=" + fmt +
                               (ro ? ",readonly=on" : ",cache=writethrough"));
                args.push_back("-device");
                args.push_back("scsi-hd,bus=scsi0.0,drive=" + drvId);
            }
        }
    } else {
        // android_x86: XHCI USB controller + keyboard + tablet
        args.push_back("-device");
        args.push_back("qemu-xhci,id=xhci");
        args.push_back("-device");
        args.push_back("usb-kbd,bus=xhci.0");
        args.push_back("-device");
        args.push_back("usb-tablet,bus=xhci.0");

        // VGA display device
        if (!m_config.vgaDevice.empty()) {
            args.push_back("-vga");
            args.push_back(m_config.vgaDevice);
        }

        // Disk (optional — may boot from CDROM only)
        if (!m_config.diskImage.empty()) {
            args.push_back("-drive");
            args.push_back("file=" + m_config.diskImage.string() + ",if=virtio,format=qcow2");
        }

        // Optional CDROM for Live CD / installation
        if (!m_config.cdromImage.empty()) {
            args.push_back("-cdrom");
            args.push_back(m_config.cdromImage.string());
        }

        args.push_back("-boot");
        args.push_back(m_config.bootDevice);
    }

    // Network with optional ADB port forwarding
    if (m_config.enableAdb) {
        args.push_back("-netdev");
        args.push_back("user,id=net0,hostfwd=tcp:127.0.0.1:" +
                       std::to_string(m_config.adbPort) + "-:5555");
    } else {
        args.push_back("-netdev");
        args.push_back("user,id=net0");
    }
    args.push_back("-device");
    args.push_back("virtio-net-pci,netdev=net0");

    // VNC display (localhost only; display number → TCP 5900 + N)
    args.push_back("-vnc");
    args.push_back("127.0.0.1:" + std::to_string(m_config.vncDisplay));

    // Serial console → file for boot diagnostics
    if (!m_config.serialLog.empty()) {
        args.push_back("-serial");
        args.push_back("file:" + m_config.serialLog.string());
    }

    // Real QMP socket
    args.push_back("-qmp");
    args.push_back("tcp:127.0.0.1:" + std::to_string(m_config.qmpPort) +
                   ",server=on,wait=off");

    for (const auto &extra : m_config.extraArgs) {
        args.push_back(extra);
    }

    return args;
}

void QemuBackend::onHealthCheck() {
    if (m_processHandle == INVALID_HANDLE_VALUE) return;
    if (!ProcessLauncher::isRunning(m_processHandle)) {
        const int exitCode = ProcessLauncher::waitForExit(m_processHandle, 0);
        CloseHandle(m_processHandle);
        m_processHandle = INVALID_HANDLE_VALUE;
        m_startupTimeoutTimer->stop();

        // Code 0 = guest-initiated clean reboot/poweroff (e.g. Android first-boot setup).
        // Auto-restart to complete the boot cycle, up to maxRestarts times.
        if (exitCode == 0 && m_config.autoRestart && m_restartCount < m_config.maxRestarts) {
            ++m_restartCount;
            qDebug() << "QemuBackend: clean exit (code 0) — auto-restart"
                     << m_restartCount << "/" << m_config.maxRestarts;
            setState(State::Starting);
            const auto args = buildArgs();
            m_processHandle = ProcessLauncher::runAsync(
                m_config.qemuBinary.string(), args,
                [](const std::string &l) { qDebug()   << "[qemu stdout]" << QString::fromStdString(l); },
                [](const std::string &l) { qWarning() << "[qemu stderr]" << QString::fromStdString(l); },
                false
            );
            if (m_processHandle == INVALID_HANDLE_VALUE || m_processHandle == nullptr) {
                m_errorMessage = QStringLiteral("QEMU restart %1 failed").arg(m_restartCount);
                qWarning() << "QemuBackend:" << m_errorMessage;
                m_healthCheckTimer->stop();
                setState(State::Error);
                emit errorOccurred(m_errorMessage);
            } else {
                setState(State::Running);
                m_startupTimeoutTimer->start();
            }
            return;
        }

        m_healthCheckTimer->stop();
        if (exitCode == 0 && m_restartCount >= m_config.maxRestarts) {
            m_errorMessage = QStringLiteral("QEMU exited (code 0) after %1 auto-restarts").arg(m_restartCount);
        } else {
            m_errorMessage = QStringLiteral("QEMU exited unexpectedly (code %1)").arg(exitCode);
        }
        qWarning() << "QemuBackend:" << m_errorMessage;
        setState(State::Error);
        emit errorOccurred(m_errorMessage);
    }
}

void QemuBackend::onStartupTimeout() {
    // After kStartupTimeoutMs the process is still alive — startup phase done.
    // Callers should have connected QMP by now; stop the timer.
    m_startupTimeoutTimer->stop();
    qDebug() << "QemuBackend: startup timeout cleared, process still alive";
}

void QemuBackend::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

} // namespace chimera::instance
