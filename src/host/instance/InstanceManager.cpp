#include "InstanceManager.h"
#include "VirtualMachine.h"
#include "DeviceSpoofer.h"
#include <algorithm>
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
};

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
    if (std::filesystem::exists(path)) {
        std::ifstream f(path);
        if (f.is_open()) f >> j;
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
            cfg.ramMB = item.value("ramMB", 4096);
            cfg.width = item.value("width", 1920);
            cfg.height = item.value("height", 1080);
            cfg.dpi = item.value("dpi", 240);
            cfg.graphicsEngine = item.value("graphicsEngine", "angle");
            cfg.graphicsRenderer = item.value("graphicsRenderer", "d3d11");
            cfg.maxFps = item.value("maxFps", 60);
            cfg.enableVsync = item.value("enableVsync", false);
            cfg.enableRoot = item.value("enableRoot", false);
            cfg.dataDir = item.value("dataDir", "");
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
        item["dataDir"] = cfg.dataDir.string();
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
        item["graphicsEngine"] = c.graphicsEngine;
        item["graphicsRenderer"] = c.graphicsRenderer;
        item["maxFps"] = c.maxFps;
        item["enableVsync"] = c.enableVsync;
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
    for (auto &vm : d->vms) {
        names.push_back(vm->config().name);
    }
    return names;
}

bool InstanceManager::createInstance(const InstanceConfig &config) {
    VirtualMachineConfig vmConfig;
    vmConfig.name = config.name;
    vmConfig.cpus = config.cpus;
    vmConfig.ramMB = config.ramMB;
    vmConfig.width = config.width;
    vmConfig.height = config.height;
    vmConfig.graphicsEngine = config.graphicsEngine;
    vmConfig.graphicsRenderer = config.graphicsRenderer;
    vmConfig.maxFps = config.maxFps;
    vmConfig.enableVsync = config.enableVsync;
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

    auto vm = std::make_unique<VirtualMachine>(vmConfig);
    if (!vm->create()) return false;
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
    // Stop and remove from live VMs
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            vm->stop();
        }
    }
    auto it = std::remove_if(d->vms.begin(), d->vms.end(), [&](auto &vm) {
        return vm->config().name == name;
    });
    if (it != d->vms.end()) d->vms.erase(it, d->vms.end());

    // Remove from saved configs
    auto sit = std::remove_if(d->savedConfigs.begin(), d->savedConfigs.end(),
                              [&](const auto &c) { return c.name == name; });
    bool removed = sit != d->savedConfigs.end();
    d->savedConfigs.erase(sit, d->savedConfigs.end());

    // Remove data directory
    auto dataDir = d->instancesDir / name;
    if (std::filesystem::exists(dataDir)) {
        std::filesystem::remove_all(dataDir);
    }

    if (removed || it != d->vms.end()) {
        saveInstances();
        return true;
    }
    return false;
}

bool InstanceManager::startInstance(const std::string &name) {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->start();
        }
    }
    return false;
}

bool InstanceManager::stopInstance(const std::string &name) {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->stop();
        }
    }
    return false;
}

bool InstanceManager::pauseInstance(const std::string &name) {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->pause();
        }
    }
    return false;
}

bool InstanceManager::resumeInstance(const std::string &name) {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->resume();
        }
    }
    return false;
}

VMState InstanceManager::getInstanceState(const std::string &name) const {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            return vm->state();
        }
    }
    return VMState::Error;
}

InstanceConfig InstanceManager::getInstanceConfig(const std::string &name) const {
    for (auto &vm : d->vms) {
        if (vm->config().name == name) {
            auto &c = vm->config();
            InstanceConfig cfg;
            cfg.name = c.name;
            cfg.cpus = c.cpus;
            cfg.ramMB = c.ramMB;
            cfg.width = c.width;
            cfg.height = c.height;
            cfg.graphicsEngine = c.graphicsEngine;
            cfg.graphicsRenderer = c.graphicsRenderer;
            cfg.maxFps = c.maxFps;
            cfg.enableVsync = c.enableVsync;
            cfg.dataDir = c.dataDir;
            return cfg;
        }
    }
    return InstanceConfig{};
}

void InstanceManager::setStateCallback(StateCallback cb) {
    for (auto &vm : d->vms) {
        vm->setStateCallback([cb, name = vm->config().name](VMState s) {
            cb(name, s);
        });
    }
}

} // namespace chimera::instance
