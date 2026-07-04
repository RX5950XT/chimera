#include "VirtualMachine.h"
#include "ProcessLauncher.h"
#include <QDebug>
#include <windows.h>
#include <iphlpapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <thread>
#include <utility>

namespace chimera::instance {

namespace {

constexpr const char *kQuickBootSnapshotName = "chimera_quickboot";
constexpr int kMinimumGuestWidth = 1920;
constexpr int kMinimumGuestHeight = 1080;

std::pair<int, int> guestDisplaySizeForConfig(const VirtualMachineConfig &config) {
    return {
        (std::max)(config.width, kMinimumGuestWidth),
        (std::max)(config.height, kMinimumGuestHeight),
    };
}

std::string emulatorGpuMode(const std::string &renderer) {
    // Explicit override: lets us pin the emulator -gpu mode without editing config.
    if (const char *modeOverride = std::getenv("CHIMERA_GPU_MODE");
        modeOverride && *modeOverride) {
        return modeOverride;
    }
    // NOTE (Session 87): passing "-gpu angle_indirect" here does NOT route host
    // GLES to ANGLE — the prebuilt emulator.exe's gpuChoiceBasedOnGpuOptions
    // rejects "angle_indirect" as an invalid CLI option and downgrades to "auto"
    // -> SwiftShader GLES + lavapipe (software) Vulkan, which also kills the
    // direct-VK 60fps path. The working hardware-GLES route is INSIDE the
    // gfxstream DLL: emugl_config.cpp's headless fallback (currently
    // SwiftShader) is patched to choose angle_indirect there, gated by
    // CHIMERA_GFXSTREAM_HEADLESS_ANGLE=1 (default off). See
    // apply-chimera-gfxstream-patch.ps1 + scripts/verify-hardware-ui.ps1.
    if (renderer == "swiftshader" || renderer == "swiftshader_indirect") {
        return "swiftshader_indirect";
    }
    if (renderer == "host" || renderer == "d3d11" || renderer == "angle" ||
        renderer == "angle_indirect") {
        return "host";
    }
    return renderer.empty() ? "host" : renderer;
}

DWORD priorityClassForConfig(const std::string &priority) {
    if (priority == "idle") return IDLE_PRIORITY_CLASS;
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

bool processTreeHasVmProcess(DWORD rootPid) {
    if (rootPid == 0) return false;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return true;

    std::map<DWORD, std::vector<DWORD>> childrenByParent;
    std::map<DWORD, std::wstring> namesByPid;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            childrenByParent[entry.th32ParentProcessID].push_back(entry.th32ProcessID);
            namesByPid[entry.th32ProcessID] = entry.szExeFile;
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    std::vector<DWORD> stack{rootPid};
    while (!stack.empty()) {
        const DWORD pid = stack.back();
        stack.pop_back();
        const auto nameIt = namesByPid.find(pid);
        if (nameIt != namesByPid.end()) {
            const std::wstring &exe = nameIt->second;
            if (exe == L"emulator.exe" || exe == L"qemu-system-x86_64.exe" ||
                exe == L"qemu-system-x86_64-headless.exe") {
                return true;
            }
        }
        const auto childIt = childrenByParent.find(pid);
        if (childIt != childrenByParent.end()) {
            stack.insert(stack.end(), childIt->second.begin(), childIt->second.end());
        }
    }
    return false;
}

bool isVmProcessName(const std::wstring &exe) {
    return exe == L"emulator.exe" || exe == L"qemu-system-x86_64.exe" ||
           exe == L"qemu-system-x86_64-headless.exe";
}

uint16_t tcpPortFromNetworkDword(DWORD value) {
    return static_cast<uint16_t>(((value & 0xffu) << 8u) | ((value >> 8u) & 0xffu));
}

std::vector<DWORD> tcpPortOwners(uint16_t port) {
    ULONG size = 0;
    DWORD rc = GetExtendedTcpTable(nullptr, &size, FALSE, AF_INET,
                                   TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc != ERROR_INSUFFICIENT_BUFFER || size == 0) return {};

    std::vector<unsigned char> buffer(size);
    auto *table = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());
    rc = GetExtendedTcpTable(table, &size, FALSE, AF_INET,
                             TCP_TABLE_OWNER_PID_ALL, 0);
    if (rc != NO_ERROR) return {};

    std::vector<DWORD> owners;
    for (DWORD i = 0; i < table->dwNumEntries; ++i) {
        const auto &row = table->table[i];
        if (tcpPortFromNetworkDword(row.dwLocalPort) == port && row.dwOwningPid != 0) {
            owners.push_back(row.dwOwningPid);
        }
    }
    return owners;
}

DWORD vmRootForPid(DWORD pid) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;

    struct Info {
        DWORD parent = 0;
        std::wstring exe;
    };
    std::map<DWORD, Info> processes;
    PROCESSENTRY32W entry = {};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            processes[entry.th32ProcessID] = Info{entry.th32ParentProcessID, entry.szExeFile};
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);

    auto it = processes.find(pid);
    if (it == processes.end() || !isVmProcessName(it->second.exe)) return 0;

    DWORD root = pid;
    DWORD parent = it->second.parent;
    for (int depth = 0; depth < 8 && parent != 0; ++depth) {
        auto parentIt = processes.find(parent);
        if (parentIt == processes.end() || !isVmProcessName(parentIt->second.exe)) break;
        root = parent;
        parent = parentIt->second.parent;
    }
    return root;
}

void terminateStaleVmPortOwners(const VirtualMachineConfig &config) {
    std::set<DWORD> roots;
    const std::vector<int> ports = {config.qmpPort, config.adbPort, config.grpcPort};
    for (int port : ports) {
        if (port <= 0 || port > 65535) continue;
        for (DWORD owner : tcpPortOwners(static_cast<uint16_t>(port))) {
            const DWORD root = vmRootForPid(owner);
            if (root != 0) roots.insert(root);
        }
    }

    for (DWORD root : roots) {
        qWarning() << "Terminating stale Android Emulator process before start: pid" << root;
        ProcessLauncher::terminateProcessTreeById(root);
    }
    if (!roots.empty())
        std::this_thread::sleep_for(std::chrono::milliseconds(750));
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

std::filesystem::path sdkRootForConfig(const VirtualMachineConfig &config) {
    std::vector<std::filesystem::path> candidates;
    if (!config.emulatorPath.empty()) {
        candidates.push_back(config.emulatorPath.parent_path().parent_path());
    }
    if (!config.avdHome.empty()) {
        candidates.push_back(config.avdHome.parent_path() / "android-sdk");
    }
    if (const char *sdkRoot = std::getenv("ANDROID_SDK_ROOT")) {
        candidates.emplace_back(sdkRoot);
    }
    if (const char *sdkHome = std::getenv("ANDROID_HOME")) {
        candidates.emplace_back(sdkHome);
    }

    std::error_code ec;
    for (const auto &candidate : candidates) {
        if (std::filesystem::exists(candidate / "system-images", ec)) {
            return candidate;
        }
        ec.clear();
    }
    return config.emulatorPath.empty() ? std::filesystem::path{}
                                       : config.emulatorPath.parent_path().parent_path();
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

    const auto [guestWidth, guestHeight] = guestDisplaySizeForConfig(config);
    std::map<std::string, std::string> updates = {
        {"hw.cpu.ncore", std::to_string(std::max(1, config.cpus))},
        {"hw.gpu.enabled", "yes"},
        {"hw.gpu.mode", emulatorGpuMode(config.graphicsRenderer)},
        {"hw.initialOrientation", (guestWidth >= guestHeight) ? "landscape" : "portrait"},
        {"hw.keyboard", "yes"},
        {"hw.lcd.density", std::to_string(std::max(120, config.dpi))},
        {"hw.lcd.height", std::to_string(guestHeight)},
        {"hw.lcd.vsync", std::to_string(std::clamp(config.maxFps, 30, 240))},
        {"hw.lcd.width", std::to_string(guestWidth)},
        {"hw.ramSize", std::to_string(std::max(1024, config.ramMB)) + "M"},
        {"showDeviceFrame", "no"},
    };

    const std::filesystem::path sdkRoot = sdkRootForConfig(config);
    const std::filesystem::path playStoreImage =
        sdkRoot / "system-images" / "android-34" / "google_apis_playstore" / "x86_64";
    if (std::filesystem::exists(playStoreImage, ec)) {
        updates["PlayStore.enabled"] = "yes";
        updates["image.sysdir.1"] = R"(system-images\android-34\google_apis_playstore\x86_64\)";
        updates["tag.display"] = "Google Play";
        updates["tag.id"] = "google_apis_playstore";
    } else {
        qWarning() << "Google Play system image not found; launcher Play entry will stay disabled";
    }

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

std::filesystem::path adbPathForConfig(const VirtualMachineConfig &config) {
    if (config.emulatorPath.empty()) return {};
    return config.emulatorPath.parent_path().parent_path() / "platform-tools" / "adb.exe";
}

std::string adbSerialForConfig(const VirtualMachineConfig &config) {
    return "emulator-" + std::to_string(config.qmpPort);
}

bool runAdbEmuCommand(const VirtualMachineConfig &config,
                      const std::vector<std::string> &emuArgs,
                      int timeoutMs) {
    const auto adbPath = adbPathForConfig(config);
    std::error_code ec;
    if (adbPath.empty() || !std::filesystem::exists(adbPath, ec)) return false;

    std::vector<std::string> args = {"-s", adbSerialForConfig(config), "emu"};
    args.insert(args.end(), emuArgs.begin(), emuArgs.end());

    HANDLE hProc = ProcessLauncher::runAsync(adbPath.string(), args, nullptr, nullptr, true);
    if (!hProc) return false;
    const DWORD waitResult = WaitForSingleObject(hProc, static_cast<DWORD>(timeoutMs));
    if (waitResult == WAIT_OBJECT_0) {
        DWORD exitCode = 1;
        GetExitCodeProcess(hProc, &exitCode);
        CloseHandle(hProc);
        return exitCode == 0;
    }
    ProcessLauncher::terminate(hProc);
    WaitForSingleObject(hProc, 2000);
    CloseHandle(hProc);
    return false;
}

bool truthyEnv(const char *name) {
    const char *value = std::getenv(name);
    if (!value) return false;
    std::string text(value);
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !text.empty() && text != "0" && text != "false" && text != "off";
}

std::string visibleWindowSummary(const std::vector<std::string> &titles) {
    std::ostringstream out;
    for (size_t i = 0; i < titles.size() && i < 3; ++i) {
        if (i > 0) out << ", ";
        out << titles[i];
    }
    if (titles.size() > 3) out << ", ...";
    return out.str();
}

std::vector<std::string> waitForVisibleWindows(DWORD rootPid, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        auto titles = ProcessLauncher::visibleWindowTitlesInProcessTreeById(rootPid);
        if (!titles.empty()) return titles;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return {};
}

std::vector<std::string> buildEmulatorArgsForConfig(const VirtualMachineConfig &config) {
    const auto [guestWidth, guestHeight] = guestDisplaySizeForConfig(config);
    const bool classicRuntime = config.useClassicEmuglRuntime;
    const bool effectiveHeadless = config.headless || !config.allowVisibleEmulatorWindow;
    std::vector<std::string> args;
    args.push_back("-avd");
    args.push_back(config.avdName.empty() ? config.name : config.avdName);

    if (effectiveHeadless) {
        args.push_back("-no-window");
    }

    args.push_back("-accel");
    args.push_back("on");

    args.push_back("-gpu");
    args.push_back(emulatorGpuMode(config.graphicsRenderer));

    // Never pass "-systemui-renderer skiavk" and never try a runtime skiavk switch
    // (probed 2026-07-02 on the google_apis_playstore user image):
    //   - "-systemui-renderer skiavk" only reaches HWUI (init translates
    //     ro.boot.debug.hwui.renderer but NOT ro.boot.debug.renderengine.backend),
    //     so SurfaceFlinger stays on SkiaGL while apps render Vulkan — SF's
    //     SwiftShader-ES compositor cannot sample host-NVIDIA Vulkan surfaces and
    //     every app window turns blank (home/Settings screencaps uniform ~10KB).
    //   - the runtime alternative (adb setprop + "stop"/"start") needs root; the
    //     user build answers "Must be root", leaving the same half-applied blank.
    //
    // CHIMERA_GUEST_VULKAN=1 therefore only enables the guest Vulkan FEATURE so
    // apps/games that use Vulkan directly reach the host NVIDIA GPU (Session 91:
    // "Selecting Vulkan device: NVIDIA GeForce RTX 3070 Ti"; gl60 non-regression
    // with the flag Session 94). It is not a UI renderer switch.
    if (truthyEnv("CHIMERA_GUEST_VULKAN")) {
        args.push_back("-feature");
        args.push_back("Vulkan");
    }

    // System locale is NOT set here: on the non-root google_apis_playstore image,
    // "-prop persist.sys.locale=..." is silently dropped by the qemu-props service
    // (SELinux denies setting persist.* from that context — verified S105, prop
    // stays empty). The guest locale (zh-TW) is instead set once via Settings and
    // persists in userdata (/data survives cold boot). See scripts/debloat-guest.ps1
    // notes and docs; no launch flag can seed it on this image.

    args.push_back("-memory");
    args.push_back(std::to_string(config.ramMB));

    if (!classicRuntime) {
        args.push_back("-cores");
        args.push_back(std::to_string(config.cpus));
    }

    args.push_back("-no-skin");

    if (!classicRuntime) {
        args.push_back("-window-size");
        args.push_back(std::to_string(guestWidth) + "x" + std::to_string(guestHeight));
        args.push_back("-fixed-scale");

        args.push_back("-vsync-rate");
        args.push_back(std::to_string(std::clamp(config.maxFps, 30, 240)));
    }

    args.push_back("-netfast");

    if (config.quickBoot) {
        args.push_back("-snapshot");
        args.push_back(kQuickBootSnapshotName);
        if (!truthyEnv("CHIMERA_SAVE_QUICK_BOOT")) {
            args.push_back("-no-snapshot-save");
        }
    } else {
        args.push_back("-no-snapstorage");
        args.push_back("-no-snapshot");
        args.push_back("-no-snapshot-load");
        args.push_back("-no-snapshot-save");
    }
    args.push_back("-no-boot-anim");
    if (!config.enableAudio) {
        args.push_back("-no-audio");
    }
    if (!classicRuntime) {
        args.push_back("-no-metrics");
    }

    if (config.enableRoot) {
        args.push_back("-writable-system");
    }
    if (!classicRuntime) {
        args.push_back("-crash-report-mode");
        args.push_back("never");
    }

    args.push_back("-ports");
    args.push_back(std::to_string(config.qmpPort) + "," + std::to_string(config.adbPort));

    if (config.enableGrpc && !classicRuntime) {
        args.push_back("-grpc");
        args.push_back(std::to_string(config.grpcPort));
        // No -idle-grpc-timeout: the shared-texture display path generates zero
        // steady gRPC traffic, so the emulator's IdleInterceptor would shut the
        // VM down after 300s of user inactivity. Orphan protection is already
        // handled by the kill-on-close Job Object in ProcessLauncher.
    }

    std::vector<std::string> qemuArgs;
    if (config.enableVnc) {
        const int display = (config.vncPort > 5900) ? (config.vncPort - 5900) : 0;
        qemuArgs.push_back("-display");
        qemuArgs.push_back("vnc=127.0.0.1:" + std::to_string(display));
    }
    // virtio-snd-pci is not passed: the stock Android Emulator already provides
    // Goldfish audio when -no-audio is absent. Adding virtio-snd-pci via -qemu
    // conflicts with the built-in audio HAL and causes guest-side init failures.
    if (!qemuArgs.empty()) {
        args.push_back("-qemu");
        args.insert(args.end(), qemuArgs.begin(), qemuArgs.end());
    }
    return args;
}

} // namespace

VirtualMachine::VirtualMachine(const VirtualMachineConfig &config)
    : m_config(config) {}

VirtualMachine::~VirtualMachine() {
    const VMState state = m_state.load();
    if (state == VMState::Running || state == VMState::Paused) {
        stop();
    }
    joinExitMonitor();
}

bool VirtualMachine::create() {
    setState(VMState::Creating, false);
    std::filesystem::create_directories(m_config.dataDir);
    setState(VMState::Created, false);
    return true;
}

bool VirtualMachine::start() {
    const VMState initialState = m_state.load();
    if (initialState != VMState::Created && initialState != VMState::Stopped) return false;
    if (m_config.emulatorPath.empty()) return false;

    joinExitMonitor();
    setState(VMState::Starting, false);
    terminateStaleVmPortOwners(m_config);
    removeStaleAvdLocks(m_config);
    applyAvdHardwareConfig(m_config);

    auto onStdout = [](const std::string &line) {
        if (!line.empty()) qDebug() << "emulator:" << QString::fromStdString(line);
    };
    auto onStderr = [](const std::string &line) {
        if (!line.empty()) qWarning() << "emulator:" << QString::fromStdString(line);
    };
    const DWORD priorityClass = priorityClassForConfig(m_config.processPriority);
    const DWORD startupPriorityClass =
        priorityClass == NORMAL_PRIORITY_CLASS ? BELOW_NORMAL_PRIORITY_CLASS : IDLE_PRIORITY_CLASS;

    auto launch = [&](bool quickBoot) -> HANDLE {
        VirtualMachineConfig launchConfig = m_config;
        launchConfig.quickBoot = quickBoot;
        const bool forceHiddenWindow =
            launchConfig.headless ||
            !launchConfig.allowVisibleEmulatorWindow ||
            launchConfig.startHidden;
        return ProcessLauncher::runAsync(
            m_config.emulatorPath.string(),
            buildEmulatorArgsForConfig(launchConfig),
            onStdout,
            onStderr,
            forceHiddenWindow,
            startupPriorityClass
        );
    };

    HANDLE hProc = launch(m_config.quickBoot);
    if (!hProc) {
        setState(VMState::Error);
        return false;
    }
    DWORD rootPid = GetProcessId(hProc);
    if (rootPid == 0) {
        qCritical() << "Failed to obtain emulator root process id; error=" << GetLastError();
        ProcessLauncher::terminate(hProc);
        setState(VMState::Error);
        return false;
    }

    if (ProcessLauncher::waitForExit(hProc, 1500) >= 0) {
        if (m_config.quickBoot) {
            qWarning() << "Quick Boot launch exited early; retrying with full boot";
            hProc = launch(false);
            rootPid = hProc ? GetProcessId(hProc) : 0;
            if (!hProc || ProcessLauncher::waitForExit(hProc, 1500) >= 0) {
                setState(VMState::Error);
                return false;
            }
            if (rootPid == 0) {
                qCritical() << "Failed to obtain emulator root process id after full boot retry; error="
                            << GetLastError();
                ProcessLauncher::terminate(hProc);
                setState(VMState::Error);
                return false;
            }
        } else {
            setState(VMState::Error);
            return false;
        }
    }

    m_processHandle = hProc;
    const bool rejectVisibleWindows =
        m_config.headless || !m_config.allowVisibleEmulatorWindow || m_config.startHidden;
    if (rejectVisibleWindows) {
        const auto visibleWindows = waitForVisibleWindows(rootPid, 2500);
        if (!visibleWindows.empty()) {
            qCritical() << "Headless emulator exposed visible native window; terminating:"
                        << QString::fromStdString(visibleWindowSummary(visibleWindows));
            ProcessLauncher::terminate(hProc);
            ProcessLauncher::waitForExit(hProc, 5000);
            m_processHandle = nullptr;
            setState(VMState::Error);
            return false;
        }
    }
    ProcessLauncher::setProcessTreePriority(hProc, startupPriorityClass);
    std::thread([rootPid, startupPriorityClass, priorityClass, rejectVisibleWindows]() {
        for (int i = 0; i < 600; ++i) {
            if (rejectVisibleWindows) {
                const auto visibleWindows =
                    ProcessLauncher::visibleWindowTitlesInProcessTreeById(rootPid);
                if (!visibleWindows.empty()) {
                    qCritical() << "Headless emulator exposed visible native window; terminating:"
                                << QString::fromStdString(visibleWindowSummary(visibleWindows));
                    ProcessLauncher::terminateProcessTreeById(rootPid);
                    return;
                }
            }
            ProcessLauncher::setProcessTreePriorityById(rootPid, startupPriorityClass);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        for (int i = 0; i < 90; ++i) {
            ProcessLauncher::setProcessTreePriorityById(rootPid, priorityClass);
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
    }).detach();

    setState(VMState::Running);
    startExitMonitor(rootPid);
    return true;
}

bool VirtualMachine::stop() {
    setState(VMState::Stopping);
    // Claim the handle atomically: if the exit monitor already disposed it, we get
    // nullptr and skip; otherwise we own the close (via waitForExit below).
    if (void *claimed = InterlockedExchangePointer(&m_processHandle, nullptr)) {
        const HANDLE hProc = static_cast<HANDLE>(claimed);
        bool exited = false;
        if (m_config.quickBoot && truthyEnv("CHIMERA_SAVE_QUICK_BOOT")) {
            const bool saved = runAdbEmuCommand(
                m_config, {"avd", "snapshot", "save", kQuickBootSnapshotName}, 30000);
            if (!saved) {
                qWarning() << "Quick Boot snapshot save failed; falling back to emulator kill";
            }
            if (runAdbEmuCommand(m_config, {"kill"}, 5000)) {
                exited = ProcessLauncher::waitForExit(hProc, 15000) >= 0;
            }
        }
        if (!exited) {
            ProcessLauncher::terminate(hProc);
            ProcessLauncher::waitForExit(hProc, 10000);
        }
    }
    setState(VMState::Stopped);
    joinExitMonitor();
    return true;
}

bool VirtualMachine::pause() {
    if (!m_processHandle || m_state.load() != VMState::Running) return false;
    typedef LONG (NTAPI *NtSuspendProcess_t)(HANDLE);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn = ntdll ? reinterpret_cast<NtSuspendProcess_t>(GetProcAddress(ntdll, "NtSuspendProcess")) : nullptr;
    if (!fn || fn(static_cast<HANDLE>(m_processHandle)) != 0) return false;
    setState(VMState::Paused);
    return true;
}

bool VirtualMachine::resume() {
    if (!m_processHandle || m_state.load() != VMState::Paused) return false;
    typedef LONG (NTAPI *NtResumeProcess_t)(HANDLE);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    auto fn = ntdll ? reinterpret_cast<NtResumeProcess_t>(GetProcAddress(ntdll, "NtResumeProcess")) : nullptr;
    if (!fn || fn(static_cast<HANDLE>(m_processHandle)) != 0) return false;
    setState(VMState::Running);
    return true;
}

uint32_t VirtualMachine::processId() const {
#ifdef _WIN32
    return m_processHandle ? GetProcessId(static_cast<HANDLE>(m_processHandle)) : 0;
#else
    return 0;
#endif
}

void VirtualMachine::setState(VMState state, bool notify) {
    m_state.store(state);
    if (notify && m_callback) {
        m_callback(state);
    }
}

void VirtualMachine::joinExitMonitor() {
    if (m_exitMonitor.joinable()) {
        m_exitMonitor.join();
    }
}

void VirtualMachine::startExitMonitor(uint32_t rootPid) {
#ifdef _WIN32
    m_exitMonitor = std::thread([this, rootPid]() {
        int missingSamples = 0;
        while (true) {
            const VMState state = m_state.load();
            if (state != VMState::Running && state != VMState::Paused &&
                state != VMState::Starting) {
                return;
            }
            if (processTreeHasVmProcess(rootPid)) {
                missingSamples = 0;
            } else {
                ++missingSamples;
                if (missingSamples >= 4) {
                    qCritical() << "Emulator process tree exited unexpectedly; rootPid=" << rootPid;
                    // Atomically claim the handle so we close it exactly once even
                    // if stop() races us; whoever exchanges the non-null pointer owns
                    // the close. A bare null here both leaked the handle and could
                    // double-close against stop().
                    if (void *claimed = InterlockedExchangePointer(&m_processHandle, nullptr)) {
                        CloseHandle(static_cast<HANDLE>(claimed));
                    }
                    setState(VMState::Error);
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
#else
    Q_UNUSED(rootPid);
#endif
}

VMState VirtualMachine::state() const {
    const VMState state = m_state.load();
    if (state == VMState::Running && m_processHandle) {
        if (!ProcessLauncher::isRunning(static_cast<HANDLE>(m_processHandle))) {
            // Process exited unexpectedly
            const_cast<VirtualMachine*>(this)->setState(VMState::Error, false);
            return VMState::Error;
        }
    }
    return state;
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

std::vector<std::string> VirtualMachine::buildEmulatorArgs() const {
    return buildEmulatorArgsForConfig(m_config);
}

void VirtualMachine::setStateCallback(StateCallback cb) {
    m_callback = cb;
}

} // namespace chimera::instance
