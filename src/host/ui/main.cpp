#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QProcess>
#include <QDebug>
#include <QImage>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSize>
#include "ChimeraWindow.h"
#include "GuestDisplay.h"
#include "NativeEmulatorView.h"
#include "QmlAndroidControls.h"
#include "QmlInstanceManager.h"
#include "QmlMacroEngine.h"
#include "ScreenRecorder.h"
#include "InstanceManager.h"
#include "ConfigManager.h"
#include "InputBridge.h"
#include "GamepadManager.h"
#include "QmpInput.h"
#include "AudioBridge.h"
#include "DeviceSpoofer.h"
#include "AdbFramebufferCapture.h"
#include "GrpcFramebufferCapture.h"
#include "VncFramebufferCapture.h"
#include "PerformanceMonitor.h"
#include "QemuBackend.h"
#include "HyperVManager.h"
#include "HvSocketTransport.h"
#include "HvSocketFramebufferCapture.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <memory>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

using namespace chimera::instance;
using namespace chimera::config;
using namespace chimera;

static std::filesystem::path g_projectRoot;
static std::filesystem::path g_adbPath;

#ifdef _WIN32
// Read HCS VM serial console — connects as CLIENT to HCS's named pipe server.
// HCS creates \\.\pipe\chimera-serial as the SERVER; we open it as CLIENT to read kernel output.
// Must be called AFTER the VM is Running so the pipe already exists.
static void startSerialConsoleReader() {
    std::thread([](){
        HANDLE h = INVALID_HANDLE_VALUE;
        // Retry for up to 20 s (pipe appears once HCS processes the VM start)
        for (int i = 0; i < 20 && h == INVALID_HANDLE_VALUE; ++i) {
            h = CreateFileW(L"\\\\.\\pipe\\chimera-serial",
                            GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
            if (h != INVALID_HANDLE_VALUE) break;
            const DWORD err = GetLastError();
            if (err == ERROR_PIPE_BUSY) {
                WaitNamedPipeW(L"\\\\.\\pipe\\chimera-serial", 2000);
            } else {
                if (i == 0) qDebug() << "HCS serial: pipe not yet available (err" << err << "), retrying...";
                Sleep(1000);
            }
        }
        if (h == INVALID_HANDLE_VALUE) {
            qWarning() << "HCS serial: pipe not found after 20 retries — ComPorts may not be "
                          "supported in LinuxKernelDirect mode (err" << GetLastError() << ")";
            return;
        }
        qDebug() << "HCS serial: pipe opened — reading kernel output:";
        char buf[512];
        DWORD n;
        while (ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr) && n > 0) {
            buf[n] = '\0';
            qDebug() << "[VM-serial]" << QString::fromLocal8Bit(buf, static_cast<int>(n));
        }
        CloseHandle(h);
        qDebug() << "HCS serial: pipe closed";
    }).detach();
}
#endif

#ifdef _WIN32
static bool isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elev{};
    DWORD size = 0;
    const bool result = GetTokenInformation(token, TokenElevation, &elev, sizeof(elev), &size)
                        && elev.TokenIsElevated != 0;
    CloseHandle(token);
    return result;
}

// Re-launch the current process elevated via UAC (runas verb).
// Returns true if ShellExecuteEx succeeded; the caller should exit(0) in that case.
static bool selfElevate(int argc, char *argv[]) {
    wchar_t exePath[MAX_PATH];
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;

    // Rebuild command line from all args (skip argv[0])
    QString params;
    for (int i = 1; i < argc; ++i) {
        if (i > 1) params += QLatin1Char(' ');
        const QString arg = QString::fromLocal8Bit(argv[i]);
        // Quote args that contain spaces
        if (arg.contains(QLatin1Char(' ')))
            params += QLatin1Char('"') + arg + QLatin1Char('"');
        else
            params += arg;
    }

    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
    sei.lpVerb       = L"runas";
    sei.lpFile       = exePath;
    sei.lpParameters = reinterpret_cast<LPCWSTR>(params.utf16());
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}
#endif

// v2 QEMU backend globals
static QemuInstanceConfig g_qemuConfig;
static bool g_qemuConfigLoaded = false;

// Phase 5 HCS backend globals
static HyperVManager::HcsConfig g_hcsConfig;
static bool g_hcsConfigLoaded = false;

static bool loadHcsConfig() {
    const auto cfgPath = g_projectRoot / "configs" / "hcs.json";
    if (!std::filesystem::exists(cfgPath)) {
        qWarning() << "hcs.json not found at" << QString::fromStdString(cfgPath.string());
        return false;
    }
    std::ifstream f(cfgPath);
    nlohmann::json j;
    f >> j;

    const auto &hcs  = j.at("hcs");
    const auto &boot = j.at("boot");
    const auto &disk = j.at("disks");

    g_hcsConfig.name   = QString::fromStdString(hcs.value("name", "chimera_hcs"));
    g_hcsConfig.cpus   = hcs.value("cpus", 4);
    g_hcsConfig.ramMB  = hcs.value("ram_mb", 4096);
    const std::string gpuMode = hcs.value("gpu_mode", "partition");
    g_hcsConfig.gpuMode = (gpuMode == "none") ? HyperVManager::GpuNone :
                          (gpuMode == "dda")   ? HyperVManager::GpuDDA  :
                                                 HyperVManager::GpuPartition;

    auto absPath = [](const std::string &s) -> QString {
        if (s.empty()) return {};
        std::filesystem::path p(s);
        if (p.is_relative()) p = g_projectRoot / p;
        return QString::fromStdString(p.string());
    };

    g_hcsConfig.kernelPath   = absPath(boot.value("kernel", ""));
    g_hcsConfig.initrdPath   = absPath(boot.value("initrd", ""));
    g_hcsConfig.kernelCmdLine = QString::fromStdString(boot.value("cmdline", ""));

    const std::string sys  = disk.value("system",   "");
    const std::string vnd  = disk.value("vendor",   "");
    const std::string data = disk.value("userdata",  "");
    if (!sys.empty())  g_hcsConfig.readonlyDiskPaths << absPath(sys);
    if (!vnd.empty())  g_hcsConfig.readonlyDiskPaths << absPath(vnd);
    g_hcsConfig.writableDiskPath = absPath(data);

    g_hcsConfigLoaded = !g_hcsConfig.kernelPath.isEmpty();
    if (!g_hcsConfigLoaded)
        qWarning() << "hcs.json: boot.kernel is empty — HCS backend will not start";
    return g_hcsConfigLoaded;
}

static bool loadQemuConfig() {
    const auto cfgPath = g_projectRoot / "configs" / "qemu.json";
    if (!std::filesystem::exists(cfgPath)) {
        qWarning() << "qemu.json not found at" << QString::fromStdString(cfgPath.string());
        return false;
    }
    std::ifstream f(cfgPath);
    nlohmann::json j;
    f >> j;

    const auto &qemu = j.at("qemu");
    g_qemuConfig.qemuBinary = qemu.at("binary").get<std::string>();
    g_qemuConfig.machineType = qemu.value("machine_type", "q35");
    g_qemuConfig.accel       = qemu.value("accel", "whpx");
    g_qemuConfig.cpus        = qemu.value("cpus", 4);
    g_qemuConfig.ramMB       = qemu.value("ram_mb", 4096);
    g_qemuConfig.vgaDevice   = qemu.value("vga", "vmware");
    if (qemu.contains("extra_args")) {
        for (const auto &arg : qemu.at("extra_args")) {
            g_qemuConfig.extraArgs.push_back(arg.get<std::string>());
        }
    }

    const auto &disk = j.at("disk");
    {
        std::string baseImg = disk.value("base_image", "");
        if (!baseImg.empty()) {
            std::filesystem::path p = baseImg;
            if (p.is_relative()) p = g_projectRoot / p;
            g_qemuConfig.diskImage = p;
        }
    }
    {
        std::string cdrom = disk.value("cdrom", "");
        if (!cdrom.empty()) {
            std::filesystem::path p = cdrom;
            if (p.is_relative()) p = g_projectRoot / p;
            g_qemuConfig.cdromImage = p;
        }
    }
    g_qemuConfig.bootDevice = disk.value("boot", "c");

    const auto &ports = j.at("ports");
    const int vncBase = ports.value("vnc_base", 5900);
    g_qemuConfig.vncDisplay = 0;               // display :0 → TCP 5900+0
    g_qemuConfig.qmpPort    = ports.value("qmp_base", 4444);
    g_qemuConfig.adbPort    = ports.value("adb_base", 5560);
    g_qemuConfig.enableAdb  = true;
    g_qemuConfig.name       = "chimera_dev";
    (void)vncBase;

    if (j.contains("display")) {
        const auto &disp = j.at("display");
        g_qemuConfig.displayWidth  = disp.value("width",  1024);
        g_qemuConfig.displayHeight = disp.value("height", 768);
    }

    g_qemuConfigLoaded = true;
    return true;
}

static bool loadCuttlefishConfig() {
    const auto cfgPath = g_projectRoot / "configs" / "cuttlefish.json";
    if (!std::filesystem::exists(cfgPath)) {
        qWarning() << "cuttlefish.json not found at" << QString::fromStdString(cfgPath.string());
        return false;
    }
    std::ifstream f(cfgPath);
    nlohmann::json j;
    f >> j;

    auto absPath = [](const std::string &s) -> std::filesystem::path {
        if (s.empty()) return {};
        std::filesystem::path p(s);
        if (p.is_relative()) p = g_projectRoot / p;
        return p;
    };

    const auto &qemu = j.at("qemu");
    g_qemuConfig.qemuBinary  = absPath(qemu.at("binary").get<std::string>());
    g_qemuConfig.machineType = qemu.value("machine_type", "q35");
    g_qemuConfig.accel       = qemu.value("accel", "whpx,kernel-irqchip=off");
    g_qemuConfig.cpus        = qemu.value("cpus", 4);
    g_qemuConfig.ramMB       = qemu.value("ram_mb", 4096);
    g_qemuConfig.mode        = qemu.value("mode", "cuttlefish");
    if (qemu.contains("extra_args")) {
        for (const auto &a : qemu.at("extra_args"))
            g_qemuConfig.extraArgs.push_back(a.get<std::string>());
    }

    const auto &kernel = j.at("kernel");
    g_qemuConfig.kernelPath   = absPath(kernel.at("image").get<std::string>());
    if (kernel.contains("initrd"))
        g_qemuConfig.initrdPath = absPath(kernel.at("initrd").get<std::string>());
    g_qemuConfig.kernelCmdline = kernel.value("cmdline", "");

    if (j.contains("disks")) {
        for (const auto &d : j.at("disks")) {
            g_qemuConfig.scsiDisks.push_back(absPath(d.at("path").get<std::string>()));
            g_qemuConfig.scsiDiskReadOnly.push_back(d.value("readonly", false));
        }
    }

    const auto &ports = j.at("ports");
    const int vncBase = ports.value("vnc_base", 5901);
    g_qemuConfig.vncDisplay = vncBase - 5900;   // :N → TCP 5900+N
    g_qemuConfig.qmpPort    = ports.value("qmp_base", 4445);
    g_qemuConfig.adbPort    = ports.value("adb_base", 5558);
    g_qemuConfig.enableAdb  = true;
    g_qemuConfig.name       = "cuttlefish_dev";
    g_qemuConfig.serialLog  = g_projectRoot / "qemu-cuttlefish-serial.log";

    if (j.contains("display")) {
        const auto &disp = j.at("display");
        g_qemuConfig.displayWidth  = disp.value("width",  1280);
        g_qemuConfig.displayHeight = disp.value("height", 720);
    }

    g_qemuConfigLoaded = true;
    return true;
}

static std::filesystem::path findProjectRoot() {
    auto path = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(path / "configs" / "android_sdk.json")) {
            return path;
        }
        path = path.parent_path();
    }
    return std::filesystem::current_path();
}

static bool loadSdkConfig() {
    g_projectRoot = findProjectRoot();
    auto cfgPath = g_projectRoot / "configs" / "android_sdk.json";
    if (!std::filesystem::exists(cfgPath)) {
        qWarning() << "android_sdk.json not found at" << QString::fromStdString(cfgPath.string());
        return false;
    }
    std::ifstream f(cfgPath);
    nlohmann::json j;
    f >> j;
    if (j.contains("adb")) {
        g_adbPath = j["adb"].get<std::string>();
    }
    if (j.contains("sdk_root")) {
        qputenv("ANDROID_SDK_ROOT", QByteArray::fromStdString(j["sdk_root"].get<std::string>()));
    }
    if (j.contains("avd_home")) {
        qputenv("ANDROID_AVD_HOME", QByteArray::fromStdString(j["avd_home"].get<std::string>()));
    }
    return !g_adbPath.empty();
}

static QString adbPathString() {
    return QString::fromStdString(g_adbPath.string());
}

static void runAdbShell(const QStringList &shellArgs, int timeoutMs = 1500) {
    if (g_adbPath.empty()) return;

    QProcess proc;
    QStringList args;
    args << "-s" << "emulator-5554" << "shell";
    args << shellArgs;
    proc.start(adbPathString(), args);
    proc.waitForFinished(timeoutMs);
}

static void applyGuestPerformanceSettings() {
    // Apply all guest tuning in a single adb shell invocation instead of six
    // separate process spawns: one round-trip instead of six.
    runAdbShell({
        "settings", "put", "system", "peak_refresh_rate", "60.0", ";",
        "settings", "put", "system", "min_refresh_rate", "60.0", ";",
        "settings", "put", "global", "window_animation_scale", "0", ";",
        "settings", "put", "global", "transition_animation_scale", "0", ";",
        "settings", "put", "global", "animator_duration_scale", "0", ";",
        "cmd", "power", "set-fixed-performance-mode-enabled", "true", ";",
        // Hide Android navigation bar so the host right-panel controls are used instead
        "settings", "put", "global", "policy_control", "immersive.navigation=*",
    }, 5000);
    qDebug() << "Guest performance settings applied";
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    // HCS/AF_HYPERV require SeCreateGlobalPrivilege (held by administrators).
    // If --hcs-backend is requested and we are not elevated, ask UAC to re-launch elevated.
    {
        bool needsHcs = false;
        for (int i = 1; i < argc; ++i) {
            if (strcmp(argv[i], "--hcs-backend") == 0) { needsHcs = true; break; }
        }
        if (needsHcs) {
            const bool elevated = isElevated();
            fprintf(stderr, "[INFO] HCS backend: process elevated = %s\n", elevated ? "YES" : "NO");
            if (!elevated) {
                fprintf(stderr, "[INFO] HCS backend requires elevation — requesting UAC...\n");
                if (selfElevate(argc, argv)) {
                    fprintf(stderr, "[INFO] Elevated process launched — exiting this instance.\n");
                    return 0;
                }
                fprintf(stderr, "[WARN] UAC elevation denied or failed — continuing without elevation.\n");
            }
        }
    }
#endif

    // Write debug log to file so we can inspect output without a debug viewer.
    // Only active when --hcs-backend flag is present.
    static FILE *g_logFile = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--hcs-backend") == 0 || strcmp(argv[i], "--qemu-backend") == 0 ||
            strcmp(argv[i], "--cuttlefish") == 0) {
            g_logFile = fopen("chimera-debug.log", "w");
            break;
        }
    }
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        const char *prefix = (type == QtWarningMsg) ? "WARN" :
                             (type == QtCriticalMsg || type == QtFatalMsg) ? "ERR " : "DBG ";
        QByteArray b = msg.toLocal8Bit();
        if (g_logFile) { fprintf(g_logFile, "[%s] %s\n", prefix, b.constData()); fflush(g_logFile); }
        fprintf(stderr, "[%s] %s\n", prefix, b.constData());
    });

    // BlueStacks uses D3D11 for its Qt shell. Force the same Qt Quick RHI path
    // before QGuiApplication is created so the UI does not fall back to OpenGL.
    qputenv("QSG_RHI_BACKEND", "d3d11");
    qputenv("QT_QUICK_BACKEND", "rhi");

    QGuiApplication app(argc, argv);
    app.setApplicationName("Chimera");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("chimera-emulator");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const bool noEmulator = app.arguments().contains(QStringLiteral("--no-emulator"));
    const bool streamCapture = app.arguments().contains(QStringLiteral("--stream-capture"));
    const bool nativeDisplayEnabled = !streamCapture;
    // v2: stock QEMU + VNC display + real QMP input (Phase 1-3)
    const bool cuttlefishMode  = app.arguments().contains(QStringLiteral("--cuttlefish"));
    const bool qemuBackendMode = cuttlefishMode ||
                                 app.arguments().contains(QStringLiteral("--qemu-backend"));
    // Phase 5: Hyper-V HCS + GPU-PV backend
    const bool hcsBackendMode = app.arguments().contains(QStringLiteral("--hcs-backend"));

    g_projectRoot = findProjectRoot();
    std::error_code cwdError;
    std::filesystem::current_path(g_projectRoot, cwdError);
    if (cwdError) {
        qWarning() << "Failed to set working directory:"
                   << QString::fromStdString(cwdError.message());
    }

    if (hcsBackendMode) {
        if (!loadHcsConfig()) {
            qWarning() << "Failed to load hcs.json. HCS backend will not start.";
        } else {
            qDebug() << "HCS backend: kernel=" << g_hcsConfig.kernelPath
                     << "gpu=" << static_cast<int>(g_hcsConfig.gpuMode);
        }
    } else if (cuttlefishMode) {
        if (!loadCuttlefishConfig()) {
            qWarning() << "Failed to load cuttlefish.json. Cuttlefish backend will not start.";
        } else {
            qDebug() << "Cuttlefish backend: kernel="
                     << QString::fromStdString(g_qemuConfig.kernelPath.string());
        }
    } else if (qemuBackendMode) {
        if (!loadQemuConfig()) {
            qWarning() << "Failed to load qemu.json. QEMU backend will not start.";
        }
    } else if (!noEmulator && !loadSdkConfig()) {
        qWarning() << "Failed to load Android SDK config. Emulator will not start.";
    }

    // Ensure screenshots and recordings directories exist
    std::filesystem::create_directories(g_projectRoot / "screenshots");
    std::filesystem::create_directories(g_projectRoot / "recordings");

    QQmlApplicationEngine engine;
    qmlRegisterType<chimera::ChimeraWindow>("Chimera.UI", 1, 0, "ChimeraWindow");
    qmlRegisterType<chimera::GuestDisplay>("Chimera.UI", 1, 0, "GuestDisplay");
    qmlRegisterType<chimera::NativeEmulatorView>("Chimera.UI", 1, 0, "NativeEmulatorView");

    // Expose instance manager to QML
    chimera::QmlAndroidControls qmlAndroidControls;
    engine.rootContext()->setContextProperty("AndroidControls", &qmlAndroidControls);

    // Expose instance manager to QML
    chimera::QmlInstanceManager qmlInstanceMgr;
    engine.rootContext()->setContextProperty("InstanceManager", &qmlInstanceMgr);

    // Expose macro engine to QML
    chimera::QmlMacroEngine qmlMacroEngine;
    engine.rootContext()->setContextProperty("MacroEngine", &qmlMacroEngine);

    // Screen recorder
    chimera::ScreenRecorder screenRecorder;
    engine.rootContext()->setContextProperty("ScreenRecorder", &screenRecorder);

    // Performance monitoring
    auto *perfMonitor = new chimera::graphics::PerformanceMonitor(&app);
    engine.rootContext()->setContextProperty("PerfMonitor", perfMonitor);

    bool emulatorStarted = false;
    int grpcCaptureWidth = 960;
    int grpcCaptureHeight = 0;
    QSize guestInputSize;
    chimera::input::QmpInput *qmpInput = nullptr;

    // Phase 5: Hyper-V HCS + GPU-PV backend
    HyperVManager *hcsManager = nullptr;
    chimera::input::HvSocketTransport *hvInput = nullptr;
    chimera::graphics::HvSocketFramebufferCapture *hvDisplay = nullptr;
    if (hcsBackendMode && g_hcsConfigLoaded) {
        if (!HyperVManager::isAvailable()) {
            qWarning() << "HCS unavailable: Hyper-V not enabled or computecore.dll missing";
        } else {
            qDebug() << "HCS: GPU-PV supported:" << HyperVManager::isGpuPartitionSupported()
                     << "| Partitions:" << HyperVManager::availableGpuPartitions();

            hcsManager = new HyperVManager(&app);

            // HvSocket input transport — connects once VM is Running
            hvInput = new chimera::input::HvSocketTransport(
                QString(), &app); // VM GUID resolved after createVm
            hvInput->setAutoReconnect(true, 3000);
            chimera::input::InputBridge::instance().setHvSocketTransport(hvInput);

            // HvSocket framebuffer capture — same GUID, different service port
            hvDisplay = new chimera::graphics::HvSocketFramebufferCapture(
                QString(), &app);
            hvDisplay->setAutoReconnect(true, 3000);

            QObject::connect(hcsManager, &HyperVManager::stateChanged, &app,
                             [hcsManager, hvInput, hvDisplay](HyperVManager::State s) {
                qDebug() << "HCS state ->" << static_cast<int>(s);
                if (s == HyperVManager::State::Stopped) {
                    qDebug() << "HCS: VM created, starting...";
                    hcsManager->startVm();
                } else if (s == HyperVManager::State::Running) {
                    // partitionId() is the actual HV partition GUID for AF_HYPERV SOCKADDR_HV
                    const QString partGuid = hcsManager->partitionId();
                    qDebug() << "HCS: VM running, partitionId=" << partGuid
                             << "— reading serial, connecting HvSocket in 30s";
                    hvInput->setVmId(partGuid);
                    hvDisplay->setVmId(partGuid);
#ifdef _WIN32
                    // Connect to HCS serial pipe now that the VM is Running
                    startSerialConsoleReader();
#endif
                    // 30s delay: kernel boot + module load + vsock relay start
                    QTimer::singleShot(30000, hvInput,  [hvInput]()  {
                        qDebug() << "HvSocket: attempting input connect...";
                        hvInput->connectToVm();
                    });
                    QTimer::singleShot(30000, hvDisplay, [hvDisplay](){
                        qDebug() << "HvSocket: attempting display connect...";
                        hvDisplay->start();
                    });
                }
            });
            QObject::connect(hvInput, &chimera::input::HvSocketTransport::connected,
                             &app, []() { qDebug() << "HvSocket input: connected"; });
            QObject::connect(hvInput, &chimera::input::HvSocketTransport::error,
                             &app, [](const QString &m) { qWarning() << "HvSocket input error:" << m; });
            QObject::connect(hcsManager, &HyperVManager::error, &app,
                             [](const QString &msg) { qWarning() << "HCS error:" << msg; });

            qDebug() << "HCS: creating VM" << g_hcsConfig.name
                     << QString("%1 CPU / %2 MB RAM").arg(g_hcsConfig.cpus).arg(g_hcsConfig.ramMB);
            if (!hcsManager->createVm(g_hcsConfig)) {
                qWarning() << "HCS createVm failed:" << hcsManager->lastError();
            } else {
                emulatorStarted = true;
                guestInputSize  = QSize(1920, 1080);
            }
        }
    }

    // v2: Start stock QEMU instance
    QemuBackend *qemuBackend = nullptr;
    if (qemuBackendMode && g_qemuConfigLoaded) {
        qemuBackend = new QemuBackend(g_qemuConfig, &app);

        QObject::connect(qemuBackend, &QemuBackend::stateChanged, &app,
                         [](QemuBackend::State s) {
            qDebug() << "QemuBackend state:" << static_cast<int>(s);
        });
        QObject::connect(qemuBackend, &QemuBackend::errorOccurred, &app,
                         [](const QString &msg) { qWarning() << "QemuBackend error:" << msg; });

        if (!qemuBackend->start()) {
            qWarning() << "Failed to start QEMU:" << qemuBackend->errorMessage();
        } else {
            qDebug() << "QEMU started. VNC on port" << qemuBackend->vncPort()
                     << "| QMP on port" << qemuBackend->qmpPort()
                     << "| ADB on port" << qemuBackend->adbPort();

            // QMP input on the real QMP socket (not the SDK telnet console)
            qmpInput = new chimera::input::QmpInput(&app);
            qmpInput->setAutoReconnect(true, 3000);
            qmpInput->setDisplaySize(g_qemuConfig.displayWidth, g_qemuConfig.displayHeight);
            chimera::input::InputBridge::instance().setQmpInput(qmpInput);
            // Will connect once QEMU is ready; auto-reconnect handles the retry
            QTimer::singleShot(3000, &app, [qmpInput, qemuBackend]() {
                const bool ok = qmpInput->connectToHost(
                    QStringLiteral("127.0.0.1"), qemuBackend->qmpPort());
                if (ok) qDebug() << "QMP input connected to port" << qemuBackend->qmpPort();
                else    qDebug() << "QMP not ready yet; auto-reconnect will retry";
            });

            guestInputSize = QSize(g_qemuConfig.displayWidth, g_qemuConfig.displayHeight);
            emulatorStarted = true;

            // Wake Android display: send a QMP mouse click 60s after QEMU starts.
            // virtio-gpu 2D only flushes VNC when the guest calls RESOURCE_FLUSH;
            // a mouse click wakes the display power manager so SurfaceFlinger resumes rendering.
            if (cuttlefishMode) {
                QTimer::singleShot(60000, &app, [qmpInput]() {
                    if (qmpInput && qmpInput->isConnected()) {
                        qDebug() << "VNC wakeup: sending QMP mouse click to wake Android display";
                        qmpInput->sendMouseButton(0, true, 640, 360);
                        QTimer::singleShot(100, qmpInput, [qmpInput]() {
                            qmpInput->sendMouseButton(0, false, 640, 360);
                        });
                    }
                });
            }
        }
    }

    // v1: Start emulator instance
    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && !g_adbPath.empty()) {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "chimera_dev";
        cfg.cpus = 4;
        cfg.ramMB = 2048;
        cfg.width = 1280;
        cfg.height = 720;
        cfg.deviceProfile = "Samsung Galaxy S24 Ultra"; // Unlock high FPS/quality in games
        cfg.graphicsRenderer = "host";
        cfg.headless = !nativeDisplayEnabled;
        cfg.processPriority = "high";
        cfg.dataDir = (g_projectRoot / "instances" / cfg.name).make_preferred();
        guestInputSize = QSize(cfg.width, cfg.height);

        // Remove existing instance with same name to avoid duplicates in memory
        mgr.deleteInstance(cfg.name);
        if (mgr.createInstance(cfg)) {
            qDebug() << "Instance created:" << QString::fromStdString(cfg.name);
            mgr.setStateCallback([](const std::string &name, VMState s) {
                qDebug() << "Instance" << QString::fromStdString(name)
                         << "state:" << static_cast<int>(s);
            });
            if (mgr.startInstance(cfg.name)) {
                qDebug() << "Instance started:" << QString::fromStdString(cfg.name);
                emulatorStarted = true;
            } else {
                qWarning() << "Failed to start instance" << QString::fromStdString(cfg.name);
            }
        } else {
            qWarning() << "Failed to create instance" << QString::fromStdString(cfg.name);
        }

        // Configure input bridge for ADB forwarding (fallback)
        chimera::input::InputBridge::instance().setAdbConfig(g_adbPath, 5555);
        chimera::input::InputBridge::instance().setDisplaySize(cfg.width, cfg.height);

        // Try QMP low-latency input with auto-reconnect
        qmpInput = new chimera::input::QmpInput(&app);
        qmpInput->setAutoReconnect(true, 5000);
        qmpInput->setDisplaySize(cfg.width, cfg.height);
        chimera::input::InputBridge::instance().setQmpInput(qmpInput);
        bool qmpConnected = qmpInput->connectToHost("localhost", 5554);
        if (qmpConnected) {
            qDebug() << "QMP input connected (low-latency mode)";
        } else {
            qWarning() << "QMP input not available, falling back to ADB. Will retry every 5s.";
        }

        qDebug() << "Guest audio disabled (-no-audio)";

        // Wire gamepad to input bridge
        auto &gp = chimera::input::GamepadManager::instance();
        gp.initialize();
        gp.setButtonCallback([](int deviceId, int button, bool pressed) {
            chimera::input::InputBridge::instance().onGamepadButton(deviceId, button, pressed);
        });
        gp.setAxisCallback([](int deviceId, int axis, float value) {
            chimera::input::InputBridge::instance().onGamepadAxis(deviceId, axis, value);
        });
        auto *gpTimer = new QTimer(&app);
        QObject::connect(gpTimer, &QTimer::timeout, &app, [&gp]() {
            gp.poll();
        });
        gpTimer->start(16); // ~60 Hz polling

        grpcCaptureWidth = cfg.width > cfg.height ? 960 : 540;
        grpcCaptureHeight = 0; // Preserve guest aspect ratio.
    }

    const QUrl url(QStringLiteral("qrc:/ChimeraWindow.qml"));
    QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
                     &app, [url](QObject *obj, const QUrl &objUrl) {
        if (!obj && url == objUrl) {
            QCoreApplication::exit(-1);
        }
    }, Qt::QueuedConnection);

    engine.load(url);

    GuestDisplay *guestDisplay = nullptr;
    const auto roots = engine.rootObjects();
    for (auto *obj : roots) {
        guestDisplay = obj->findChild<GuestDisplay*>("guestDisplay");
        if (guestDisplay) break;
    }
    if (!guestDisplay) {
        qWarning() << "GuestDisplay not found; frame capture will not be visible";
    } else if (guestInputSize.isValid()) {
        guestDisplay->setGuestSize(guestInputSize);
    }

    auto currentDisplaySize = std::make_shared<QSize>();
    auto logicalGuestSize = std::make_shared<QSize>(guestInputSize);
    auto wireCapture = [&](chimera::graphics::FramebufferCapture *cap) {
        QObject::connect(cap, &chimera::graphics::FramebufferCapture::frameReady,
                         &engine, [guestDisplay, &screenRecorder, perfMonitor, qmpInput, currentDisplaySize, logicalGuestSize](const QImage &img) {
            perfMonitor->onFrameReceived();
            const QSize inputSize = logicalGuestSize->isValid() ? *logicalGuestSize : img.size();
            if (*currentDisplaySize != inputSize) {
                *currentDisplaySize = inputSize;
                chimera::input::InputBridge::instance().setDisplaySize(inputSize.width(), inputSize.height());
                if (qmpInput) qmpInput->setDisplaySize(inputSize.width(), inputSize.height());
            }
            if (guestDisplay) {
                guestDisplay->setFrame(img);
                screenRecorder.feedFrame(img);
            }
        });
        QObject::connect(cap, &chimera::graphics::FramebufferCapture::captureError,
                         &app, [perfMonitor](const QString &msg) {
            qWarning() << "Frame capture error:" << msg;
            perfMonitor->onFrameDropped();
        });
    };

    // Phase 5: HvSocket display capture (wired but auto-starts when HCS VM is Running)
    if (hcsBackendMode && hvDisplay) {
        wireCapture(hvDisplay);
    }

    // v2: VNC capture from stock QEMU
    chimera::graphics::VncFramebufferCapture *vncCapture = nullptr;
    if (qemuBackendMode && emulatorStarted && qemuBackend) {
        vncCapture = new chimera::graphics::VncFramebufferCapture(
            QStringLiteral("127.0.0.1"), qemuBackend->vncPort(), &app);
        vncCapture->setAutoReconnect(true, 2000);
        vncCapture->setDesiredResolution(g_qemuConfig.displayWidth, g_qemuConfig.displayHeight);
        wireCapture(vncCapture);

        // Initial retry timer: start VNC connection attempts until QEMU's VNC server is ready
        auto *vncRetryTimer = new QTimer(&app);
        vncRetryTimer->setInterval(1500);
        vncRetryTimer->setProperty("attempts", 0);
        QObject::connect(vncRetryTimer, &QTimer::timeout, &app,
                         [vncCapture, vncRetryTimer]() {
            int attempts = vncRetryTimer->property("attempts").toInt() + 1;
            vncRetryTimer->setProperty("attempts", attempts);
            if (vncCapture->isConnected()) {
                // Fully in Ready/FramebufferUpdate state — frames are flowing
                vncRetryTimer->stop();
                qDebug() << "VNC: connected and ready after" << attempts << "attempts";
                return;
            }
            if (!vncCapture->isRunning()) {
                // Socket disconnected — try again
                qDebug() << "VNC: connecting attempt" << attempts;
                vncCapture->start();
                if (attempts >= 40) {
                    qWarning() << "VNC: giving up after 60s";
                    vncRetryTimer->stop();
                }
            }
            // else: connection in progress, wait for next tick
        });
        vncRetryTimer->start();
    }

    // v1: gRPC / ADB capture
    chimera::graphics::AdbFramebufferCapture *adbCapture = nullptr;
    chimera::graphics::GrpcFramebufferCapture *grpcCapture = nullptr;
    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && emulatorStarted && streamCapture) {
        grpcCapture = new chimera::graphics::GrpcFramebufferCapture(
            QStringLiteral("127.0.0.1"), 8554, grpcCaptureWidth, grpcCaptureHeight, &app);
        wireCapture(grpcCapture);

        if (!g_adbPath.empty()) {
            adbCapture = new chimera::graphics::AdbFramebufferCapture(
                QString::fromStdString(g_adbPath.string()), 5555, false, &app);
            adbCapture->setIntervalMs(1000);
            wireCapture(adbCapture);
        }
    }

    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && emulatorStarted && !g_adbPath.empty()) {
        auto *guestPerfTimer = new QTimer(&app);
        guestPerfTimer->setInterval(2000);
        guestPerfTimer->setProperty("attempts", 0);
        QObject::connect(guestPerfTimer, &QTimer::timeout, [guestPerfTimer]() {
            int attempts = guestPerfTimer->property("attempts").toInt() + 1;
            guestPerfTimer->setProperty("attempts", attempts);

            QProcess proc;
            proc.start(adbPathString(), QStringList() << "-s" << "emulator-5554"
                                                     << "shell" << "getprop" << "sys.boot_completed");
            proc.waitForFinished(1000);
            const QString booted = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
            if (booted == QStringLiteral("1")) {
                guestPerfTimer->stop();
                applyGuestPerformanceSettings();
                return;
            }
            if (attempts >= 60) {
                guestPerfTimer->stop();
            }
        });
        guestPerfTimer->start();
    }

    // Log FPS every 5 seconds
    auto *perfTimer = new QTimer(&app);
    QObject::connect(perfTimer, &QTimer::timeout, [perfMonitor]() {
        qDebug() << QStringLiteral("[Perf] FPS: %1 | Avg: %2ms | Max: %3ms | Dropped: %4 / %5")
                    .arg(perfMonitor->fps(), 0, 'f', 1)
                    .arg(perfMonitor->averageFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->maxFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->droppedFrames())
                    .arg(perfMonitor->totalFrames());
    });
    perfTimer->start(5000);

    // ADB is a compatibility fallback. gRPC should be used for normal display streaming.
    auto adbStartRequested = std::make_shared<bool>(false);
    auto startAdbFallback = [adbCapture, adbStartRequested, &app]() {
        if (!adbCapture || *adbStartRequested) return;
        *adbStartRequested = true;

        auto *bootTimer = new QTimer(&app);
        bootTimer->setInterval(2000);
        bootTimer->setProperty("attempts", 0);
        QObject::connect(bootTimer, &QTimer::timeout, [adbCapture, bootTimer]() {
            int attempts = bootTimer->property("attempts").toInt() + 1;
            bootTimer->setProperty("attempts", attempts);

            QProcess proc;
            proc.start(QString::fromStdString(g_adbPath.string()),
                       QStringList() << "-s" << "emulator-5554"
                                     << "shell" << "getprop" << "sys.boot_completed");
            proc.waitForFinished(1000);
            const QString booted = QString::fromLocal8Bit(proc.readAllStandardOutput()).trimmed();
            if (booted == QStringLiteral("1") || attempts >= 45) {
                bootTimer->stop();
                if (adbCapture->start()) {
                    qDebug() << "ADB raw screen capture fallback started (" << adbCapture->intervalMs()
                             << "ms target interval)";
                }
            }
        });
        bootTimer->start();
    };

    if (!hcsBackendMode && !qemuBackendMode && grpcCapture) {
        auto grpcReceivedFrame = std::make_shared<bool>(false);
        QObject::connect(grpcCapture, &chimera::graphics::FramebufferCapture::frameReady,
                         &app, [grpcReceivedFrame, adbCapture]() {
            *grpcReceivedFrame = true;
            if (adbCapture && adbCapture->isRunning()) {
                adbCapture->stop();
            }
        });

        auto *grpcRetryTimer = new QTimer(&app);
        grpcRetryTimer->setInterval(1000);
        grpcRetryTimer->setProperty("attempts", 0);
        QObject::connect(grpcRetryTimer, &QTimer::timeout,
                         [grpcCapture, grpcRetryTimer, grpcReceivedFrame, startAdbFallback]() {
            if (*grpcReceivedFrame) {
                grpcRetryTimer->stop();
                return;
            }

            int attempts = grpcRetryTimer->property("attempts").toInt() + 1;
            grpcRetryTimer->setProperty("attempts", attempts);
            if (!grpcCapture->isRunning()) {
                qDebug() << "Starting gRPC screen capture stream";
                grpcCapture->start();
            } else if (attempts > 1 && attempts % 5 == 0) {
                qWarning() << "gRPC capture has not produced a frame; restarting stream";
                grpcCapture->stop();
                grpcCapture->start();
            }
            if (attempts == 45 && !*grpcReceivedFrame) {
                qWarning() << "gRPC capture has not produced a frame yet; starting ADB fallback in parallel";
                startAdbFallback();
            }
            if (attempts >= 180 && !*grpcReceivedFrame) {
                grpcRetryTimer->stop();
                grpcCapture->stop();
            }
        });
        grpcRetryTimer->start();
    } else if (!hcsBackendMode && !qemuBackendMode && streamCapture) {
        startAdbFallback();
    }

    return app.exec();
}
