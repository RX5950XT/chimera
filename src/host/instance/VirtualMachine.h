#pragma once

#include <string>
#include <vector>
#include <chrono>
#include <functional>
#include <filesystem>
#include <atomic>
#include <thread>

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
    bool useClassicEmuglRuntime = false; // Legacy EmuGL runtime does not support modern emulator CLI flags
    int vncPort = 5900;                  // QEMU VNC display port; unsupported by current emulator GPU modes
    bool enableVnc = false;
    bool headless = true;                // -no-window flag; production default
    bool allowVisibleEmulatorWindow = false; // unsafe diagnostics only
    bool startHidden = true;             // hidden launch protects desktop unless unsafe diagnostics need visibility
    bool enableRoot = false;             // -writable-system (google_apis only; adb root post-boot)
    bool enableAudio = false;            // Remove -no-audio: emulator routes Android Goldfish audio to host WASAPI
    bool quickBoot = false;               // Opt-in named emulator snapshot; default protects host audio
    std::string deviceProfile;           // Device spoofing profile name
    std::string processPriority = "below_normal";
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
    void setState(VMState state, bool notify = true);
    void joinExitMonitor();
    void startExitMonitor(uint32_t rootPid);

    VirtualMachineConfig m_config;
    std::atomic<VMState> m_state{VMState::Stopped};
    StateCallback m_callback;
    void *m_processHandle = nullptr;  // Windows HANDLE
    std::thread m_exitMonitor;
};

} // namespace chimera::instance
