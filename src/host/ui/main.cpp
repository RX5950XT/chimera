#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QDateTime>
#include <QTimer>
#include <QProcess>
#include <QDebug>
#include <QImage>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSize>
#include <QPointer>
#include <QFileInfo>
#include <QFile>
#include <QCryptographicHash>
#include <QWindow>
#include "ChimeraWindow.h"
#include "GuestDisplay.h"
#include "NativeEmulatorView.h"
#include "QmlAndroidControls.h"
#include "QmlInstanceManager.h"
#include "QmlMacroEngine.h"
#include "MacroEngine.h"
#include "QmlInputMapper.h"
#include "ScreenRecorder.h"
#include "InstanceManager.h"
#include "ConfigManager.h"
#include "InputBridge.h"
#include "AndroidConsoleInput.h"
#include "EmulatorGrpcInput.h"
#include "GamepadManager.h"
#include "QmpInput.h"
#include "AudioBridge.h"
#include "DeviceSpoofer.h"
#include "AdbFramebufferCapture.h"
#include "AdbH264FramebufferCapture.h"
#include "GrpcFramebufferCapture.h"
#include "GrpcMmapFramebufferCapture.h"
#include "SharedD3D11TextureCapture.h"
#include "WindowD3D11Capture.h"
#include "SharedMemoryFramebufferCapture.h"
#include "VncFramebufferCapture.h"
#include "PerformanceMonitor.h"
#include "QemuBackend.h"
#include "InstanceRuntimeConfig.h"
#include "HyperVManager.h"
#include "HvSocketTransport.h"
#include "HvSocketFramebufferCapture.h"
#include "LocationSimulator.h"
#include "ClipboardBridge.h"
#include "SharedFolder.h"
#include "LowInterferenceProcess.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdlib>
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
static InstanceRuntimeConfig  g_runtimeCfg; // set when the v1 emulator instance starts

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

static constexpr int kMinimumGuestWidth = 1920;
static constexpr int kMinimumGuestHeight = 1080;

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
        g_qemuConfig.displayWidth =
            (std::max)(disp.value("width", kMinimumGuestWidth), kMinimumGuestWidth);
        g_qemuConfig.displayHeight =
            (std::max)(disp.value("height", kMinimumGuestHeight), kMinimumGuestHeight);
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
        g_qemuConfig.displayWidth =
            (std::max)(disp.value("width", kMinimumGuestWidth), kMinimumGuestWidth);
        g_qemuConfig.displayHeight =
            (std::max)(disp.value("height", kMinimumGuestHeight), kMinimumGuestHeight);
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
        const auto sdkRoot = QByteArray::fromStdString(j["sdk_root"].get<std::string>());
        qputenv("ANDROID_SDK_ROOT", sdkRoot);
        // Emulator 31+ prefers ANDROID_HOME over ANDROID_SDK_ROOT; an inherited
        // user-level ANDROID_HOME (e.g. Android Studio's AppData SDK) would win
        // and break AVD system-image resolution.
        qputenv("ANDROID_HOME", sdkRoot);
    }
    if (j.contains("avd_home")) {
        qputenv("ANDROID_AVD_HOME", QByteArray::fromStdString(j["avd_home"].get<std::string>()));
    }
    return !g_adbPath.empty();
}

static QString adbPathString() {
    return QString::fromStdString(g_adbPath.string());
}

static void startLowInterferenceProcess(QProcess *process,
                                        const QString &program,
                                        const QStringList &args) {
    if (!process) return;
    process->start(program, args);
    chimera::utils::applyLowInterferencePriority(process);
}

static bool runAdbCommand(const QStringList &args, int timeoutMs = 5000) {
    if (g_adbPath.empty()) return false;

    QProcess proc;
    startLowInterferenceProcess(&proc, adbPathString(), args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        qWarning() << "ADB command timed out:" << args;
        return false;
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        const QString err = QString::fromUtf8(proc.readAllStandardError()).trimmed();
        const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        qWarning() << "ADB command failed:" << args << (err.isEmpty() ? out : err);
        return false;
    }
    return true;
}

static void runAdbShell(const QStringList &shellArgs, int timeoutMs = 1500) {
    if (g_adbPath.empty()) return;

    QProcess proc;
    QStringList args;
    args << "-s" << QString::fromStdString(g_runtimeCfg.adbSerial) << "shell";
    args << shellArgs;
    startLowInterferenceProcess(&proc, adbPathString(), args);
    proc.waitForFinished(timeoutMs);
}

static QByteArray runAdbOutput(const QStringList &args, int timeoutMs = 5000) {
    if (g_adbPath.empty()) return {};

    QProcess proc;
    startLowInterferenceProcess(&proc, adbPathString(), args);
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        proc.waitForFinished(1000);
        qWarning() << "ADB output command timed out:" << args;
        return {};
    }
    if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0) {
        return {};
    }
    return proc.readAllStandardOutput();
}

static void saveQuickBootSnapshotAsync(QObject *parent) {
    if (g_adbPath.empty()) return;
    auto *proc = new QProcess(parent);
    const QStringList args = {"-s", QString::fromStdString(g_runtimeCfg.adbSerial),
                              "emu", "avd", "snapshot", "save",
                              QStringLiteral("chimera_quickboot")};
    QObject::connect(proc, &QProcess::finished, proc,
                     [proc](int exitCode, QProcess::ExitStatus status) {
        if (status == QProcess::NormalExit && exitCode == 0) {
            qDebug() << "Quick Boot snapshot saved";
        } else {
            const QString err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
            qWarning() << "Quick Boot snapshot save failed:" << err;
        }
        proc->deleteLater();
    });
    startLowInterferenceProcess(proc, adbPathString(), args);
    QTimer::singleShot(45000, proc, [proc]() {
        if (proc->state() != QProcess::NotRunning) {
            qWarning() << "Quick Boot snapshot save timed out";
            proc->kill();
        }
    });
}

static bool isTruthyEnv(const char *name) {
    const char *env = std::getenv(name);
    if (!env) return false;
    const QString value = QString::fromLocal8Bit(env).trimmed();
    return !value.isEmpty() &&
           value.compare(QStringLiteral("0"), Qt::CaseInsensitive) != 0 &&
           value.compare(QStringLiteral("false"), Qt::CaseInsensitive) != 0 &&
           value.compare(QStringLiteral("off"), Qt::CaseInsensitive) != 0;
}

static bool shouldAutoSaveQuickBootSnapshot() {
    return isTruthyEnv("CHIMERA_SAVE_QUICK_BOOT");
}

static QString findFFmpegBinary() {
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + QStringLiteral("/ffmpeg.exe"),
        QString::fromStdString((g_projectRoot / "third_party" / "ffmpeg" / "ffmpeg.exe").string()),
        QStringLiteral("ffmpeg.exe")
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo::exists(candidate))
            return candidate;
    }
    return {};
}

static QByteArray envValue(const char *name) {
    const char *env = std::getenv(name);
    return env ? QByteArray(env) : QByteArray();
}

static QByteArray firstEnvValue(const char *first, const char *second, const QByteArray &fallback) {
    const QByteArray a = envValue(first);
    if (!a.isEmpty()) return a;
    const QByteArray b = envValue(second);
    return b.isEmpty() ? fallback : b;
}

static void setEnvIfChanged(const char *name, const QByteArray &value) {
    if (value.isEmpty()) return;
    if (envValue(name) != value)
        qputenv(name, value);
}

// Interactive knob resolvers: an explicit env override wins, otherwise the
// built-in default (chosen to preserve the historical effective behavior).
static std::string envOrDefault(const char *name, const std::string &fallback) {
    const char *env = std::getenv(name);
    if (!env) return fallback;
    const QString value = QString::fromLocal8Bit(env).trimmed();
    return value.isEmpty() ? fallback : value.toStdString();
}

static int envIntOrDefault(const char *name, int fallback) {
    const char *env = std::getenv(name);
    if (!env) return fallback;
    bool ok = false;
    const int value = QString::fromLocal8Bit(env).trimmed().toInt(&ok);
    return ok ? value : fallback;
}

// The emulator priority ceiling is "normal" (enforced by InstanceManager and
// ProcessLauncher); anything else falls back to the conservative default so a
// stray value can never starve host audio with a high-priority guest tree.
static std::string sanitizeProcessPriority(const std::string &priority) {
    const QString p = QString::fromStdString(priority).trimmed().toLower();
    if (p == QStringLiteral("idle") || p == QStringLiteral("below_normal") ||
        p == QStringLiteral("normal") || p == QStringLiteral("eco")) {
        return p.toStdString();
    }
    qWarning() << "Unsupported CHIMERA_INTERACTIVE_PRIORITY value" << p
               << "- falling back to below_normal";
    return "below_normal";
}

static void configureEmuglSharedTextureEnv(const QStringList &args) {
    const bool requested = args.contains(QStringLiteral("--emugl-shared-texture")) ||
                           isTruthyEnv("CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE") ||
                           !envValue("CHIMERA_D3D11_TEXTURE_METADATA").isEmpty() ||
                           !envValue("CHIMERA_EMUGL_D3D11_TEXTURE_METADATA").isEmpty();
    if (!requested) return;

    const QByteArray suffix = QByteArray::number(QCoreApplication::applicationPid());
    const QByteArray metadata = firstEnvValue("CHIMERA_D3D11_TEXTURE_METADATA",
                                             "CHIMERA_EMUGL_D3D11_TEXTURE_METADATA",
                                             "Local\\ChimeraEmuglD3D11Meta_" + suffix);
    const QByteArray texture = firstEnvValue("CHIMERA_D3D11_TEXTURE_NAME",
                                            "CHIMERA_EMUGL_D3D11_TEXTURE_NAME",
                                            "Local\\ChimeraEmuglD3D11Texture_" + suffix);
    const QByteArray event = firstEnvValue("CHIMERA_D3D11_TEXTURE_EVENT",
                                          "CHIMERA_EMUGL_D3D11_TEXTURE_EVENT",
                                          "Local\\ChimeraEmuglD3D11Event_" + suffix);

    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_METADATA", metadata);
    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_NAME", texture);
    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_EVENT", event);
    setEnvIfChanged("CHIMERA_EMUGL_D3D11_TEXTURE_METADATA", metadata);
    setEnvIfChanged("CHIMERA_EMUGL_D3D11_TEXTURE_NAME", texture);
    setEnvIfChanged("CHIMERA_EMUGL_D3D11_TEXTURE_EVENT", event);
    setEnvIfChanged("CHIMERA_EMUGL_SHARED_TEXTURE_REQUESTED", QByteArrayLiteral("1"));

    qDebug() << "EmuGL shared D3D11 texture transport requested; metadata="
             << QString::fromLocal8Bit(metadata);
}

static void configureGfxstreamSharedTextureEnv(const QStringList &args) {
    const bool requested = args.contains(QStringLiteral("--gfxstream-shared-texture")) ||
                           isTruthyEnv("CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE") ||
                           !envValue("CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA").isEmpty();
    if (!requested) return;

    const QByteArray suffix = QByteArray::number(QCoreApplication::applicationPid());
    const QByteArray metadata = firstEnvValue("CHIMERA_D3D11_TEXTURE_METADATA",
                                             "CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA",
                                             "Local\\ChimeraGfxstreamD3D11Meta_" + suffix);
    const QByteArray texture = firstEnvValue("CHIMERA_D3D11_TEXTURE_NAME",
                                            "CHIMERA_GFXSTREAM_D3D11_TEXTURE_NAME",
                                            "Local\\ChimeraGfxstreamD3D11Texture_" + suffix);
    const QByteArray event = firstEnvValue("CHIMERA_D3D11_TEXTURE_EVENT",
                                          "CHIMERA_GFXSTREAM_D3D11_TEXTURE_EVENT",
                                          "Local\\ChimeraGfxstreamD3D11Event_" + suffix);

    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_METADATA", metadata);
    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_NAME", texture);
    setEnvIfChanged("CHIMERA_D3D11_TEXTURE_EVENT", event);
    setEnvIfChanged("CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA", metadata);
    setEnvIfChanged("CHIMERA_GFXSTREAM_D3D11_TEXTURE_NAME", texture);
    setEnvIfChanged("CHIMERA_GFXSTREAM_D3D11_TEXTURE_EVENT", event);
    setEnvIfChanged("CHIMERA_GFXSTREAM_SHARED_TEXTURE_REQUESTED", QByteArrayLiteral("1"));

    qDebug() << "gfxstream shared D3D11 texture transport requested; metadata="
             << QString::fromLocal8Bit(metadata);
}

static bool adbPackageExists(const QString &packageName) {
    const QString serial = QString::fromStdString(g_runtimeCfg.adbSerial);
    const QByteArray output = runAdbOutput(
        {"-s", serial, "shell", "pm", "path", packageName}, 5000);
    return output.contains("package:");
}

static void installGuestSupportApps() {
    const QString serial = QString::fromStdString(g_runtimeCfg.adbSerial);
    const auto materialFilesApk = g_projectRoot / "third_party" / "android-apps" / "material-files.apk";
    if (adbPackageExists(QStringLiteral("me.zhanghai.android.files"))) {
        qDebug() << "Material Files already installed";
        return;
    }
    if (!std::filesystem::exists(materialFilesApk)) {
        qWarning() << "Material Files APK not found; expected"
                   << QString::fromStdString(materialFilesApk.string());
        return;
    }

    const QString apk = QString::fromStdString(materialFilesApk.string());
    if (runAdbCommand({"-s", serial, "install", "-r", apk}, 30000)) {
        qDebug() << "Material Files installed";
    }
}

static void installChimeraLauncher() {
    const auto apkPath = g_projectRoot / "build" / "launcher" / "chimera-launcher.apk";
    if (!std::filesystem::exists(apkPath)) {
        qWarning() << "Chimera launcher APK not found; run scripts/build-chimera-launcher.ps1";
        return;
    }

    const QString serial = QString::fromStdString(g_runtimeCfg.adbSerial);
    const QString apk = QString::fromStdString(apkPath.string());

    // Skip the reinstall + force-stop + relaunch when the installed APK already
    // matches this build: "pm install -r" kills the running HOME on every boot,
    // which costs ~5-8s and flashes the home screen for no reason.
    QFile apkFile(apk);
    if (apkFile.open(QIODevice::ReadOnly)) {
        QCryptographicHash hash(QCryptographicHash::Md5);
        if (hash.addData(&apkFile)) {
            const QByteArray localMd5 = hash.result().toHex();
            const QByteArray deviceMd5 = runAdbOutput(
                {"-s", serial, "shell",
                 "p=$(pm path com.chimera.launcher | sed -n 's/^package://p' | head -n1); "
                 "[ -n \"$p\" ] && md5sum -b \"$p\""}, 5000);
            if (!localMd5.isEmpty() && deviceMd5.contains(localMd5)) {
                qDebug() << "Chimera launcher up to date; skipping reinstall";
                return;
            }
        }
    }

    if (!runAdbCommand({"-s", serial, "install", "-r", apk}, 20000))
        return;

    const QString component = QStringLiteral("com.chimera.launcher/.MainActivity");
    bool homeSet = runAdbCommand({"-s", serial, "shell", "cmd", "package",
                                  "set-home-activity", "--user", "0", component}, 15000);
    if (!homeSet) {
        homeSet = runAdbCommand({"-s", serial, "shell", "cmd", "package",
                                 "set-home-activity", component}, 15000);
    }
    if (!homeSet) {
        runAdbCommand({"-s", serial, "shell", "cmd", "role", "add-role-holder",
                       "android.app.role.HOME", "com.chimera.launcher"}, 10000);
    }
    runAdbCommand({"-s", serial, "shell", "am", "force-stop",
                   "com.chimera.launcher"}, 10000);
    runAdbCommand({"-s", serial, "shell", "am", "start", "-n", component, "-a",
                   "android.intent.action.MAIN", "-c",
                   "android.intent.category.HOME"}, 10000);
    runAdbCommand({"-s", serial, "shell", "am", "start", "-a",
                   "android.intent.action.MAIN", "-c",
                   "android.intent.category.HOME"}, 10000);
    qDebug() << "Chimera launcher installed and HOME requested";
}

static void applyGuestPerformanceSettings() {
    // All guest tuning in one ADB round-trip.
    runAdbShell({
        "wm", "size", "1920x1080", ";",
        "wm", "density", "320", ";",
        "settings", "put", "system", "accelerometer_rotation", "0", ";",
        "settings", "put", "system", "user_rotation", "0", ";",
        "cmd", "window", "set-ignore-orientation-request", "true", ";",
        "settings", "put", "system", "peak_refresh_rate", "60.0", ";",
        "settings", "put", "system", "min_refresh_rate", "60.0", ";",
        "settings", "put", "global", "window_animation_scale", "0", ";",
        "settings", "put", "global", "transition_animation_scale", "0", ";",
        "settings", "put", "global", "animator_duration_scale", "0", ";",
        "cmd", "power", "set-fixed-performance-mode-enabled", "true", ";",
        // Drop the old `put policy_control immersive.navigation=*`: it is a no-op on
        // this (gesture-nav, Android 12+) image — measured, forcing it produced zero
        // SurfaceFlinger NavigationBar frames and left the handle byte-identical, so
        // it never hid the bar. It is also NOT the cause of the reported home-handle
        // flicker: the handle is fully static guest-side (0 layer redraws), so that
        // flicker is a host present-timing artifact (S102 windowed-DWM frame-pacing
        // boundary) on the thin bright handle, not a guest setting. Clear any value
        // persisted by older builds so the dead command leaves nothing behind.
        "settings", "delete", "global", "policy_control",
    }, 5000);
    qDebug() << "Guest performance settings applied";

    // Window/transition animations stay OFF (scales = 0 above). The old code
    // re-enabled them under CHIMERA_GUEST_VULKAN for "smooth UI", justified by the
    // Session 99 "general-UI 60" evidence — but S101/S102 re-characterised that as
    // the ~57fps windowed-DWM frame-pacing boundary, not a true 60. On a high-refresh
    // host (e.g. 144Hz) the guest's ~57-60fps animation frames land on an irregular
    // 2-3-refresh pulldown, so app-switch / home-return transitions judder, and the
    // judder reads as a flicker on the thin bright gesture handle (user report:
    // flicker "mainly when switching apps / returning home"). Instant transitions
    // remove the animated motion entirely, so there is nothing to judder. The guest
    // handle content is static regardless (proven: 0 SF NavigationBar frames), so the
    // residual idle pulldown is a host-present concern (fullscreen / present-pacing),
    // not an animation one. CHIMERA_GUEST_VULKAN still only means "-feature Vulkan".
}

// Skip Android setup wizard and suppress first-boot prompts.
// Commands are idempotent — safe to run on every boot.
static void applyGuestFirstBootSetup() {
    runAdbShell({
        // Mark device as provisioned so setup wizard is skipped on next boot
        "settings", "put", "global", "device_provisioned", "1", ";",
        "settings", "put", "secure", "user_setup_complete", "1", ";",
        // Suppress "finish setting up your device" notifications
        "settings", "put", "global", "setup_wizard_has_run", "1", ";",
        // Keep screen on while charging (emulator always "charging")
        "settings", "put", "global", "stay_on_while_plugged_in", "3", ";",
        // Disable annoying "Select home app" dialog by accepting the default
        "settings", "put", "secure", "default_input_method",
            "com.google.android.inputmethod.latin/com.android.inputmethod.latin.LatinIME", ";",
        // Present a usable desktop instead of leaving the stream on a mostly
        // empty lock/loading surface after boot.
        "input", "keyevent", "224", ";", // KEYCODE_WAKEUP
        "wm", "dismiss-keyguard", ";",
        "input", "keyevent", "82", ";",  // KEYCODE_MENU, unlock fallback
        "input", "keyevent", "3",         // KEYCODE_HOME
    }, 8000);
    qDebug() << "Guest first-boot setup applied";
}

// No skiavk (Vulkan HWUI / SF RenderEngine) switch exists here — do not re-add one.
// The google_apis_playstore image is a user build (ro.debuggable=0): "adb shell
// stop"/"start" fail with "Must be root", so the framework restart such a switch
// requires can never run, and init does not translate
// ro.boot.debug.renderengine.backend, so SurfaceFlinger can never leave SkiaGL.
// A half-applied state (HWUI on Vulkan, SF still on GLES) blanks every app window:
// guest Vulkan surfaces live in host NVIDIA memory that the SwiftShader-ES GL
// compositor cannot sample. Probed 2026-07-02: boot-time "-systemui-renderer
// skiavk" renders Pipeline=Skia (Vulkan) but home/Settings screencaps are uniform
// blanks — this was the "-Fast loads to a black center screen" bug.

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

    // Write debug log to file so verifiers can inspect output without a debug viewer.
    static FILE *g_logFile = nullptr;
    if (const char *logPath = std::getenv("CHIMERA_LOG_PATH")) {
        if (logPath[0] != '\0')
            g_logFile = fopen(logPath, "w");
    }
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--hcs-backend") == 0 || strcmp(argv[i], "--qemu-backend") == 0 ||
            strcmp(argv[i], "--cuttlefish") == 0) {
            if (!g_logFile)
                g_logFile = fopen("chimera-debug.log", "w");
            break;
        }
    }
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &msg) {
        if (msg.contains(QStringLiteral("QNetworkReplyImpl: backend error: caching was enabled")) ||
            msg.startsWith(QStringLiteral("setCachingEnabled:"))) {
            return;
        }
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
    // Pin the threaded render loop: guest frames arrive from the capture worker as
    // queued signals on the GUI thread; on the basic (GUI-thread) render loop, scroll-
    // time GUI-thread work delays those signals and depresses render cadence below the
    // guest's. The threaded loop renders on its own thread so it tracks the guest 1:1.
    // Allow an explicit override for debugging (QSG_RENDER_LOOP already set wins).
    if (qEnvironmentVariableIsEmpty("QSG_RENDER_LOOP"))
        qputenv("QSG_RENDER_LOOP", "threaded");

    QGuiApplication app(argc, argv);
    app.setApplicationName("Chimera");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("chimera-emulator");
    QQuickStyle::setStyle(QStringLiteral("Basic"));
    const bool noEmulator = app.arguments().contains(QStringLiteral("--no-emulator"));
    // Display path: default to headless GPU/shared-texture transports. Raw
    // screenshot transports are diagnostics-only because 1080p readback can
    // preempt foreground desktop/audio work and cannot prove true 60 FPS.
    const bool nativeDisplayRequestedFromEnv = isTruthyEnv("CHIMERA_ENABLE_NATIVE_EMBED");
    const bool unsafeNativeWindowAllowedFromEnv = isTruthyEnv("CHIMERA_ALLOW_UNSAFE_NATIVE_WINDOW");
    const bool nativeDisplayRequested = app.arguments().contains(QStringLiteral("--native-embed"));
    const bool unsafeNativeWindowAllowed =
        app.arguments().contains(QStringLiteral("--allow-unsafe-native-window"));
    const bool unsafeVisibleWindowDiagnosticsAllowed =
        isTruthyEnv("CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW");
    if (nativeDisplayRequestedFromEnv || unsafeNativeWindowAllowedFromEnv) {
        qWarning() << "Ignoring native emulator display environment variables; use --native-embed --allow-unsafe-native-window for local diagnostics";
    }
    const bool nativeDisplayEnabled =
        nativeDisplayRequested && unsafeNativeWindowAllowed && unsafeVisibleWindowDiagnosticsAllowed;
    if (nativeDisplayRequested && (!unsafeNativeWindowAllowed || !unsafeVisibleWindowDiagnosticsAllowed)) {
        qWarning() << "Native emulator embed is unsafe because it exposes a stock emulator window; refusing without --allow-unsafe-native-window and CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1";
    }
    const bool windowCaptureRequestedFromEnv = isTruthyEnv("CHIMERA_ENABLE_WINDOW_CAPTURE");
    const bool unsafeWindowCaptureAllowedFromEnv = isTruthyEnv("CHIMERA_ALLOW_UNSAFE_WINDOW_CAPTURE");
    const bool windowCaptureRequested = app.arguments().contains(QStringLiteral("--window-capture"));
    const bool unsafeWindowCaptureAllowed =
        app.arguments().contains(QStringLiteral("--allow-unsafe-window-capture"));
    if (windowCaptureRequestedFromEnv || unsafeWindowCaptureAllowedFromEnv) {
        qWarning() << "Ignoring window capture environment variables; use --window-capture --allow-unsafe-window-capture for local diagnostics";
    }
    const bool windowCaptureEnabled =
        windowCaptureRequested && unsafeWindowCaptureAllowed && unsafeVisibleWindowDiagnosticsAllowed;
    if (windowCaptureRequested && (!unsafeWindowCaptureAllowed || !unsafeVisibleWindowDiagnosticsAllowed)) {
        qWarning() << "Window D3D11 capture is unsafe because it depends on the stock emulator HWND; refusing without --allow-unsafe-window-capture and CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW=1";
    }
    if (nativeDisplayEnabled || windowCaptureEnabled) {
        qputenv("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION", "1");
    } else {
        qunsetenv("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION");
    }
    const bool rawCaptureFallbackRequestedFromEnv =
        isTruthyEnv("CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK");
    if (rawCaptureFallbackRequestedFromEnv) {
        qWarning() << "Ignoring CHIMERA_ALLOW_RAW_CAPTURE_FALLBACK; use --allow-raw-capture-fallback for local diagnostics";
    }
    const bool allowRawCaptureFallback =
        app.arguments().contains(QStringLiteral("--allow-raw-capture-fallback"));
    const bool streamCapture = !nativeDisplayEnabled;
    // v2: stock QEMU + VNC display + real QMP input (Phase 1-3)
    const bool cuttlefishMode  = app.arguments().contains(QStringLiteral("--cuttlefish"));
    const bool qemuBackendMode = cuttlefishMode ||
                                 app.arguments().contains(QStringLiteral("--qemu-backend"));
    // Phase 5: Hyper-V HCS + GPU-PV backend
    const bool hcsBackendMode = app.arguments().contains(QStringLiteral("--hcs-backend"));
    configureEmuglSharedTextureEnv(app.arguments());
    configureGfxstreamSharedTextureEnv(app.arguments());

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

    // Display-path flag for QML: true only when the legacy native window-embed
    // path is requested. Default is gRPC streaming, so NativeEmulatorView stays
    // dormant and GuestDisplay renders the streamed frames.
    engine.rootContext()->setContextProperty("nativeEmbedEnabled", nativeDisplayEnabled);

    // Expose instance manager to QML
    chimera::QmlAndroidControls qmlAndroidControls;
    engine.rootContext()->setContextProperty("AndroidControls", &qmlAndroidControls);

    // Expose instance manager to QML
    chimera::QmlInstanceManager qmlInstanceMgr;
    engine.rootContext()->setContextProperty("InstanceManager", &qmlInstanceMgr);

    // Expose macro engine to QML
    chimera::QmlMacroEngine qmlMacroEngine;
    engine.rootContext()->setContextProperty("MacroEngine", &qmlMacroEngine);

    // Expose input mapper to QML
    chimera::QmlInputMapper qmlInputMapper;
    engine.rootContext()->setContextProperty("InputMapper", &qmlInputMapper);

    // Screen recorder
    chimera::ScreenRecorder screenRecorder;
    engine.rootContext()->setContextProperty("ScreenRecorder", &screenRecorder);

    // Performance monitoring
    auto *perfMonitor = new chimera::graphics::PerformanceMonitor(&app);
    engine.rootContext()->setContextProperty("PerfMonitor", perfMonitor);
    auto grpcCaptureForInput = std::make_shared<QPointer<chimera::graphics::GrpcFramebufferCapture>>();
    auto grpcMmapCaptureForInput = std::make_shared<QPointer<chimera::graphics::GrpcMmapFramebufferCapture>>();

    // Wire InputBridge events → visible latency tracking + macro recording
    // QPointer ensures no dangling access if perfMonitor is destroyed before InputBridge
    QPointer<chimera::graphics::PerformanceMonitor> weakPerf(perfMonitor);
    chimera::input::InputBridge::instance().setEventCallback(
        [weakPerf, grpcCaptureForInput, grpcMmapCaptureForInput](
            const chimera::input::InputBridge::Event &ev) {
            if (weakPerf) weakPerf->onInputEvent();
            if (grpcCaptureForInput && *grpcCaptureForInput)
                (*grpcCaptureForInput)->notifyInputActivity();
            if (grpcMmapCaptureForInput && *grpcMmapCaptureForInput)
                (*grpcMmapCaptureForInput)->notifyInputActivity();

            auto &macro = chimera::input::MacroEngine::instance();
            if (!macro.isRecording()) return;

            chimera::input::MacroEvent me;
            me.timestamp = macro.recordingElapsed();
            me.x = ev.x;
            me.y = ev.y;
            me.keyCode = ev.code;

            using EvType = chimera::input::InputBridge::Event::Type;
            switch (ev.type) {
            case EvType::MouseButtonDown: me.type = chimera::input::MacroEvent::Tap;        break;
            case EvType::MouseMove:       me.type = chimera::input::MacroEvent::Swipe;      break;
            case EvType::KeyDown:         me.type = chimera::input::MacroEvent::KeyPress;   break;
            case EvType::KeyUp:           me.type = chimera::input::MacroEvent::KeyRelease; break;
            default: return;
            }
            macro.recordEvent(me);
        });

    bool emulatorStarted = false;
    int grpcCaptureWidth = chimera::graphics::GrpcFramebufferCapture::kMinimumCaptureWidth;
    int grpcCaptureHeight = chimera::graphics::GrpcFramebufferCapture::kMinimumCaptureHeight;
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

    std::string g_instanceName;  // persists for NativeEmulatorView PID wiring
    bool v1QuickBootEnabled = false;
    // Resolved interactive knobs, surfaced later in the CHIMERA_DISPLAY log line.
    std::string g_v1ProcessPriority = "below_normal";
    int g_v1Cpus = 4;
    int g_v1RamMB = 4096;

    // v1: Start emulator instance
    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && !g_adbPath.empty()) {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "chimera_dev";
        // cpus/ramMB are env-overridable but still floored by
        // normalizedInstanceConfig (>=4 / >=4096) unless that floor is lowered.
        cfg.cpus = envIntOrDefault("CHIMERA_INTERACTIVE_CPUS", 4);
        cfg.ramMB = envIntOrDefault("CHIMERA_INTERACTIVE_RAM_MB", 4096);
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.dpi = 320;
        cfg.deviceProfile = "Samsung Galaxy S24 Ultra"; // Unlock high FPS/quality in games
        cfg.graphicsRenderer = "host";
        cfg.allowVisibleEmulatorWindow = nativeDisplayEnabled || windowCaptureEnabled;
        cfg.headless = !cfg.allowVisibleEmulatorWindow;
        cfg.quickBoot = isTruthyEnv("CHIMERA_QUICK_BOOT");
        v1QuickBootEnabled = cfg.quickBoot;
        // Keep emulator/qemu below foreground desktop/audio work. Boot and
        // 1080p screenshot readback can otherwise preempt normal-priority
        // media threads and cause music stutter or crackle on the host.
        // CHIMERA_INTERACTIVE_PRIORITY (idle|below_normal|normal) trades host
        // audio headroom against interactive FPS; default preserves today's
        // effective behavior.
        cfg.processPriority =
            sanitizeProcessPriority(envOrDefault("CHIMERA_INTERACTIVE_PRIORITY", "below_normal"));
        cfg.dataDir = (g_projectRoot / "instances" / cfg.name).make_preferred();
        guestInputSize = QSize(cfg.width, cfg.height);

        // Remove existing instance with same name to avoid duplicates in memory
        mgr.deleteInstance(cfg.name);
        if (mgr.createInstance(cfg)) {
            qDebug() << "Instance created:" << QString::fromStdString(cfg.name);
            // Read back the normalized config so the CHIMERA_DISPLAY log reports
            // the values actually in effect (after the cpus/ram/priority floors).
            const InstanceConfig effectiveCfg = mgr.getInstanceConfig(cfg.name);
            if (!effectiveCfg.name.empty()) {
                g_v1ProcessPriority = effectiveCfg.processPriority;
                g_v1Cpus = effectiveCfg.cpus;
                g_v1RamMB = effectiveCfg.ramMB;
            }
            mgr.setStateCallback([&app](const std::string &name, VMState s) {
                qDebug() << "Instance" << QString::fromStdString(name)
                         << "state:" << static_cast<int>(s);
                if (s == VMState::Error &&
                    (isTruthyEnv("CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE") ||
                     isTruthyEnv("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE"))) {
                    qCritical() << "Required shared texture VM exited or failed; terminating Chimera";
                    QTimer::singleShot(0, &app, []() {
                        QCoreApplication::exit(4);
                    });
                }
            });
            if (mgr.startInstance(cfg.name)) {
                qDebug() << "Instance started:" << QString::fromStdString(cfg.name);
                emulatorStarted = true;
                g_instanceName = cfg.name;
            } else {
                qWarning() << "Failed to start instance" << QString::fromStdString(cfg.name);
            }
        } else {
            qWarning() << "Failed to create instance" << QString::fromStdString(cfg.name);
            if (isTruthyEnv("CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE") ||
                isTruthyEnv("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE")) {
                qCritical() << "Required shared texture runtime is unavailable; exiting";
                return 3;
            }
        }

        // Populate runtime config. Honor CHIMERA_EMULATOR_CONSOLE_PORT so the
        // host's adb/console/gRPC wiring targets the same device the emulator
        // actually launches on (InstanceManager applies the same override and
        // derivation). Without this, a non-default port leaves the host wired to
        // emulator-5554 while the device is e.g. emulator-5560 — every post-boot
        // adb command (wake, dismiss keyguard, set HOME) fails and the screen
        // stays black.
        int consolePort = 5554;
        if (const char *overridePort = std::getenv("CHIMERA_EMULATOR_CONSOLE_PORT")) {
            try {
                consolePort = std::stoi(overridePort);
            } catch (...) {
                qWarning() << "Ignoring invalid CHIMERA_EMULATOR_CONSOLE_PORT:" << overridePort;
            }
        }
        g_runtimeCfg.consolePort = consolePort;
        g_runtimeCfg.adbPort     = consolePort + 1;
        g_runtimeCfg.grpcPort    = 8554 + ((consolePort - 5554) / 2) * 2;
        g_runtimeCfg.adbSerial   = "emulator-" + std::to_string(consolePort);

        // Configure input bridge for ADB forwarding (fallback)
        chimera::input::InputBridge::instance().setAdbConfig(g_adbPath, g_runtimeCfg.adbPort,
                                                              g_runtimeCfg.adbSerial);
        chimera::input::InputBridge::instance().setDisplaySize(cfg.width, cfg.height);

        // Wire SharedFolder ADB config for v1 push/pull
        chimera::storage::SharedFolder::instance().setAdbConfig(g_adbPath, g_runtimeCfg.adbSerial);

        // Wire ADB config to AndroidControls for APK installation
        qmlAndroidControls.setAdbConfig(
            QString::fromStdString(g_adbPath.string()),
            QString::fromStdString(g_runtimeCfg.adbSerial));

        // Android Console input on port 5554 (telnet protocol, NOT JSON QMP).
        // InputBridge priority: HvSocket > Console > QMP > ADB.
        // CHIMERA_INPUT_BACKEND=adb disables Console (force-fallback).
        const char *inputBackendEnv = std::getenv("CHIMERA_INPUT_BACKEND");
        const std::string inputBackend = inputBackendEnv ? inputBackendEnv : "auto";
        if (inputBackend != "adb" && inputBackend != "qmp") {
            auto *consoleInput = new chimera::input::AndroidConsoleInput(&app);
            consoleInput->setAutoReconnect(true);
            chimera::input::InputBridge::instance().setConsoleInput(consoleInput);
            consoleInput->connectToHost(QStringLiteral("127.0.0.1"), g_runtimeCfg.consolePort);
            qDebug() << "[main] Android Console input wired (CHIMERA_INPUT_BACKEND="
                     << inputBackend.c_str() << ")";

            // Emulator gRPC keyboard input — the console has no working
            // keyboard channel, so physical keys and IME text go via gRPC.
            auto *grpcInput = new chimera::input::EmulatorGrpcInput(
                QStringLiteral("127.0.0.1"), g_runtimeCfg.grpcPort, &app);
            chimera::input::InputBridge::instance().setGrpcInput(grpcInput);
            qDebug() << "[main] Emulator gRPC keyboard input wired (port"
                     << g_runtimeCfg.grpcPort << ")";

            // Wire LocationSimulator → geo fix via the same console connection
            chimera::integration::LocationSimulator::instance().setGeoSink(
                [consoleInput](double lon, double lat, double alt) {
                    consoleInput->sendGeoFix(lon, lat, alt);
                });
            qDebug() << "[main] LocationSimulator geo fix wired via console";

            // Wire ClipboardBridge host→guest via console clipboard set
            chimera::integration::ClipboardBridge::instance().setGuestSink(
                [consoleInput](const std::string &utf8text) {
                    consoleInput->sendClipboardSet(utf8text);
                });
            chimera::integration::ClipboardBridge::instance().initialize();
            qDebug() << "[main] ClipboardBridge wired via console";

            // Wire console input to QmlAndroidControls for sensor/battery commands
            qmlAndroidControls.setConsoleInput(consoleInput);
        } else {
            qDebug() << "[main] Console input disabled (CHIMERA_INPUT_BACKEND=" << inputBackend.c_str() << ")";
        }

        // Guest audio is configured per instance (enableAudio field); emulator routes
        // Android Goldfish audio to host WASAPI automatically when -no-audio is absent.

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
            // Only forward gamepad input while Chimera is the active application.
            // XInput reads controllers system-wide, so an unfocused poll leaks the
            // user's inputs from another game straight into the guest — a B press
            // becomes Android BACK and kills the foreground app (observed as
            // "guest randomly stops rendering" while the user played a controller
            // game). A button held across focus loss stays held in the guest until
            // the window regains focus and the next poll diffs it — acceptable.
            if (QGuiApplication::applicationState() == Qt::ApplicationActive) {
                gp.poll();
            }
        });
        gpTimer->start(16); // ~60 Hz polling

        // GPS route simulation: advance LocationSimulator at 1 Hz
        auto *gpsRouteTimer = new QTimer(&app);
        QObject::connect(gpsRouteTimer, &QTimer::timeout, []() {
            chimera::integration::LocationSimulator::instance().update(1.0);
        });
        gpsRouteTimer->start(1000);

        // Keep the Android guest, input, and capture request at no less than
        // 1920x1080. Performance work must happen above this floor; smaller
        // raw screenshot requests are not an acceptable way to report 60 FPS.
        grpcCaptureWidth = chimera::graphics::GrpcFramebufferCapture::kMinimumCaptureWidth;
        grpcCaptureHeight = chimera::graphics::GrpcFramebufferCapture::kMinimumCaptureHeight;
        if (const char *captureWidthEnv = std::getenv("CHIMERA_CAPTURE_WIDTH")) {
            grpcCaptureWidth = std::atoi(captureWidthEnv);
        }
        if (const char *captureHeightEnv = std::getenv("CHIMERA_CAPTURE_HEIGHT")) {
            grpcCaptureHeight = std::atoi(captureHeightEnv);
        }
        const QSize grpcCaptureSize =
            chimera::graphics::GrpcFramebufferCapture::normalizedCaptureSize(
                grpcCaptureWidth, grpcCaptureHeight);
        grpcCaptureWidth = grpcCaptureSize.width();
        grpcCaptureHeight = grpcCaptureSize.height();
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

    // Optional initial window position ("x,y", negatives valid for a left-of-primary
    // monitor). Verifiers export CHIMERA_VERIFY_WINDOW_ORIGIN so the test window
    // opens on the user's secondary screen from the very first frame — if it first
    // appears over the user's active screen they will (reasonably) close it, which
    // aborts the run.
    if (const QByteArray origin = envValue("CHIMERA_VERIFY_WINDOW_ORIGIN"); !origin.isEmpty()) {
        const QList<QByteArray> parts = origin.split(',');
        bool okX = false, okY = false;
        const int x = parts.value(0).trimmed().toInt(&okX);
        const int y = parts.value(1).trimmed().toInt(&okY);
        if (okX && okY && !roots.isEmpty()) {
            if (auto *rootWindow = qobject_cast<QWindow *>(roots.first())) {
                rootWindow->setPosition(x, y);
                qDebug() << "[main] window origin from CHIMERA_VERIFY_WINDOW_ORIGIN:" << x << y;
            }
        }
    }

    // Pin NativeEmulatorView only for explicit unsafe diagnostics. The default
    // product path is a headless emulator plus GuestDisplay streaming.
    uint32_t emulatorRootPid = 0;
    if (emulatorStarted && !g_instanceName.empty()) {
        const uint32_t emulatorPid = chimera::instance::InstanceManager::instance()
                                         .emulatorProcessId(g_instanceName);
        if (emulatorPid != 0) {
            emulatorRootPid = emulatorPid;
            qmlAndroidControls.setEmulatorPid(emulatorPid);
            if (nativeDisplayEnabled) {
                for (auto *obj : roots) {
                    auto *nativeView = obj->findChild<chimera::NativeEmulatorView *>("nativeDisplay");
                    if (nativeView) {
                        nativeView->setEmulatorPid(emulatorPid);
                        qDebug() << "[main] NativeEmulatorView pinned to emulator PID" << emulatorPid;
                        break;
                    }
                }
            }
        }
    }
    if (!guestDisplay) {
        qWarning() << "GuestDisplay not found; frame capture will not be visible";
    } else {
        if (guestInputSize.isValid())
            guestDisplay->setGuestSize(guestInputSize);
        // Wire visible latency: framePainted → onFrameRendered (render thread → main thread)
        QObject::connect(guestDisplay, &chimera::GuestDisplay::framePainted,
                         perfMonitor, [perfMonitor]() {
            perfMonitor->onFrameRendered();
        }, Qt::QueuedConnection);
    }

    auto currentDisplaySize = std::make_shared<QSize>();
    auto logicalGuestSize = std::make_shared<QSize>(guestInputSize);
    auto wireCapture = [&](chimera::graphics::FramebufferCapture *cap,
                           bool streamMetricsFromBackend = false) {
        if (streamMetricsFromBackend) {
            QObject::connect(cap, &chimera::graphics::FramebufferCapture::streamFrameReceived,
                             perfMonitor, &chimera::graphics::PerformanceMonitor::onFrameReceived);
        }
        QObject::connect(cap, &chimera::graphics::FramebufferCapture::frameReady,
                         &engine, [guestDisplay, &screenRecorder, perfMonitor, qmpInput, currentDisplaySize, logicalGuestSize, streamMetricsFromBackend](const QImage &img) {
            if (!streamMetricsFromBackend)
                perfMonitor->onFrameReceived(true);
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
        QObject::connect(cap, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady,
                         &engine, [guestDisplay, perfMonitor, qmpInput, currentDisplaySize, logicalGuestSize, streamMetricsFromBackend](const QString &textureName,
                                                                                                                              const QSize &size,
                                                                                                                              quint64 sequence,
                                                                                                                              bool hasAlpha) {
            if (!streamMetricsFromBackend)
                perfMonitor->onFrameReceived(true);
            const QSize inputSize = logicalGuestSize->isValid() ? *logicalGuestSize : size;
            if (*currentDisplaySize != inputSize) {
                *currentDisplaySize = inputSize;
                chimera::input::InputBridge::instance().setDisplaySize(inputSize.width(), inputSize.height());
                if (qmpInput) qmpInput->setDisplaySize(inputSize.width(), inputSize.height());
            }
            if (guestDisplay) {
                guestDisplay->setSharedD3D11Texture(textureName, size, sequence, hasAlpha);
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
    chimera::graphics::AdbH264FramebufferCapture *h264Capture = nullptr;
    chimera::graphics::FramebufferCapture *grpcCapture = nullptr;
    chimera::graphics::GrpcFramebufferCapture *grpcUnaryCapture = nullptr;
    chimera::graphics::SharedD3D11TextureCapture *sharedTextureCapture = nullptr;
    chimera::graphics::SharedMemoryFramebufferCapture *sharedMemoryCapture = nullptr;
    chimera::graphics::WindowD3D11Capture *windowD3D11Capture = nullptr;
    auto androidBootReady = std::make_shared<bool>(false);
    auto sharedTextureReceivedFrame = std::make_shared<bool>(false);
    auto lastSharedTextureFrameMs = std::make_shared<qint64>(0);
    auto shmemReceivedFrame = std::make_shared<bool>(false);
    auto windowCaptureReceivedFrame = std::make_shared<bool>(false);
    const QString sharedTextureMetadataName = QString::fromLocal8Bit(
        std::getenv("CHIMERA_D3D11_TEXTURE_METADATA") ? std::getenv("CHIMERA_D3D11_TEXTURE_METADATA") : "");
    const QString sharedTextureEvent = QString::fromLocal8Bit(
        std::getenv("CHIMERA_D3D11_TEXTURE_EVENT") ? std::getenv("CHIMERA_D3D11_TEXTURE_EVENT") : "");
    const QString shmemName = QString::fromLocal8Bit(
        std::getenv("CHIMERA_SHMEM_FRAME_NAME") ? std::getenv("CHIMERA_SHMEM_FRAME_NAME") : "");
    const QString shmemEvent = QString::fromLocal8Bit(
        std::getenv("CHIMERA_SHMEM_FRAME_EVENT") ? std::getenv("CHIMERA_SHMEM_FRAME_EVENT") : "");
    const bool emuglSharedTextureRequested =
        isTruthyEnv("CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE") ||
        isTruthyEnv("CHIMERA_EMUGL_SHARED_TEXTURE_REQUESTED");
    const bool gfxstreamSharedTextureRequested =
        isTruthyEnv("CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE") ||
        isTruthyEnv("CHIMERA_GFXSTREAM_SHARED_TEXTURE_REQUESTED");
    const bool requireEmuglSharedTexture =
        isTruthyEnv("CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE");
    const bool requireGfxstreamSharedTexture =
        isTruthyEnv("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE");
    const bool emuglRuntimeUnavailable =
        emuglSharedTextureRequested &&
        envValue("CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_READY") == QByteArrayLiteral("0");
    const bool gfxstreamRuntimeUnavailable =
        gfxstreamSharedTextureRequested &&
        envValue("CHIMERA_GFXSTREAM_SHARED_TEXTURE_RUNTIME_READY") == QByteArrayLiteral("0");
    if (emuglRuntimeUnavailable || gfxstreamRuntimeUnavailable) {
        qWarning() << "Skipping shared texture capture:"
                   << QString::fromLocal8Bit(
                          emuglRuntimeUnavailable
                              ? envValue("CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_STATUS")
                              : envValue("CHIMERA_GFXSTREAM_SHARED_TEXTURE_RUNTIME_STATUS"));
    }

    if (streamCapture && !sharedTextureMetadataName.isEmpty() &&
        !emuglRuntimeUnavailable && !gfxstreamRuntimeUnavailable) {
        sharedTextureCapture = new chimera::graphics::SharedD3D11TextureCapture(
            sharedTextureMetadataName, sharedTextureEvent, &app);
        sharedTextureCapture->setIntervalMs(16);
        wireCapture(sharedTextureCapture, true);
        QObject::connect(sharedTextureCapture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady,
                         &app, [sharedTextureReceivedFrame, lastSharedTextureFrameMs]() {
            *sharedTextureReceivedFrame = true;
            *lastSharedTextureFrameMs = QDateTime::currentMSecsSinceEpoch();
        });

        auto *sharedTextureRetryTimer = new QTimer(&app);
        sharedTextureRetryTimer->setInterval(500);
        QObject::connect(sharedTextureRetryTimer, &QTimer::timeout, &app,
                         [sharedTextureCapture, sharedTextureRetryTimer]() {
            if (sharedTextureCapture->isRunning()) {
                sharedTextureRetryTimer->stop();
                return;
            }
            if (sharedTextureCapture->start()) {
                qDebug() << "Shared D3D11 texture display capture started";
                sharedTextureRetryTimer->stop();
            }
        });
        sharedTextureRetryTimer->start();
    }

    if (streamCapture && !shmemName.isEmpty() && !sharedTextureCapture) {
        sharedMemoryCapture = new chimera::graphics::SharedMemoryFramebufferCapture(
            shmemName, shmemEvent, &app);
        sharedMemoryCapture->setIntervalMs(16);
        wireCapture(sharedMemoryCapture, true);
        QObject::connect(sharedMemoryCapture, &chimera::graphics::FramebufferCapture::frameReady,
                         &app, [shmemReceivedFrame]() {
            *shmemReceivedFrame = true;
        });

        auto *shmemRetryTimer = new QTimer(&app);
        shmemRetryTimer->setInterval(500);
        QObject::connect(shmemRetryTimer, &QTimer::timeout, &app,
                         [sharedMemoryCapture, shmemRetryTimer]() {
            if (sharedMemoryCapture->isRunning()) {
                shmemRetryTimer->stop();
                return;
            }
            if (sharedMemoryCapture->start()) {
                qDebug() << "Shared-memory display capture started";
                shmemRetryTimer->stop();
            }
        });
        shmemRetryTimer->start();
    }

    if (streamCapture && windowCaptureEnabled && emulatorStarted && emulatorRootPid != 0 &&
        !sharedTextureCapture && !sharedMemoryCapture) {
        windowD3D11Capture = new chimera::graphics::WindowD3D11Capture(
            emulatorRootPid,
            QString::fromStdString(g_instanceName),
            g_runtimeCfg.consolePort,
            &app);
        windowD3D11Capture->setIntervalMs(16);
        wireCapture(windowD3D11Capture, true);
        QObject::connect(windowD3D11Capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady,
                         &app, [windowCaptureReceivedFrame]() {
            *windowCaptureReceivedFrame = true;
        });
        if (windowD3D11Capture->start()) {
            qDebug() << "Window D3D11 display capture requested";
        }
    }

    const bool strictGpuCapture =
        (windowCaptureEnabled && !allowRawCaptureFallback) ||
        requireEmuglSharedTexture ||
        requireGfxstreamSharedTexture;

    // gRPC is the always-on safety capture for emulator.exe. Shared D3D11 remains
    // the preferred path, but its gfxstream producer can stop publishing after boot
    // while guest input still works; keeping this backup alive prevents a stale host
    // frame from looking like dead clicks. --allow-raw-capture-fallback enables MMAP /
    // screenrecord diagnostic variants instead of the default unary gRPC path.
    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && emulatorStarted && streamCapture &&
        !strictGpuCapture) {
        const char *transportEnv = std::getenv("CHIMERA_GRPC_TRANSPORT");
        const char *videoTransportEnv = std::getenv("CHIMERA_VIDEO_TRANSPORT");
        const bool useScreenrecordVideo = videoTransportEnv &&
            QString::fromLocal8Bit(videoTransportEnv).compare(
                QStringLiteral("screenrecord"), Qt::CaseInsensitive) == 0;
        const bool useMmapGrpc = transportEnv &&
                                 QString::fromLocal8Bit(transportEnv).compare(
                                     QStringLiteral("mmap"), Qt::CaseInsensitive) == 0;
        if (useScreenrecordVideo) {
            const QString ffmpeg = findFFmpegBinary();
            if (!ffmpeg.isEmpty()) {
                h264Capture = new chimera::graphics::AdbH264FramebufferCapture(
                    QString::fromStdString(g_adbPath.string()),
                    ffmpeg,
                    QString::fromStdString(g_runtimeCfg.adbSerial),
                    grpcCaptureWidth,
                    grpcCaptureHeight,
                    &app);
                h264Capture->setIntervalMs(16);
                grpcCapture = h264Capture;
                qDebug() << "ADB H.264 screenrecord display capture selected at"
                         << grpcCaptureWidth << "x" << grpcCaptureHeight;
            } else {
                qWarning() << "CHIMERA_VIDEO_TRANSPORT=screenrecord requested but ffmpeg.exe was not found";
            }
        }
        if (!grpcCapture && useMmapGrpc) {
            auto *grpcMmapCapture = new chimera::graphics::GrpcMmapFramebufferCapture(
                QStringLiteral("127.0.0.1"), g_runtimeCfg.grpcPort, grpcCaptureWidth,
                grpcCaptureHeight, &app);
            grpcCapture = grpcMmapCapture;
            *grpcMmapCaptureForInput = grpcMmapCapture;
        } else if (!grpcCapture) {
            // Use the derived gRPC port (8554 + console offset), not a hardcoded
            // 8554 — otherwise a non-default console port (e.g. the verifier's
            // auto-picked 5560 → emulator gRPC 8560) makes capture talk to the
            // wrong port and deliver zero frames (total=0) while ADB still works.
            grpcUnaryCapture = new chimera::graphics::GrpcFramebufferCapture(
                QStringLiteral("127.0.0.1"), g_runtimeCfg.grpcPort, grpcCaptureWidth,
                grpcCaptureHeight, &app);
            grpcCapture = grpcUnaryCapture;
            // Unary getScreenshot is a CPU/GPU readback fallback, not the 60 FPS
            // production path. Keep it conservative so it does not preempt host
            // audio; true 1080p/60 must come from shared texture/custom runtime.
            grpcCapture->setIntervalMs(33);
        }
        *grpcCaptureForInput = grpcUnaryCapture;
        wireCapture(grpcCapture, true);
        if (sharedMemoryCapture) {
            QObject::connect(sharedMemoryCapture, &chimera::graphics::FramebufferCapture::frameReady,
                             &app, [grpcCapture]() {
                if (grpcCapture->isRunning())
                    grpcCapture->stop();
            });
        }
        if (windowD3D11Capture) {
            QObject::connect(windowD3D11Capture, &chimera::graphics::FramebufferCapture::sharedD3D11TextureReady,
                             &app, [grpcCapture]() {
                if (grpcCapture->isRunning())
                    grpcCapture->stop();
            });
        }

        // Shared-texture stall watchdog. The gfxstream producer can stop publishing
        // long after its first frame (observed: repeated "Failed to find ColorBuffer"
        // then no further "published sequence"). The guest and the input path stay
        // healthy, so the host just shows a frozen frame and every click looks
        // ignored. Revive the gRPC capture when frames stop, park it again when the
        // producer recovers — only one capture may drive GuestDisplay at a time.
        if (sharedTextureCapture && grpcCapture) {
            // An idle guest legitimately publishes no frames (static screen = correct
            // no-redraw), so any threshold trades idle churn against stall-detect
            // latency. Measured idle gaps on Home reach ~3s; 6s keeps the fallback
            // parked while idle and still recovers a truly dead producer promptly.
            constexpr qint64 kSharedTextureStallMs = 6000;
            auto *stallWatchdog = new QTimer(&app);
            stallWatchdog->setInterval(1000);
            QObject::connect(stallWatchdog, &QTimer::timeout, &app,
                             [grpcCapture, sharedTextureReceivedFrame, lastSharedTextureFrameMs]() {
                if (!*sharedTextureReceivedFrame) return;
                const qint64 idleMs =
                    QDateTime::currentMSecsSinceEpoch() - *lastSharedTextureFrameMs;
                if (idleMs > kSharedTextureStallMs) {
                    if (!grpcCapture->isRunning() && grpcCapture->start()) {
                        qWarning() << "Shared texture producer stalled for" << idleMs
                                   << "ms; resuming gRPC capture so the guest stays visible";
                    }
                } else if (grpcCapture->isRunning()) {
                    qDebug() << "Shared texture producer recovered; parking gRPC capture";
                    grpcCapture->stop();
                }
            });
            stallWatchdog->start();
        }

        if (!g_adbPath.empty() && app.arguments().contains(QStringLiteral("--adb-display-fallback"))) {
            adbCapture = new chimera::graphics::AdbFramebufferCapture(
                QString::fromStdString(g_adbPath.string()), g_runtimeCfg.adbPort, false, &app);
            adbCapture->setIntervalMs(1000);
            wireCapture(adbCapture);
        }
    } else if (strictGpuCapture) {
        qWarning() << "Strict GPU capture enabled; raw gRPC/ADB fallback is disabled";
    } else if (sharedTextureCapture) {
        qDebug() << "Shared D3D11 texture path wired; gRPC capture not started";
    }

    // One authoritative, machine-greppable line naming the display path that
    // actually wired up (start-chimera's banner reflects intent; a custom
    // runtime can silently fall back to gRPC). Mirrors the CHIMERA_PERF format.
    if (emulatorStarted) {
        const char *displayPath =
            sharedTextureCapture ? "gfxstream-shared-texture" :
            sharedMemoryCapture  ? "shmem-cpu-readback"       :
            windowD3D11Capture   ? "window-d3d11"             :
            h264Capture          ? "adb-h264"                 :
            grpcUnaryCapture     ? "grpc-unary"               :
            grpcCapture          ? "grpc-mmap"                :
            adbCapture           ? "adb-raw"                  : "none";
        const char *fallbackPath =
            grpcUnaryCapture ? "grpc-unary" :
            grpcCapture      ? "grpc-mmap"  :
            adbCapture       ? "adb-raw"    : "none";
        const bool sharedTexture =
            sharedTextureCapture || sharedMemoryCapture || windowD3D11Capture;
        qInfo().noquote()
            << QStringLiteral("CHIMERA_DISPLAY path=%1 sharedTexture=%2 fallback=%3 "
                              "priority=%4 cpus=%5 ramMB=%6")
                   .arg(QString::fromLatin1(displayPath))
                   .arg(sharedTexture ? QStringLiteral("yes") : QStringLiteral("no"))
                   .arg(QString::fromLatin1(fallbackPath))
                   .arg(QString::fromStdString(g_v1ProcessPriority))
                   .arg(g_v1Cpus)
                   .arg(g_v1RamMB);
    }

    if ((requireEmuglSharedTexture || requireGfxstreamSharedTexture) && streamCapture) {
        auto *strictSharedTextureWatchdog = new QTimer(&app);
        strictSharedTextureWatchdog->setInterval(1000);
        strictSharedTextureWatchdog->setProperty("postBootAttempts", 0);
        QObject::connect(strictSharedTextureWatchdog, &QTimer::timeout, &app,
                         [strictSharedTextureWatchdog, androidBootReady,
                          sharedTextureCapture, sharedTextureReceivedFrame]() {
            if (*sharedTextureReceivedFrame) {
                strictSharedTextureWatchdog->stop();
                return;
            }
            if (!*androidBootReady) {
                return;
            }
            const int attempts =
                strictSharedTextureWatchdog->property("postBootAttempts").toInt() + 1;
            strictSharedTextureWatchdog->setProperty("postBootAttempts", attempts);
            if (!sharedTextureCapture) {
                qCritical() << "Required shared texture capture was not configured; "
                               "refusing raw fallback";
                strictSharedTextureWatchdog->stop();
                QCoreApplication::exit(3);
                return;
            }
            if (!sharedTextureCapture->isRunning() && attempts >= 15) {
                qCritical() << "Required shared texture capture did not start after Android boot; "
                               "metadata mapping unavailable";
                strictSharedTextureWatchdog->stop();
                QCoreApplication::exit(3);
                return;
            }
            if (sharedTextureCapture->isRunning() && attempts >= 30) {
                qCritical() << "Required shared texture capture did not produce a frame after Android boot";
                strictSharedTextureWatchdog->stop();
                QCoreApplication::exit(3);
            }
        });
        strictSharedTextureWatchdog->start();
    }

    if (!hcsBackendMode && !qemuBackendMode && !noEmulator && emulatorStarted && !g_adbPath.empty()) {
        // Keep the QML "waiting for Android" placeholder up until the guest is
        // actually usable: with -no-boot-anim the shared-texture path delivers
        // black frames long before boot completes, and hiding the placeholder on
        // the first such frame reads as a dead black screen.
        qmlAndroidControls.setBootReady(false);
        auto *guestPerfTimer = new QTimer(&app);
        guestPerfTimer->setInterval(2000);
        guestPerfTimer->setProperty("attempts", 0);
        QObject::connect(guestPerfTimer, &QTimer::timeout,
                         [guestPerfTimer, androidBootReady, v1QuickBootEnabled, &qmlAndroidControls, &app]() {
            int attempts = guestPerfTimer->property("attempts").toInt() + 1;
            guestPerfTimer->setProperty("attempts", attempts);
            if (attempts >= 60) {
                guestPerfTimer->stop();
                qmlAndroidControls.setBootReady(true); // fail-open: show whatever the guest has
                return;
            }

            auto *proc = new QProcess(guestPerfTimer);
            startLowInterferenceProcess(proc,
                                        adbPathString(),
                                        QStringList() << "-s" << QString::fromStdString(g_runtimeCfg.adbSerial)
                                                      << "shell" << "getprop" << "sys.boot_completed");
            QObject::connect(proc, &QProcess::finished, proc,
                             [proc, guestPerfTimer, androidBootReady, v1QuickBootEnabled, &qmlAndroidControls, &app](
                                 int, QProcess::ExitStatus) {
                const QString booted = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
                proc->deleteLater();
                if (booted == QStringLiteral("1")) {
                    *androidBootReady = true;
                    guestPerfTimer->stop();
                    applyGuestPerformanceSettings();
                    installGuestSupportApps();
                    installChimeraLauncher();
                    applyGuestFirstBootSetup();
                    qmlAndroidControls.setBootReady(true);
                    if (v1QuickBootEnabled && shouldAutoSaveQuickBootSnapshot()) {
                        QTimer::singleShot(30000, &app, [&app]() {
                            saveQuickBootSnapshotAsync(&app);
                        });
                    }
                }
            });
        });
        guestPerfTimer->start();
    }

    // Synthetic real-path scroll injector (diagnostics-only, default OFF). Drives a
    // continuous vertical drag through the PRODUCTION input path
    // (InputBridge::onTouchPoint -> emulator gRPC sendTouch) WITHOUT moving the host
    // cursor (no SendInput/SetCursorPos), so a verifier can measure real input-path
    // render cadence. adb-swipe cannot represent the real path, and the host
    // mouse-drag probe could only measure it by grabbing the physical mouse (which
    // the user forbade). Gated by CHIMERA_SYNTHETIC_SCROLL (=1, or a tick-period in
    // ms 4..200); never runs in normal use.
    if (const QByteArray synthRaw = envValue("CHIMERA_SYNTHETIC_SCROLL");
        !synthRaw.isEmpty() && synthRaw != "0" &&
        synthRaw.toLower() != "false" && synthRaw.toLower() != "off") {
        bool okPeriod = false;
        int period = synthRaw.toInt(&okPeriod);
        if (!okPeriod || period < 4 || period > 200) period = 16;
        // Guest is enforced >= 1920x1080; center column + a mid vertical band is a
        // valid scroll region on any conforming guest.
        const int cx = 960;
        const int yTop = 270;
        const int yBottom = 860;
        const int step = 90;
        struct ScrollState { int y; int dir = -1; bool pressed = false; int gestureTicks = 0; };
        auto st = std::make_shared<ScrollState>();
        st->y = (yTop + yBottom) / 2;
        auto *synthTimer = new QTimer(&app);
        synthTimer->setTimerType(Qt::PreciseTimer);
        QObject::connect(synthTimer, &QTimer::timeout, [st, androidBootReady, cx, yTop, yBottom, step]() {
            if (!*androidBootReady) return; // wait for full boot before injecting
            auto &ib = chimera::input::InputBridge::instance();
            // Repeated fast flings: press, drag hard over ~0.7s, lift, repress reversed.
            // The LIFT with velocity is what triggers Android fling momentum (continuous
            // vsync-paced frames); a held drag instead parks at the list boundary and
            // produces ~0 unique frames. Measured real-path guest cadence ~56fps.
            if (!st->pressed) {
                ib.onTouchPoint(0, cx, st->y, true);
                st->pressed = true;
                st->gestureTicks = 0;
                return;
            }
            st->y += st->dir * step;
            if (st->y <= yTop) { st->y = yTop; st->dir = 1; }
            else if (st->y >= yBottom) { st->y = yBottom; st->dir = -1; }
            ib.onTouchPoint(0, cx, st->y, true);
            if (++st->gestureTicks >= 45) {          // ~0.7s gestures, then lift + repress
                ib.onTouchPoint(0, cx, st->y, false);
                st->pressed = false;
            }
        });
        synthTimer->start(period);
        qInfo().noquote() << QStringLiteral("[main] CHIMERA_SYNTHETIC_SCROLL active — real-path scroll injector period=%1ms (no host cursor movement)").arg(period);
    }

    // Log FPS every 5 seconds
    auto *perfTimer = new QTimer(&app);
    QObject::connect(perfTimer, &QTimer::timeout, [perfMonitor]() {
        const double effectiveFps = (std::min)(
            perfMonitor->fps(),
            (std::min)(perfMonitor->streamFps(), perfMonitor->renderFps()));
        qDebug() << QStringLiteral("[Perf] Guest: %1 FPS | Stream: %2 FPS | Render: %3 FPS | Avg: %4ms | Max: %5ms | Dup: %6 (%7%) | Dropped: %8 / %9")
                    .arg(perfMonitor->fps(), 0, 'f', 1)
                    .arg(perfMonitor->streamFps(), 0, 'f', 1)
                    .arg(perfMonitor->renderFps(), 0, 'f', 1)
                    .arg(perfMonitor->averageFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->maxFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->duplicateFrames())
                    .arg(perfMonitor->duplicateRate() * 100.0, 0, 'f', 0)
                    .arg(perfMonitor->droppedFrames())
                    .arg(perfMonitor->totalFrames());
        qDebug() << QStringLiteral("CHIMERA_PERF guest=%1 stream=%2 render=%3 effective=%4 dupPct=%5 dropped=%6 total=%7 avgMs=%8 maxMs=%9")
                    .arg(perfMonitor->fps(), 0, 'f', 1)
                    .arg(perfMonitor->streamFps(), 0, 'f', 1)
                    .arg(perfMonitor->renderFps(), 0, 'f', 1)
                    .arg(effectiveFps, 0, 'f', 1)
                    .arg(perfMonitor->duplicateRate() * 100.0, 0, 'f', 0)
                    .arg(perfMonitor->droppedFrames())
                    .arg(perfMonitor->totalFrames())
                    .arg(perfMonitor->averageFrameTimeMs(), 0, 'f', 1)
                    .arg(perfMonitor->maxFrameTimeMs(), 0, 'f', 1);
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

            auto *proc = new QProcess(bootTimer);
            startLowInterferenceProcess(proc,
                                        QString::fromStdString(g_adbPath.string()),
                                        QStringList() << "-s" << QString::fromStdString(g_runtimeCfg.adbSerial)
                                                      << "shell" << "getprop" << "sys.boot_completed");
            QObject::connect(proc, &QProcess::finished, proc,
                             [proc, adbCapture, bootTimer](int, QProcess::ExitStatus) {
                const QString booted = QString::fromLocal8Bit(proc->readAllStandardOutput()).trimmed();
                const int att = bootTimer->property("attempts").toInt();
                proc->deleteLater();
                if (booted == QStringLiteral("1") || att >= 45) {
                    bootTimer->stop();
                    if (adbCapture->start()) {
                        qDebug() << "ADB raw screen capture fallback started ("
                                 << adbCapture->intervalMs() << "ms target interval)";
                    }
                }
            });
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
                         [grpcCapture, grpcRetryTimer, grpcReceivedFrame, startAdbFallback,
                          androidBootReady, sharedMemoryCapture, shmemReceivedFrame,
                          sharedTextureCapture, sharedTextureReceivedFrame,
                          windowD3D11Capture, windowCaptureReceivedFrame,
                          requireEmuglSharedTexture, requireGfxstreamSharedTexture]() {
            if (*grpcReceivedFrame) {
                grpcRetryTimer->stop();
                return;
            }
            if (sharedMemoryCapture && *shmemReceivedFrame) {
                grpcRetryTimer->stop();
                if (grpcCapture->isRunning())
                    grpcCapture->stop();
                return;
            }
            if (sharedTextureCapture && *sharedTextureReceivedFrame) {
                // Shared texture is live: park the gRPC capture (the two capture
                // sources clear each other's state in GuestDisplay, so only one may
                // run at a time). The stall watchdog below revives it if the
                // gfxstream producer later stops publishing.
                grpcRetryTimer->stop();
                if (grpcCapture->isRunning())
                    grpcCapture->stop();
                return;
            }
            if (windowD3D11Capture && *windowCaptureReceivedFrame) {
                grpcRetryTimer->stop();
                if (grpcCapture->isRunning())
                    grpcCapture->stop();
                return;
            }
            if (!*androidBootReady) {
                return;
            }

            int attempts = grpcRetryTimer->property("attempts").toInt() + 1;
            grpcRetryTimer->setProperty("attempts", attempts);
            if (sharedTextureCapture && !*sharedTextureReceivedFrame) {
                if (requireEmuglSharedTexture || requireGfxstreamSharedTexture) {
                    if (attempts >= 15) {
                        qCritical() << "Required shared texture capture did not produce a frame; "
                                       "refusing raw gRPC/ADB fallback";
                        grpcRetryTimer->stop();
                        QCoreApplication::exit(3);
                    }
                    return;
                }
                if (attempts < 3)
                    return;
            }
            if (sharedMemoryCapture && !*shmemReceivedFrame && attempts < 3) {
                return;
            }
            if (windowD3D11Capture && !*windowCaptureReceivedFrame && attempts < 12) {
                return;
            }
            const bool encodedVideoCapture =
                grpcCapture->backendName() == QStringLiteral("ADB-H264");
            const int restartInterval = encodedVideoCapture ? 30 : 5;
            const int fallbackAttempt = encodedVideoCapture ? -1 : 45;
            const int stopAttempt = encodedVideoCapture ? 120 : 180;

            if (!grpcCapture->isRunning()) {
                qDebug() << "Starting" << grpcCapture->backendName() << "screen capture stream";
                grpcCapture->start();
            } else if (attempts > 1 && attempts % restartInterval == 0 &&
                       !grpcCapture->hasInFlight()) {
                // Only hard-restart a genuinely wedged/idle pipeline. While a
                // request is still in flight a slow 1080p readback is legitimately
                // progressing; a blind stop()/start() would abort it and reset the
                // stream, which is the total=0-under-load freeze. The request's own
                // transfer timeout + error/backoff path reclaims truly hung replies.
                qWarning() << grpcCapture->backendName()
                           << "capture has not produced a frame; restarting stream";
                grpcCapture->stop();
                grpcCapture->start();
            }
            if (attempts == fallbackAttempt && !*grpcReceivedFrame) {
                qWarning() << grpcCapture->backendName()
                           << "capture has not produced a frame yet; starting ADB fallback in parallel";
                startAdbFallback();
            }
            if (attempts >= stopAttempt && !*grpcReceivedFrame) {
                grpcRetryTimer->stop();
                grpcCapture->stop();
            }
        });
        grpcRetryTimer->start();
    } else if (!hcsBackendMode && !qemuBackendMode && streamCapture &&
               !sharedMemoryCapture && !sharedTextureCapture && !windowD3D11Capture &&
               allowRawCaptureFallback) {
        startAdbFallback();
    }

    return app.exec();
}
