#include "InstanceManager.h"
#include "VirtualMachine.h"
#include "DeviceSpoofer.h"
#include <QDebug>
#include <QString>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <set>
#include <nlohmann/json.hpp>

namespace chimera::instance {

class InstanceManager::Impl {
public:
    std::vector<std::unique_ptr<VirtualMachine>> vms;
    std::vector<InstanceConfig> savedConfigs;
    std::filesystem::path instancesDir;
    std::filesystem::path instancesJsonPath;
    InstanceManager::StateCallback stateCallback;
};

static bool isValidInstanceName(const std::string &name) {
    if (name.empty()) return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return std::isalnum(ch) || ch == '_' || ch == '-' || ch == '.';
    });
}

static std::string envValue(const char *name) {
    const char *value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

static bool truthyEnvValue(const char *name) {
    std::string value = envValue(name);
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return !value.empty() && value != "0" && value != "false" && value != "off";
}

static bool unsafeVisibleWindowDiagnosticsAllowed() {
    return truthyEnvValue("CHIMERA_ALLOW_UNSAFE_VISIBLE_EMULATOR_WINDOW") &&
           truthyEnvValue("CHIMERA_VISIBLE_EMULATOR_DIAGNOSTICS_SESSION");
}

static InstanceConfig normalizedInstanceConfig(InstanceConfig config) {
    config.width = (std::max)(config.width, 1920);
    config.height = (std::max)(config.height, 1080);
    if (config.allowVisibleEmulatorWindow && !unsafeVisibleWindowDiagnosticsAllowed()) {
        qWarning() << "Visible Android Emulator window requested but blocked; "
                      "use explicit unsafe diagnostics flags in this Chimera launch";
        config.allowVisibleEmulatorWindow = false;
    }
    if (!config.allowVisibleEmulatorWindow) {
        config.headless = true;
    }
    if (config.processPriority.empty()) {
        config.processPriority = "below_normal";
    } else if (config.processPriority == "high" ||
               config.processPriority == "gaming" ||
               config.processPriority == "above_normal" ||
               config.processPriority == "realtime") {
        config.processPriority = "normal";
    }
    return config;
}

static std::filesystem::path getProjectRoot() {
    auto path = std::filesystem::current_path();
    for (int i = 0; i < 5; ++i) {
        if (std::filesystem::exists(path / "configs" / "android_sdk.json")) {
            return path;
        }
        path = path.parent_path();
    }
    return std::filesystem::current_path();
}

static nlohmann::json loadAndroidSdkConfig() {
    auto root = getProjectRoot();
    auto path = root / "configs" / "android_sdk.json";
    nlohmann::json j;
    try {
        if (std::filesystem::exists(path)) {
            std::ifstream f(path);
            if (f.is_open()) f >> j;
        }
    } catch (...) {
        return nlohmann::json::object();
    }
    return j;
}

static void prependPathEnv(const std::filesystem::path &path) {
    if (path.empty() || !std::filesystem::exists(path)) return;
    const std::string current = envValue("PATH");
    const std::string entry = path.string();
    if (current.find(entry) != std::string::npos) return;
    const std::string updated = entry + (current.empty() ? "" : ";" + current);
    _putenv_s("PATH", updated.c_str());
}

static void prepareEmulatorRuntimePath(const std::filesystem::path &emulatorPath) {
    if (emulatorPath.empty()) return;
    const auto dir = emulatorPath.parent_path();
    prependPathEnv(dir / "lib64");
    prependPathEnv(dir / "lib");
    prependPathEnv(dir);
}

static bool existsAny(const std::vector<std::filesystem::path> &paths) {
    std::error_code ec;
    return std::any_of(paths.begin(), paths.end(), [&ec](const auto &path) {
        ec.clear();
        return std::filesystem::exists(path, ec);
    });
}

static bool fileExists(const std::filesystem::path &path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

static std::filesystem::path firstExistingPath(const std::vector<std::filesystem::path> &paths) {
    for (const auto &path : paths) {
        if (fileExists(path)) return path;
    }
    return {};
}

static bool hasCompleteEmuglRuntimeDllSet(const std::filesystem::path &dir) {
    if (dir.empty()) return false;
    return fileExists(dir / "lib64OpenglRender.dll") &&
           fileExists(dir / "lib64EGL_translator.dll") &&
           fileExists(dir / "lib64GLES_CM_translator.dll") &&
           fileExists(dir / "lib64GLES_V2_translator.dll");
}

static std::string readSourceProperty(const std::filesystem::path &path, const std::string &key) {
    std::ifstream input(path);
    if (!input.is_open()) return {};
    std::string line;
    const std::string prefix = key + "=";
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) == 0) {
            return line.substr(prefix.size());
        }
    }
    return {};
}

static bool hasValidSharedTextureManifest(
    const std::vector<std::filesystem::path> &paths,
    const char *producerName,
    const std::string &baseEmulatorBuildId,
    bool *matchingGfxstreamBuildId = nullptr,
    bool *mismatchedGfxstreamBuildId = nullptr) {
    if (matchingGfxstreamBuildId) *matchingGfxstreamBuildId = false;
    if (mismatchedGfxstreamBuildId) *mismatchedGfxstreamBuildId = false;
    for (const auto &path : paths) {
        if (!fileExists(path)) continue;
        try {
            std::ifstream f(path);
            nlohmann::json manifest;
            f >> manifest;
            const bool identityOk =
                manifest.value("producer", "") == producerName &&
                manifest.value("transport", "") == "D3D11SharedTexture";
            const bool floorOk =
                manifest.value("minWidth", 0) >= 1920 &&
                manifest.value("minHeight", 0) >= 1080 &&
                manifest.value("targetFps", 0) >= 60;
            bool runtimePathOk = true;
            if (std::string(producerName) == "ChimeraGfxstreamSharedTextureBridge") {
                const bool runtimePathBaseOk =
                    manifest.value("renderPath", "") == "VulkanDisplayVkPost" &&
                    manifest.value("abi", "") == "sdk-emulator-36";
                runtimePathOk =
                    runtimePathBaseOk;
                const std::string snapBuildId = manifest.value("gfxstreamSourceSnapBuildId", "");
                const std::string manifestBaseBuildId = manifest.value("baseEmulatorBuildId", "");
                // R&D bypass: CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK=1 allows testing
                // mismatched gfxstream source vs emulator build IDs. Empirically tested:
                // sdk-release build 13278158 crashes in emulator 15261927 during Vulkan init.
                const bool buildIdBypass =
                    std::getenv("CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK") != nullptr &&
                    std::string(std::getenv("CHIMERA_GFXSTREAM_SKIP_BUILD_ID_CHECK")) == "1";
                const bool buildIdOk =
                    buildIdBypass ||
                    (!baseEmulatorBuildId.empty() &&
                     !snapBuildId.empty() &&
                     snapBuildId == baseEmulatorBuildId &&
                     (manifestBaseBuildId.empty() || manifestBaseBuildId == baseEmulatorBuildId));
                if (matchingGfxstreamBuildId) *matchingGfxstreamBuildId = buildIdOk;
                if (!buildIdOk && identityOk && floorOk && runtimePathBaseOk &&
                    mismatchedGfxstreamBuildId) {
                    *mismatchedGfxstreamBuildId = true;
                }
                runtimePathOk = runtimePathOk && buildIdOk;
            }
            if (identityOk && floorOk && runtimePathOk) return true;
        } catch (...) {
            return false;
        }
    }
    return false;
}

static bool fileContainsMarker(const std::filesystem::path &path, const std::string &marker) {
    if (path.empty() || marker.empty()) return false;
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) return false;

    std::string window;
    window.reserve(marker.size() * 2);
    char buffer[4096] = {};
    while (input.read(buffer, sizeof(buffer)) || input.gcount() > 0) {
        window.append(buffer, static_cast<size_t>(input.gcount()));
        if (window.find(marker) != std::string::npos) return true;
        if (window.size() > marker.size()) {
            window.erase(0, window.size() - marker.size());
        }
    }
    return false;
}

static bool emuglSharedTextureRequested() {
    return truthyEnvValue("CHIMERA_ENABLE_EMUGL_SHARED_TEXTURE") ||
           truthyEnvValue("CHIMERA_EMUGL_SHARED_TEXTURE_REQUESTED") ||
           !envValue("CHIMERA_EMUGL_D3D11_TEXTURE_METADATA").empty();
}

static bool gfxstreamSharedTextureRequested() {
    return truthyEnvValue("CHIMERA_ENABLE_GFXSTREAM_SHARED_TEXTURE") ||
           truthyEnvValue("CHIMERA_GFXSTREAM_SHARED_TEXTURE_REQUESTED") ||
           !envValue("CHIMERA_GFXSTREAM_D3D11_TEXTURE_METADATA").empty();
}

static void publishRuntimeProbeEnv(const EmulatorRuntimeCapabilities &caps) {
    _putenv_s("CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_READY",
              caps.supportsChimeraEmuglSharedTexture ? "1" : "0");
    _putenv_s("CHIMERA_EMUGL_SHARED_TEXTURE_RUNTIME_STATUS", caps.status.c_str());
    _putenv_s("CHIMERA_GFXSTREAM_SHARED_TEXTURE_RUNTIME_READY",
              caps.supportsChimeraGfxstreamSharedTexture ? "1" : "0");
    _putenv_s("CHIMERA_GFXSTREAM_SHARED_TEXTURE_RUNTIME_STATUS", caps.status.c_str());
}

InstanceManager::InstanceManager() : d(std::make_unique<Impl>()) {
    auto root = getProjectRoot();
    d->instancesDir = root / "instances";
    d->instancesJsonPath = root / "configs" / "instances.json";
    std::filesystem::create_directories(d->instancesDir);
    loadInstances();
}

InstanceManager::~InstanceManager() {
    saveInstances();
}

void InstanceManager::loadInstances() {
    if (!std::filesystem::exists(d->instancesJsonPath)) return;
    std::ifstream f(d->instancesJsonPath);
    if (!f.is_open()) return;
    try {
        nlohmann::json j;
        f >> j;
        d->savedConfigs.clear();
        for (auto &item : j["instances"]) {
            InstanceConfig cfg;
            cfg.name = item.value("name", "");
            cfg.cpus = item.value("cpus", 2);
            cfg.ramMB = item.value("ramMB", 2048);
            cfg.width = item.value("width", 1920);
            cfg.height = item.value("height", 1080);
            cfg.dpi = item.value("dpi", 320);
            cfg.graphicsEngine = item.value("graphicsEngine", "angle");
            cfg.graphicsRenderer = item.value("graphicsRenderer", "host");
            cfg.maxFps = item.value("maxFps", 60);
            cfg.enableVsync = item.value("enableVsync", false);
            cfg.enableRoot = item.value("enableRoot", false);
            cfg.enableAudio = item.value("enableAudio", false);
            cfg.quickBoot = item.value("quickBoot", false);
            cfg.deviceProfile = item.value("deviceProfile", "");
            cfg.headless = item.value("headless", true);
            cfg.processPriority = item.value("processPriority", "below_normal");
            cfg.qmpPort = item.value("qmpPort", 5554);
            cfg.dataDir  = item.value("dataDir", "");
            cfg.gridRow  = item.value("gridRow", 0);
            cfg.gridCol  = item.value("gridCol", 0);
            d->savedConfigs.push_back(normalizedInstanceConfig(cfg));
        }
    } catch (...) {
        // Ignore corrupted JSON
    }
}

void InstanceManager::saveInstances() const {
    nlohmann::json j;
    nlohmann::json arr = nlohmann::json::array();
    // Merge saved configs with live VM configs
    std::set<std::string> seen;
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name.empty() || seen.count(cfg.name)) continue;
        const InstanceConfig normalized = normalizedInstanceConfig(cfg);
        seen.insert(normalized.name);
        nlohmann::json item;
        item["name"] = normalized.name;
        item["cpus"] = normalized.cpus;
        item["ramMB"] = normalized.ramMB;
        item["width"] = normalized.width;
        item["height"] = normalized.height;
        item["dpi"] = normalized.dpi;
        item["graphicsEngine"] = normalized.graphicsEngine;
        item["graphicsRenderer"] = normalized.graphicsRenderer;
        item["maxFps"] = normalized.maxFps;
        item["enableVsync"] = normalized.enableVsync;
        item["enableRoot"] = normalized.enableRoot;
        item["enableAudio"] = normalized.enableAudio;
        item["quickBoot"] = normalized.quickBoot;
        item["deviceProfile"] = normalized.deviceProfile;
        item["headless"] = normalized.headless;
        item["processPriority"] = normalized.processPriority;
        item["qmpPort"] = normalized.qmpPort;
        item["dataDir"]  = normalized.dataDir.string();
        item["gridRow"]  = normalized.gridRow;
        item["gridCol"]  = normalized.gridCol;
        arr.push_back(item);
    }
    for (auto &vm : d->vms) {
        auto &c = vm->config();
        if (c.name.empty() || seen.count(c.name)) continue;
        seen.insert(c.name);
        const auto normalized = normalizedInstanceConfig({
            c.name,
            c.cpus,
            c.ramMB,
            c.width,
            c.height,
            c.dpi,
            c.graphicsEngine,
            c.graphicsRenderer,
            c.maxFps,
            c.enableVsync,
            c.enableRoot,
            c.enableAudio,
            c.quickBoot,
            c.headless,
            c.allowVisibleEmulatorWindow,
            c.deviceProfile,
            c.processPriority,
            c.qmpPort,
            c.dataDir,
        });
        nlohmann::json item;
        item["name"] = normalized.name;
        item["cpus"] = normalized.cpus;
        item["ramMB"] = normalized.ramMB;
        item["width"] = normalized.width;
        item["height"] = normalized.height;
        item["dpi"] = normalized.dpi;
        item["graphicsEngine"] = normalized.graphicsEngine;
        item["graphicsRenderer"] = normalized.graphicsRenderer;
        item["maxFps"] = normalized.maxFps;
        item["enableVsync"] = normalized.enableVsync;
        item["enableRoot"] = normalized.enableRoot;
        item["enableAudio"] = normalized.enableAudio;
        item["quickBoot"] = normalized.quickBoot;
        item["deviceProfile"] = normalized.deviceProfile;
        item["headless"] = normalized.headless;
        item["processPriority"] = normalized.processPriority;
        item["qmpPort"] = normalized.qmpPort;
        item["dataDir"] = normalized.dataDir.string();
        arr.push_back(item);
    }
    j["instances"] = arr;
    std::ofstream f(d->instancesJsonPath);
    if (f.is_open()) f << j.dump(2);
}

InstanceManager &InstanceManager::instance() {
    static InstanceManager inst;
    return inst;
}

EmulatorRuntimeCapabilities InstanceManager::probeEmulatorRuntime(
    const std::filesystem::path &emulatorPath) {
    EmulatorRuntimeCapabilities caps;
    if (emulatorPath.empty()) {
        caps.status = "missing emulator path";
        return caps;
    }

    caps.runtimeDir = emulatorPath.parent_path();
    const auto lib64 = caps.runtimeDir / "lib64";
    const auto lib = caps.runtimeDir / "lib";
    const std::string baseEmulatorBuildId =
        readSourceProperty(caps.runtimeDir / "source.properties", "Pkg.BuildId");

    caps.hasLegacyOpenglRender = existsAny({
        lib64 / "lib64OpenglRender.dll",
        lib64 / "libOpenglRender.dll",
        lib / "libOpenglRender.dll",
    });
    const std::vector<std::filesystem::path> gfxstreamCandidates = {
        lib64 / "libgfxstream_backend.dll",
        lib / "libgfxstream_backend.dll",
    };
    const auto gfxstreamBackend = firstExistingPath(gfxstreamCandidates);
    caps.hasGfxstreamBackend = !gfxstreamBackend.empty();
    caps.hasChimeraGfxstreamBridgeMarker =
        fileContainsMarker(gfxstreamBackend, "ChimeraGfxstreamSharedTextureBridge") ||
        fileContainsMarker(gfxstreamBackend, "ChimeraGfxstreamVulkanSharedTextureBridge");
    caps.hasChimeraShmemPublisher =
        fileContainsMarker(gfxstreamBackend, "ChimeraShmemFramePublisher");
    caps.hasCompatibleGfxstreamAbi = fileContainsMarker(
        gfxstreamBackend, "gfxstream_backend_set_screen_background");
    caps.hasSdkGfxstreamRuntimeImports =
        fileContainsMarker(gfxstreamBackend, "libandroid-emu-agents.dll") &&
        fileContainsMarker(gfxstreamBackend, "libandroid-emu-protos.dll") &&
        fileContainsMarker(gfxstreamBackend, "libandroid-emu-metrics.dll");
    const std::vector<std::filesystem::path> emuglManifestCandidates = {
        caps.runtimeDir / "chimera-emugl-shared-texture.json",
        lib64 / "chimera-emugl-shared-texture.json",
        lib / "chimera-emugl-shared-texture.json",
    };
    const std::vector<std::filesystem::path> gfxstreamManifestCandidates = {
        caps.runtimeDir / "chimera-gfxstream-shared-texture.json",
        lib64 / "chimera-gfxstream-shared-texture.json",
        lib / "chimera-gfxstream-shared-texture.json",
    };
    caps.hasChimeraSharedTextureManifest = hasValidSharedTextureManifest(
        emuglManifestCandidates, "ChimeraSharedTextureBridge", baseEmulatorBuildId);
    caps.hasChimeraGfxstreamSharedTextureManifest = hasValidSharedTextureManifest(
        gfxstreamManifestCandidates,
        "ChimeraGfxstreamSharedTextureBridge",
        baseEmulatorBuildId,
        &caps.hasMatchingGfxstreamBuildId,
        &caps.hasMismatchedGfxstreamBuildId);
    caps.hasRequiredEmuglRuntimeDlls =
        hasCompleteEmuglRuntimeDllSet(lib64) || hasCompleteEmuglRuntimeDllSet(lib);
    caps.supportsChimeraEmuglSharedTexture =
        caps.hasRequiredEmuglRuntimeDlls && caps.hasChimeraSharedTextureManifest;
    caps.supportsChimeraGfxstreamSharedTexture =
        caps.hasGfxstreamBackend &&
        caps.hasChimeraGfxstreamBridgeMarker &&
        caps.hasCompatibleGfxstreamAbi &&
        caps.hasSdkGfxstreamRuntimeImports &&
        caps.hasChimeraGfxstreamSharedTextureManifest;

    if (caps.supportsChimeraGfxstreamSharedTexture) {
        caps.status = "modified gfxstream shared texture runtime detected";
    } else if (caps.supportsChimeraEmuglSharedTexture) {
        caps.status = "modified EmuGL shared texture runtime detected";
    } else if (caps.hasChimeraShmemPublisher) {
        caps.status = "chimera shmem frame publisher detected (CPU readback transport)";
    } else if (caps.hasGfxstreamBackend && !caps.hasChimeraGfxstreamBridgeMarker) {
        caps.status = "stock gfxstream runtime; Chimera gfxstream bridge marker is missing";
    } else if (caps.hasGfxstreamBackend && !caps.hasCompatibleGfxstreamAbi) {
        caps.status = "incompatible gfxstream runtime ABI; required screen background export is missing";
    } else if (caps.hasGfxstreamBackend && !caps.hasSdkGfxstreamRuntimeImports) {
        caps.status = "incompatible gfxstream runtime ABI; SDK runtime imports are missing";
    } else if (caps.hasGfxstreamBackend && caps.hasMismatchedGfxstreamBuildId) {
        caps.status = "incompatible gfxstream runtime ABI; source snapshot build id does not match emulator build id";
    } else if (caps.hasGfxstreamBackend && !caps.hasChimeraGfxstreamSharedTextureManifest) {
        caps.status = "stock gfxstream runtime; Chimera gfxstream bridge will not load";
    } else if (caps.hasLegacyOpenglRender && !caps.hasRequiredEmuglRuntimeDlls) {
        caps.status = "legacy EmuGL runtime is missing required translator DLLs";
    } else if (caps.hasLegacyOpenglRender && !caps.hasChimeraSharedTextureManifest) {
        caps.status = "legacy EmuGL runtime found, but Chimera shared texture manifest is missing or invalid";
    } else {
        caps.status = "no compatible Chimera shared texture runtime detected";
    }
    return caps;
}

std::vector<std::string> InstanceManager::listInstances() const {
    std::vector<std::string> names;
    std::set<std::string> seen;
    for (auto &cfg : d->savedConfigs) {
        if (!cfg.name.empty() && !seen.count(cfg.name)) {
            names.push_back(cfg.name);
            seen.insert(cfg.name);
        }
    }
    for (auto &vm : d->vms) {
        const auto &name = vm->config().name;
        if (!name.empty() && !seen.count(name)) {
            names.push_back(name);
            seen.insert(name);
        }
    }
    return names;
}

bool InstanceManager::createInstance(const InstanceConfig &config) {
    const InstanceConfig normalizedConfig = normalizedInstanceConfig(config);
    if (!isValidInstanceName(normalizedConfig.name)) return false;
    for (auto &vm : d->vms) {
        if (vm->config().name == normalizedConfig.name) return false;
    }

    VirtualMachineConfig vmConfig;
    vmConfig.name = normalizedConfig.name;
    vmConfig.cpus = normalizedConfig.cpus;
    vmConfig.ramMB = normalizedConfig.ramMB;
    vmConfig.width = normalizedConfig.width;
    vmConfig.height = normalizedConfig.height;
    vmConfig.dpi = normalizedConfig.dpi;
    vmConfig.graphicsEngine = normalizedConfig.graphicsEngine;
    vmConfig.graphicsRenderer = normalizedConfig.graphicsRenderer;
    vmConfig.maxFps = normalizedConfig.maxFps;
    vmConfig.enableVsync = normalizedConfig.enableVsync;
    vmConfig.headless = normalizedConfig.headless;
    vmConfig.allowVisibleEmulatorWindow = normalizedConfig.allowVisibleEmulatorWindow;
    vmConfig.startHidden = !normalizedConfig.allowVisibleEmulatorWindow;
    vmConfig.enableRoot = normalizedConfig.enableRoot;
    vmConfig.enableAudio = normalizedConfig.enableAudio;
    vmConfig.quickBoot = normalizedConfig.quickBoot;
    vmConfig.processPriority = normalizedConfig.processPriority;
    vmConfig.qmpPort = normalizedConfig.qmpPort;
    if (const auto overridePort = envValue("CHIMERA_EMULATOR_CONSOLE_PORT"); !overridePort.empty()) {
        try {
            vmConfig.qmpPort = std::stoi(overridePort);
        } catch (...) {
            qWarning() << "Ignoring invalid CHIMERA_EMULATOR_CONSOLE_PORT:" << QString::fromStdString(overridePort);
        }
    }
    vmConfig.adbPort = vmConfig.qmpPort + 1;
    vmConfig.grpcPort = 8554 + ((vmConfig.qmpPort - 5554) / 2) * 2;
    vmConfig.deviceProfile = normalizedConfig.deviceProfile;
    vmConfig.dataDir = normalizedConfig.dataDir.empty()
        ? (d->instancesDir / normalizedConfig.name)
        : normalizedConfig.dataDir;

    // Apply device spoofing if profile specified
    if (!normalizedConfig.deviceProfile.empty()) {
        auto profiles = DeviceSpoofer::getBuiltinProfiles();
        for (auto &p : profiles) {
            if (p.name == normalizedConfig.deviceProfile) {
                DeviceSpoofer::instance().applyProfile(p, normalizedConfig.name);
                break;
            }
        }
    }

    auto sdkCfg = loadAndroidSdkConfig();
    const std::string emulatorOverride = envValue("CHIMERA_EMULATOR_PATH");
    if (!emulatorOverride.empty()) {
        vmConfig.emulatorPath = emulatorOverride;
    } else if (sdkCfg.contains("emulator")) {
        vmConfig.emulatorPath = sdkCfg["emulator"].get<std::string>();
    }
    prepareEmulatorRuntimePath(vmConfig.emulatorPath);
    // Auto-enable shmem transport when the custom gfxstream runtime includes the
    // CPU readback → Win32 named shmem publisher, and no explicit shmem name is set.
    if (!vmConfig.emulatorPath.empty() && envValue("CHIMERA_SHMEM_FRAME_NAME").empty()) {
        const auto probeCaps = InstanceManager::probeEmulatorRuntime(vmConfig.emulatorPath);
        if (probeCaps.hasChimeraShmemPublisher) {
            const std::string shmemName  = "chimera_shmem_" + vmConfig.name;
            const std::string shmemEvent = "chimera_shmem_event_" + vmConfig.name;
            _putenv_s("CHIMERA_SHMEM_FRAME_NAME",  shmemName.c_str());
            _putenv_s("CHIMERA_SHMEM_FRAME_EVENT", shmemEvent.c_str());
            qDebug() << "Chimera shmem transport auto-enabled:"
                     << QString::fromStdString(shmemName);
        }
    }
    if (gfxstreamSharedTextureRequested() || emuglSharedTextureRequested()) {
        const auto caps = InstanceManager::probeEmulatorRuntime(vmConfig.emulatorPath);
        publishRuntimeProbeEnv(caps);
        if (gfxstreamSharedTextureRequested() && caps.supportsChimeraGfxstreamSharedTexture) {
            // Auto-set D3D11 metadata name so the headless bridge can activate without manual env
            if (envValue("CHIMERA_D3D11_TEXTURE_METADATA").empty()) {
                const std::string metaName = "chimera_gfxstream_d3d11_meta_" + vmConfig.name;
                const std::string evtName  = "chimera_gfxstream_d3d11_evt_" + vmConfig.name;
                _putenv_s("CHIMERA_D3D11_TEXTURE_METADATA", metaName.c_str());
                _putenv_s("CHIMERA_D3D11_TEXTURE_EVENT",    evtName.c_str());
                qDebug() << "Chimera gfxstream D3D11 texture metadata auto-set:"
                         << QString::fromStdString(metaName);
            }
            qDebug() << "Chimera gfxstream shared texture runtime ready:"
                     << QString::fromStdString(caps.status);
        } else if (emuglSharedTextureRequested() && caps.supportsChimeraEmuglSharedTexture) {
            vmConfig.useClassicEmuglRuntime = true;
            vmConfig.enableGrpc = false;
            qDebug() << "Chimera EmuGL shared texture runtime ready:"
                     << QString::fromStdString(caps.status);
        } else {
            qWarning() << "Chimera shared texture runtime requested but unavailable:"
                       << QString::fromStdString(caps.status);
            if (truthyEnvValue("CHIMERA_REQUIRE_GFXSTREAM_SHARED_TEXTURE") ||
                truthyEnvValue("CHIMERA_REQUIRE_EMUGL_SHARED_TEXTURE")) {
                return false;
            }
        }
    }
    if (sdkCfg.contains("avd_name")) {
        vmConfig.avdName = sdkCfg["avd_name"].get<std::string>();
    }
    if (sdkCfg.contains("avd_home")) {
        vmConfig.avdHome = sdkCfg["avd_home"].get<std::string>();
    }

    auto vm = std::make_unique<VirtualMachine>(vmConfig);
    if (!vm->create()) return false;
    if (d->stateCallback) {
        vm->setStateCallback([this, name = vm->config().name](VMState s) {
            d->stateCallback(name, s);
        });
    }
    d->vms.push_back(std::move(vm));

    // Update saved config
    auto it = std::remove_if(d->savedConfigs.begin(), d->savedConfigs.end(),
                             [&](const auto &c) { return c.name == normalizedConfig.name; });
    d->savedConfigs.erase(it, d->savedConfigs.end());
    d->savedConfigs.push_back(normalizedConfig);
    saveInstances();
    return true;
}

bool InstanceManager::cloneInstance(const std::string &sourceName, const std::string &newName) {
    if (!isValidInstanceName(sourceName) || !isValidInstanceName(newName)) return false;

    // Find source config
    InstanceConfig srcCfg;
    bool found = false;
    for (auto &vm : d->vms) {
        if (vm->config().name == sourceName) {
            srcCfg = getInstanceConfig(sourceName);
            found = true;
            break;
        }
    }
    if (!found) {
        for (auto &cfg : d->savedConfigs) {
            if (cfg.name == sourceName) { srcCfg = cfg; found = true; break; }
        }
    }
    if (!found) return false;

    srcCfg.name = newName;
    srcCfg.dataDir = d->instancesDir / newName;

    // Copy data directory if source exists
    auto srcDir = (d->instancesDir / sourceName);
    if (std::filesystem::exists(srcDir)) {
        try {
            std::filesystem::copy(srcDir, srcCfg.dataDir,
                                  std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing);
        } catch (...) {
            return false;
        }
    }

    return createInstance(srcCfg);
}

bool InstanceManager::deleteInstance(const std::string &name) {
    if (!isValidInstanceName(name)) return false;

    bool liveRemoved = false;

    // Stop and remove from live VMs
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            vm->stop();
        }
    }
    auto it = std::remove_if(d->vms.begin(), d->vms.end(), [&](auto &vm) {
        return vm->config().name == name;
    });
    if (it != d->vms.end()) {
        liveRemoved = true;
        d->vms.erase(it, d->vms.end());
    }

    // Remove from saved configs
    std::filesystem::path dataDir = d->instancesDir / name;
    auto sit = std::remove_if(d->savedConfigs.begin(), d->savedConfigs.end(),
                              [&](const auto &c) {
                                  if (c.name != name) return false;
                                  if (!c.dataDir.empty()) dataDir = c.dataDir;
                                  return true;
                              });
    bool removed = sit != d->savedConfigs.end();
    d->savedConfigs.erase(sit, d->savedConfigs.end());

    // Remove data directory
    try {
        if (std::filesystem::exists(dataDir)) {
            std::filesystem::remove_all(dataDir);
        }
    } catch (...) {
        return false;
    }

    if (removed || liveRemoved) {
        saveInstances();
        return true;
    }
    return false;
}

bool InstanceManager::startInstance(const std::string &name) {
    if (!isValidInstanceName(name)) return false;

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->start();
        }
    }
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            if (!createInstance(cfg)) return false;
            return startInstance(name);
        }
    }
    return false;
}

bool InstanceManager::stopInstance(const std::string &name) {
    if (!isValidInstanceName(name)) return false;

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->stop();
        }
    }
    return false;
}

bool InstanceManager::pauseInstance(const std::string &name) {
    if (!isValidInstanceName(name)) return false;

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->pause();
        }
    }
    return false;
}

bool InstanceManager::resumeInstance(const std::string &name) {
    if (!isValidInstanceName(name)) return false;

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->resume();
        }
    }
    return false;
}

VMState InstanceManager::getInstanceState(const std::string &name) const {
    if (!isValidInstanceName(name)) return VMState::Error;

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->state();
        }
    }
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            return VMState::Stopped;
        }
    }
    return VMState::Error;
}

InstanceConfig InstanceManager::getInstanceConfig(const std::string &name) const {
    if (!isValidInstanceName(name)) return InstanceConfig{};

    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            auto &c = vm->config();
            InstanceConfig cfg;
            cfg.name = c.name;
            cfg.cpus = c.cpus;
            cfg.ramMB = c.ramMB;
            cfg.width = c.width;
            cfg.height = c.height;
            cfg.dpi = c.dpi;
            cfg.graphicsEngine = c.graphicsEngine;
            cfg.graphicsRenderer = c.graphicsRenderer;
            cfg.maxFps = c.maxFps;
            cfg.enableVsync = c.enableVsync;
            cfg.headless = c.headless;
            cfg.allowVisibleEmulatorWindow = c.allowVisibleEmulatorWindow;
            cfg.processPriority = c.processPriority;
            cfg.enableRoot = c.enableRoot;
            cfg.enableAudio = c.enableAudio;
            cfg.quickBoot = c.quickBoot;
            cfg.deviceProfile = c.deviceProfile;
            cfg.dataDir = c.dataDir;
            return normalizedInstanceConfig(cfg);
        }
    }
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            return cfg;
        }
    }
    return InstanceConfig{};
}

uint32_t InstanceManager::emulatorProcessId(const std::string &name) const {
    for (const auto &vm : d->vms) {
        if (vm->config().name == name) return vm->processId();
    }
    return 0;
}

void InstanceManager::setStateCallback(StateCallback cb) {
    d->stateCallback = cb;
    for (auto &vm : d->vms) {
        vm->setStateCallback([this, name = vm->config().name](VMState s) {
            if (d->stateCallback) d->stateCallback(name, s);
        });
    }
}

InstanceRuntimeConfig InstanceManager::getRuntimeConfig(const std::string &name) const {
    const auto saved = std::find_if(d->savedConfigs.begin(), d->savedConfigs.end(),
                                    [&](const InstanceConfig& cfg) { return cfg.name == name; });
    int consolePort = (saved == d->savedConfigs.end()) ? 5554 : normalizedInstanceConfig(*saved).qmpPort;
    if (const auto overridePort = envValue("CHIMERA_EMULATOR_CONSOLE_PORT"); !overridePort.empty()) {
        try {
            consolePort = std::stoi(overridePort);
        } catch (...) {
            qWarning() << "Ignoring invalid CHIMERA_EMULATOR_CONSOLE_PORT:" << QString::fromStdString(overridePort);
        }
    }

    InstanceRuntimeConfig rc;
    rc.consolePort = consolePort;
    rc.adbPort     = consolePort + 1;
    rc.grpcPort    = 8554 + ((consolePort - 5554) / 2) * 2;
    rc.adbSerial   = "emulator-" + std::to_string(rc.consolePort);
    return rc;
}

bool InstanceManager::batchStartInstances(const std::vector<std::string> &names) {
    bool ok = true;
    for (const auto &n : names) ok &= startInstance(n);
    return ok;
}

bool InstanceManager::batchStopInstances(const std::vector<std::string> &names) {
    bool ok = true;
    for (const auto &n : names) ok &= stopInstance(n);
    return ok;
}

void InstanceManager::setGridPosition(const std::string &name, int row, int col) {
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            cfg.gridRow = row;
            cfg.gridCol = col;
            saveInstances();
            return;
        }
    }
}

bool InstanceManager::setMaxFps(const std::string &name, int maxFps) {
    if (!isValidInstanceName(name) || maxFps <= 0) return false;
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            cfg.maxFps = maxFps;
            saveInstances();
            return true;
        }
    }
    return false;
}

bool InstanceManager::setEnableRoot(const std::string &name, bool enabled) {
    if (!isValidInstanceName(name)) return false;
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            cfg.enableRoot = enabled;
            saveInstances();
            return true;
        }
    }
    return false;
}

bool InstanceManager::setEnableAudio(const std::string &name, bool enabled) {
    if (!isValidInstanceName(name)) return false;
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            cfg.enableAudio = enabled;
            saveInstances();
            return true;
        }
    }
    return false;
}

bool InstanceManager::setDeviceProfile(const std::string &name, const std::string &profileName) {
    if (!isValidInstanceName(name)) return false;
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            cfg.deviceProfile = profileName;
            saveInstances();
            return true;
        }
    }
    return false;
}

void InstanceManager::sortByName() {
    std::sort(d->savedConfigs.begin(), d->savedConfigs.end(),
              [](const InstanceConfig &a, const InstanceConfig &b) {
                  return a.name < b.name;
              });
    saveInstances();
}

} // namespace chimera::instance
