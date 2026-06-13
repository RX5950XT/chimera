#pragma once

#include "VirtualMachine.h"
#include "InstanceRuntimeConfig.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace chimera::instance {

struct InstanceConfig {
    std::string name;
    int cpus = 2;
    int ramMB = 2048;
    int width = 1920;
    int height = 1080;
    int dpi = 320;
    std::string graphicsEngine = "angle";
    std::string graphicsRenderer = "host";
    int maxFps = 60;
    bool enableVsync = false;
    bool enableRoot = false;
    bool enableAudio = false;            // Enables host audio output via WASAPI
    bool quickBoot = false;              // Opt-in emulator snapshot quick boot
    bool headless = true;
    bool allowVisibleEmulatorWindow = false; // unsafe diagnostics only; production keeps emulator headless
    std::string deviceProfile;           // Device spoofing profile name
    std::string processPriority = "below_normal";
    int qmpPort = 5554;
    std::filesystem::path dataDir;

    // Grid position in the multi-instance manager UI
    int gridRow = 0;
    int gridCol = 0;
};

struct EmulatorRuntimeCapabilities {
    std::filesystem::path runtimeDir;
    bool hasLegacyOpenglRender = false;
    bool hasRequiredEmuglRuntimeDlls = false;
    bool hasGfxstreamBackend = false;
    bool hasChimeraSharedTextureManifest = false;
    bool hasChimeraGfxstreamSharedTextureManifest = false;
    bool hasChimeraGfxstreamBridgeMarker = false;
    bool hasCompatibleGfxstreamAbi = false;
    bool hasSdkGfxstreamRuntimeImports = false;
    bool hasMatchingGfxstreamBuildId = false;
    bool hasMismatchedGfxstreamBuildId = false;
    bool supportsChimeraEmuglSharedTexture = false;
    bool supportsChimeraGfxstreamSharedTexture = false;
    std::string status;
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

    // Per-instance runtime port assignment (index-based: instance N → port 5554+2N)
    InstanceRuntimeConfig getRuntimeConfig(const std::string &name) const;

    // Batch operations (executed sequentially)
    bool batchStartInstances(const std::vector<std::string> &names);
    bool batchStopInstances(const std::vector<std::string> &names);

    // Grid layout — update stored gridRow/gridCol
    void setGridPosition(const std::string &name, int row, int col);

    // Sort the internal instance list by name
    void sortByName();

    // Update max FPS for a saved instance (takes effect on next start)
    bool setMaxFps(const std::string &name, int maxFps);

    // Toggle root mode for a saved instance (takes effect on next start)
    bool setEnableRoot(const std::string &name, bool enabled);

    // Toggle audio for a saved instance (takes effect on next start)
    bool setEnableAudio(const std::string &name, bool enabled);

    // Set device spoofing profile for a saved instance (takes effect on next start)
    bool setDeviceProfile(const std::string &name, const std::string &profileName);

    // Returns the OS process ID of the running emulator for an instance (0 if not running)
    uint32_t emulatorProcessId(const std::string &name) const;

    // Probe whether an emulator runtime can load Chimera's modified shared texture
    // producer. Stock gfxstream runtimes are explicitly not enough.
    static EmulatorRuntimeCapabilities probeEmulatorRuntime(
        const std::filesystem::path &emulatorPath);

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
