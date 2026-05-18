#include "VirtualMachine.h"
#include "ProcessLauncher.h"
#include <QDebug>
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <thread>

namespace chimera::instance {

namespace {

std::string emulatorGpuMode(const std::string &renderer) {
    if (renderer == "swiftshader" || renderer == "swiftshader_indirect") {
        return "swiftshader_indirect";
    }
    if (renderer == "host" || renderer == "d3d11" || renderer == "angle") {
        return "host";
    }
    return renderer.empty() ? "host" : renderer;
}

DWORD priorityClassForConfig(const std::string &priority) {
    if (priority == "realtime") return REALTIME_PRIORITY_CLASS;
    if (priority == "high" || priority == "gaming") return HIGH_PRIORITY_CLASS;
    if (priority == "above_normal") return ABOVE_NORMAL_PRIORITY_CLASS;
    if (priority == "below_normal" || priority == "eco") return BELOW_NORMAL_PRIORITY_CLASS;
    return NORMAL_PRIORITY_CLASS;
}

bool hasRunningEmulatorProcess() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;

    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            const std::wstring exe(entry.szExeFile);
            if (exe == L"emulator.exe" || exe == L"qemu-system-x86_64.exe" ||
                exe == L"qemu-system-x86_64-headless.exe") {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

void removeStaleAvdLocks(const VirtualMachineConfig &config) {
    if (config.avdName.empty() || hasRunningEmulatorProcess()) return;

    std::vector<std::filesystem::path> candidates;
    if (!config.avdHome.empty()) {
        candidates.push_back(config.avdHome / (config.avdName + ".avd"));
    }

    const std::filesystem::path sdkDerivedAvdHome =
        config.emulatorPath.parent_path().parent_path().parent_path() / "android-avd";
    candidates.push_back(sdkDerivedAvdHome / (config.avdName + ".avd"));

    for (const auto &avdDir : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(avdDir, ec)) continue;
        for (const auto &entry : std::filesystem::directory_iterator(avdDir, ec)) {
            if (ec) break;
            if (entry.path().extension() == ".lock") {
                std::filesystem::remove_all(entry.path(), ec);
                if (!ec) {
                    qWarning() << "Removed stale AVD lock:" << QString::fromStdWString(entry.path().wstring());
                }
            }
        }
    }
}

std::filesystem::path avdConfigPath(const VirtualMachineConfig &config) {
    if (config.avdName.empty()) return {};
    if (!config.avdHome.empty()) {
        return config.avdHome / (config.avdName + ".avd") / "config.ini";
    }
    if (!config.emulatorPath.empty()) {
        return config.emulatorPath.parent_path().parent_path().parent_path() /
               "android-avd" / (config.avdName + ".avd") / "config.ini";
    }
    return {};
}

void upsertIniValue(std::vector<std::string> &lines, const std::string &key, const std::string &value) {
    const std::string prefix = key + " ";
    const std::string replacement = key + " = " + value;
    for (auto &line : lines) {
        if (line.rfind(prefix, 0) == 0 || line.rfind(key + "=", 0) == 0) {
            line = replacement;
            return;
        }
    }
    lines.push_back(replacement);
}

void applyAvdHardwareConfig(const VirtualMachineConfig &config) {
    const std::filesystem::path path = avdConfigPath(config);
    std::error_code ec;
    if (path.empty() || !std::filesystem::exists(path, ec)) return;

    std::ifstream in(path);
    if (!in.is_open()) return;

    std::vector<std::string> lines;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }

    const std::map<std::string, std::string> updates = {
        {"hw.cpu.ncore", std::to_string(std::max(1, config.cpus))},
        {"hw.gpu.enabled", "yes"},
        {"hw.gpu.mode", emulatorGpuMode(config.graphicsRenderer)},
        {"hw.keyboard", "yes"},
        {"hw.lcd.density", std::to_string(std::max(120, config.dpi))},
        {"hw.lcd.height", std::to_string(std::max(480, config.height))},
        {"hw.lcd.vsync", std::to_string(std::clamp(config.maxFps, 30, 240))},
        {"hw.lcd.width", std::to_string(std::max(640, config.width))},
        {"hw.ramSize", std::to_string(std::max(1024, config.ramMB)) + "M"},
        {"showDeviceFrame", "no"},
    };

    for (const auto &[key, value] : updates) {
        upsertIniValue(lines, key, value);
    }

    const std::filesystem::path tmp = path.string() + ".tmp";
    std::ofstream out(tmp, std::ios::trunc);
    if (!out.is_open()) return;
    for (const auto &updatedLine : lines) {
        out << updatedLine << '\n';
    }
    out.close();
    std::filesystem::copy_file(tmp, path, std::filesystem::copy_options::overwrite_existing, ec);
    std::filesystem::remove(tmp);
    if (!ec) {
        qDebug() << "AVD hardware config synced:" << QString::fromStdWString(path.wstring());
    }
}

} // namespace

VirtualMachine::VirtualMachine(const VirtualMachineConfig &config)
    : m_config(config) {}

VirtualMachine::~VirtualMachine() {
    if (m_state == VMState::Running || m_state == VMState::Paused) {
        stop();
    }
}

bool VirtualMachine::create() {
    m_state = VMState::Creating;
    std::filesystem::create_directories(m_config.dataDir);
    m_state = VMState::Created;
    return true;
}

bool VirtualMachine::start() {
    if (m_state != VMState::Created && m_state != VMState::Stopped) return false;
    if (m_config.emulatorPath.empty()) return false;

    m_state = VMState::Starting;
    removeStaleAvdLocks(m_config);
    applyAvdHardwareConfig(m_config);

    std::vector<std::string> args;
    args.push_back("-avd");
    args.push_back(m_config.avdName.empty() ? m_config.name : m_config.avdName);

    if (m_config.headless) {
        args.push_back("-no-window");
    }

    args.push_back("-accel");
    args.push_back("on");

    args.push_back("-gpu");
    args.push_back(emulatorGpuMode(m_config.graphicsRenderer));

    args.push_back("-systemui-renderer");
    args.push_back("skiavk");

    args.push_back("-memory");
    args.push_back(std::to_string(m_config.ramMB));

    args.push_back("-cores");
    args.push_back(std::to_string(m_config.cpus));

    args.push_back("-no-skin");
    args.push_back("-window-size");
    args.push_back(std::to_string(m_config.width) + "x" + std::to_string(m_config.height));
    args.push_back("-fixed-scale");

    args.push_back("-vsync-rate");
    args.push_back(std::to_string(std::clamp(m_config.maxFps, 30, 240)));

    args.push_back("-netfast");

    args.push_back("-no-snapshot");
    args.push_back("-no-boot-anim");
    args.push_back("-no-audio");
    args.push_back("-no-metrics");

    // Root mode: enable a writable /system partition (google_apis only).
    // After boot, call "adb root" to switch adbd to root user.
    if (m_config.enableRoot) {
        args.push_back("-writable-system");
    }
    args.push_back("-crash-report-mode");
    args.push_back("never");

    // Android Emulator console port IS the QMP interface.
    // -ports console,adb  →  console=QMP, adb=ADB daemon
    args.push_back("-ports");
    args.push_back(std::to_string(m_config.qmpPort) + "," + std::to_string(m_config.adbPort));

    if (m_config.enableGrpc) {
        args.push_back("-grpc");
        args.push_back(std::to_string(m_config.grpcPort));
        args.push_back("-idle-grpc-timeout");
        args.push_back("300");
    }

    // QEMU arg passthrough: VNC is disabled by default, but virtio-snd is kept
    // for the future guest audio bridge. Host audio output remains muted by -no-audio.
    args.push_back("-qemu");
    if (m_config.enableVnc) {
        const int display = (m_config.vncPort > 5900) ? (m_config.vncPort - 5900) : 0;
        args.push_back("-display");
        args.push_back("vnc=127.0.0.1:" + std::to_string(display));
    }
    args.push_back("-device");
    args.push_back("virtio-snd-pci");

    auto onStdout = [](const std::string &line) {
        if (!line.empty()) qDebug() << "emulator:" << QString::fromStdString(line);
    };
    auto onStderr = [](const std::string &line) {
        if (!line.empty()) qWarning() << "emulator:" << QString::fromStdString(line);
    };

    HANDLE hProc = ProcessLauncher::runAsync(
        m_config.emulatorPath.string(),
        args,
        onStdout,
        onStderr,
        /*startHidden=*/!m_config.headless
    );

    if (!hProc) {
        m_state = VMState::Error;
        if (m_callback) m_callback(m_state);
        return false;
    }

    if (ProcessLauncher::waitForExit(hProc, 1500) >= 0) {
        m_state = VMState::Error;
        if (m_callback) m_callback(m_state);
        return false;
    }

    m_processHandle = hProc;
    const DWORD priorityClass = priorityClassForConfig(m_config.processPriority);
    const DWORD rootPid = GetProcessId(hProc);
    ProcessLauncher::setProcessTreePriority(hProc, priorityClass);
    std::thread([rootPid, priorityClass]() {
        for (int i = 0; i < 12; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            ProcessLauncher::setProcessTreePriorityById(rootPid, priorityClass);
        }
    }).detach();

    m_state = VMState::Running;
    if (m_callback) m_callback(m_state);
    return true;
}

bool VirtualMachine::stop() {
    m_state = VMState::Stopping;
    if (m_processHandle) {
        ProcessLauncher::terminate(static_cast<HANDLE>(m_processHandle));
        ProcessLauncher::waitForExit(static_cast<HANDLE>(m_processHandle), 10000);
        m_processHandle = nullptr;
    }
    m_state = VMState::Stopped;
    if (m_callback) m_callback(m_state);
    return true;
}

bool VirtualMachine::pause() {
    // QEMU savevm / loadvm
    return false;
}

bool VirtualMachine::resume() {
    return false;
}

VMState VirtualMachine::state() const {
    if (m_state == VMState::Running && m_processHandle) {
        if (!ProcessLauncher::isRunning(static_cast<HANDLE>(m_processHandle))) {
            // Process exited unexpectedly
            const_cast<VirtualMachine*>(this)->m_state = VMState::Error;
        }
    }
    return m_state;
}

std::vector<std::string> VirtualMachine::buildQemuArgs() const {
    std::vector<std::string> args;
    args.push_back("-machine"); args.push_back("q35");
    args.push_back("-m"); args.push_back(std::to_string(m_config.ramMB));
    args.push_back("-smp"); args.push_back(std::to_string(m_config.cpus));
    args.push_back("-cpu"); args.push_back("qemu64");
    args.push_back("-accel"); args.push_back("whpx");
    args.push_back("-vga"); args.push_back("virtio");
    args.push_back("-device"); args.push_back("virtio-net-pci,netdev=net0");
    args.push_back("-netdev"); args.push_back("user,id=net0,hostfwd=tcp::5555-:5555");
    if (!m_config.systemImage.empty()) {
        args.push_back("-drive"); args.push_back("file=" + m_config.systemImage.string() + ",format=raw,if=virtio");
    }
    return args;
}

void VirtualMachine::setStateCallback(StateCallback cb) {
    m_callback = cb;
}

} // namespace chimera::instance
