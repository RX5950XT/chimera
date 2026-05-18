#include "InstanceManager.h"
#include "VirtualMachine.h"
#include "DeviceSpoofer.h"
#include <algorithm>
#include <cctype>
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
            cfg.cpus = item.value("cpus", 4);
            cfg.ramMB = item.value("ramMB", 2048);
            cfg.width = item.value("width", 1280);
            cfg.height = item.value("height", 720);
            cfg.dpi = item.value("dpi", 240);
            cfg.graphicsEngine = item.value("graphicsEngine", "angle");
            cfg.graphicsRenderer = item.value("graphicsRenderer", "host");
            cfg.maxFps = item.value("maxFps", 60);
            cfg.enableVsync = item.value("enableVsync", false);
            cfg.enableRoot = item.value("enableRoot", false);
            cfg.enableAudio = item.value("enableAudio", false);
            cfg.deviceProfile = item.value("deviceProfile", "");
            cfg.headless = item.value("headless", false);
            cfg.processPriority = item.value("processPriority", "high");
            cfg.dataDir  = item.value("dataDir", "");
            cfg.gridRow  = item.value("gridRow", 0);
            cfg.gridCol  = item.value("gridCol", 0);
            d->savedConfigs.push_back(cfg);
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
        seen.insert(cfg.name);
        nlohmann::json item;
        item["name"] = cfg.name;
        item["cpus"] = cfg.cpus;
        item["ramMB"] = cfg.ramMB;
        item["width"] = cfg.width;
        item["height"] = cfg.height;
        item["dpi"] = cfg.dpi;
        item["graphicsEngine"] = cfg.graphicsEngine;
        item["graphicsRenderer"] = cfg.graphicsRenderer;
        item["maxFps"] = cfg.maxFps;
        item["enableVsync"] = cfg.enableVsync;
        item["enableRoot"] = cfg.enableRoot;
        item["enableAudio"] = cfg.enableAudio;
        item["deviceProfile"] = cfg.deviceProfile;
        item["headless"] = cfg.headless;
        item["processPriority"] = cfg.processPriority;
        item["dataDir"]  = cfg.dataDir.string();
        item["gridRow"]  = cfg.gridRow;
        item["gridCol"]  = cfg.gridCol;
        arr.push_back(item);
    }
    for (auto &vm : d->vms) {
        auto &c = vm->config();
        if (c.name.empty() || seen.count(c.name)) continue;
        seen.insert(c.name);
        nlohmann::json item;
        item["name"] = c.name;
        item["cpus"] = c.cpus;
        item["ramMB"] = c.ramMB;
        item["width"] = c.width;
        item["height"] = c.height;
        item["dpi"] = c.dpi;
        item["graphicsEngine"] = c.graphicsEngine;
        item["graphicsRenderer"] = c.graphicsRenderer;
        item["maxFps"] = c.maxFps;
        item["enableVsync"] = c.enableVsync;
        item["headless"] = c.headless;
        item["processPriority"] = c.processPriority;
        item["dataDir"] = c.dataDir.string();
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
    if (!isValidInstanceName(config.name)) return false;
    for (auto &vm : d->vms) {
        if (vm->config().name == config.name) return false;
    }

    VirtualMachineConfig vmConfig;
    vmConfig.name = config.name;
    vmConfig.cpus = config.cpus;
    vmConfig.ramMB = config.ramMB;
    vmConfig.width = config.width;
    vmConfig.height = config.height;
    vmConfig.dpi = config.dpi;
    vmConfig.graphicsEngine = config.graphicsEngine;
    vmConfig.graphicsRenderer = config.graphicsRenderer;
    vmConfig.maxFps = config.maxFps;
    vmConfig.enableVsync = config.enableVsync;
    vmConfig.headless = config.headless;
    vmConfig.enableRoot = config.enableRoot;
    vmConfig.enableAudio = config.enableAudio;
    vmConfig.processPriority = config.processPriority;
    vmConfig.qmpPort = config.qmpPort;
    vmConfig.deviceProfile = config.deviceProfile;
    vmConfig.dataDir = config.dataDir.empty() ? (d->instancesDir / config.name) : config.dataDir;

    // Apply device spoofing if profile specified
    if (!config.deviceProfile.empty()) {
        auto profiles = DeviceSpoofer::getBuiltinProfiles();
        for (auto &p : profiles) {
            if (p.name == config.deviceProfile) {
                DeviceSpoofer::instance().applyProfile(p, config.name);
                break;
            }
        }
    }

    auto sdkCfg = loadAndroidSdkConfig();
    if (sdkCfg.contains("emulator")) {
        vmConfig.emulatorPath = sdkCfg["emulator"].get<std::string>();
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
                             [&](const auto &c) { return c.name == config.name; });
    d->savedConfigs.erase(it, d->savedConfigs.end());
    d->savedConfigs.push_back(config);
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
            cfg.processPriority = c.processPriority;
            cfg.dataDir = c.dataDir;
            return cfg;
        }
    }
    for (auto &cfg : d->savedConfigs) {
        if (cfg.name == name) {
            return cfg;
        }
    }
    return InstanceConfig{};
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
    // Port assignment: instance at sorted index N → base + 2*N
    // Standard emulator spacing: even port = console, odd = ADB
    static constexpr int kBaseConsole = 5554;
    static constexpr int kBaseGrpc    = 8554;

    const auto names = listInstances();
    const auto it = std::find(names.begin(), names.end(), name);
    const int idx = (it == names.end()) ? 0 : static_cast<int>(it - names.begin());

    InstanceRuntimeConfig rc;
    rc.consolePort = kBaseConsole + idx * 2;
    rc.adbPort     = kBaseConsole + idx * 2 + 1;
    rc.grpcPort    = kBaseGrpc    + idx * 2;
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
