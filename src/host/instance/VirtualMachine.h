#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <filesystem>

namespace chimera::instance {

enum class VMState { Stopped, Creating, Created, Starting, Running, Paused, Stopping, Error };

struct VirtualMachineConfig {
    std::string name;
    int cpus = 4;
    int ramMB = 4096;
    int width = 1920;
    int height = 1080;
    std::string graphicsEngine = "angle";
    std::string graphicsRenderer = "d3d11";
    int maxFps = 60;
    bool enableVsync = false;
    std::filesystem::path systemImage;
    std::filesystem::path dataDir;
    std::filesystem::path emulatorPath;  // Path to emulator.exe
    std::string avdName;                 // AVD name to launch
    int adbPort = 5555;
    int qmpPort = 5554;                  // QEMU monitor port for QMP input
    bool headless = true;                // -no-window flag
    std::string deviceProfile;           // Device spoofing profile name
};

/**
 * @brief Wraps a single QEMU / Hyper-V VM instance.
 */
class VirtualMachine {
public:
    VirtualMachine(const VirtualMachineConfig &config);
    ~VirtualMachine();

    bool create();
    bool start();
    bool stop();
    bool pause();
    bool resume();

    VMState state() const;
    const VirtualMachineConfig &config() const { return m_config; }

    // QEMU command-line builder
    std::vector<std::string> buildQemuArgs() const;

    using StateCallback = std::function<void(VMState)>;
    void setStateCallback(StateCallback cb);

private:
    VirtualMachineConfig m_config;
    VMState m_state = VMState::Stopped;
    StateCallback m_callback;
    void *m_processHandle = nullptr;  // Windows HANDLE
};

} // namespace chimera::instance
