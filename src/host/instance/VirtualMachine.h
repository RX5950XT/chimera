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
    int cpus = 2;
    int ramMB = 2048;
    int width = 1920;
    int height = 1080;
    int dpi = 320;
    std::string graphicsEngine = "angle";
    std::string graphicsRenderer = "host";
    int maxFps = 60;
    bool enableVsync = false;
    std::filesystem::path systemImage;
    std::filesystem::path dataDir;
    std::filesystem::path emulatorPath;  // Path to emulator.exe
    std::string avdName;                 // AVD name to launch
    std::filesystem::path avdHome;       // Android AVD home, used for stale lock cleanup
    int adbPort = 5555;
    int qmpPort = 5554;                  // QEMU monitor port for QMP input
    int grpcPort = 8554;                 // Android Emulator gRPC control/display stream
    bool enableGrpc = true;
    int vncPort = 5900;                  // QEMU VNC display port; unsupported by current emulator GPU modes
    bool enableVnc = false;
    bool headless = false;               // -no-window flag; false enables native window embedding
    bool enableRoot = false;             // -writable-system (google_apis only; adb root post-boot)
    bool enableAudio = false;            // Remove -no-audio flag; emulator handles host audio natively
    bool quickBoot = true;                // Load/save named emulator snapshot to reduce boot time
    std::string deviceProfile;           // Device spoofing profile name
    std::string processPriority = "normal";
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

    // Returns the OS process ID of the running emulator (0 if not running)
    uint32_t processId() const;

    // QEMU command-line builder
    std::vector<std::string> buildQemuArgs() const;
    std::vector<std::string> buildEmulatorArgs() const;

    using StateCallback = std::function<void(VMState)>;
    void setStateCallback(StateCallback cb);

private:
    VirtualMachineConfig m_config;
    VMState m_state = VMState::Stopped;
    StateCallback m_callback;
    void *m_processHandle = nullptr;  // Windows HANDLE
};

} // namespace chimera::instance
