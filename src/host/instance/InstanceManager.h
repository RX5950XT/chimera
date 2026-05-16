#pragma once

#include "VirtualMachine.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace chimera::instance {

struct InstanceConfig {
    std::string name;
    int cpus = 4;
    int ramMB = 2048;
    int width = 1280;
    int height = 720;
    int dpi = 240;
    std::string graphicsEngine = "angle";
    std::string graphicsRenderer = "host";
    int maxFps = 60;
    bool enableVsync = false;
    bool enableRoot = false;
    bool headless = false;
    std::string deviceProfile;           // Device spoofing profile name
    std::string processPriority = "high";
    int qmpPort = 5554;
    std::filesystem::path dataDir;
};

/**
 * @brief Manages the lifecycle of Android VM instances.
 *
 * Wraps QEMU or Hyper-V HCS to create, start, stop, and clone VM instances.
 */
class InstanceManager {
public:
    static InstanceManager &instance();

    // Instance CRUD
    std::vector<std::string> listInstances() const;
    bool createInstance(const InstanceConfig &config);
    bool cloneInstance(const std::string &sourceName, const std::string &newName);
    bool deleteInstance(const std::string &name);

    // Lifecycle
    bool startInstance(const std::string &name);
    bool stopInstance(const std::string &name);
    bool pauseInstance(const std::string &name);
    bool resumeInstance(const std::string &name);

    VMState getInstanceState(const std::string &name) const;
    InstanceConfig getInstanceConfig(const std::string &name) const;

    // Callbacks
    using StateCallback = std::function<void(const std::string &name, VMState state)>;
    void setStateCallback(StateCallback cb);

private:
    InstanceManager();
    ~InstanceManager();
    class Impl;
    std::unique_ptr<Impl> d;

    void loadInstances();
    void saveInstances() const;
};

} // namespace chimera::instance
