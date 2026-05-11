#include "VirtualMachine.h"
#include "ProcessLauncher.h"
#include <windows.h>
#include <thread>

namespace chimera::instance {

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

    std::vector<std::string> args;
    args.push_back("-avd");
    args.push_back(m_config.avdName.empty() ? m_config.name : m_config.avdName);

    if (m_config.headless) {
        args.push_back("-no-window");
    }

    args.push_back("-accel");
    args.push_back("on");

    args.push_back("-gpu");
    args.push_back(m_config.graphicsRenderer == "d3d11" ? "swiftshader_indirect" : m_config.graphicsRenderer);

    args.push_back("-memory");
    args.push_back(std::to_string(m_config.ramMB));

    args.push_back("-cores");
    args.push_back(std::to_string(m_config.cpus));

    args.push_back("-skin");
    args.push_back(std::to_string(m_config.width) + "x" + std::to_string(m_config.height));

    args.push_back("-no-snapshot");
    args.push_back("-no-boot-anim");

    // Android Emulator console port IS the QMP interface.
    // -ports console,adb  →  console=QMP, adb=ADB daemon
    args.push_back("-ports");
    args.push_back(std::to_string(m_config.qmpPort) + "," + std::to_string(m_config.adbPort));

    // VirtIO Audio: QEMU arg passthrough for virtio-snd-pci
    // Note: Android Emulator binary may not have virtio-snd compiled in.
    // Full integration requires custom QEMU build + AudioBridge wiring.
    args.push_back("-qemu");
    args.push_back("-device");
    args.push_back("virtio-snd-pci");

    auto onStdout = [](const std::string &line) {
        // TODO: redirect to Logger
        (void)line;
    };
    auto onStderr = [](const std::string &line) {
        // TODO: redirect to Logger
        (void)line;
    };

    HANDLE hProc = ProcessLauncher::runAsync(
        m_config.emulatorPath.string(),
        args,
        onStdout,
        onStderr
    );

    if (!hProc) {
        m_state = VMState::Error;
        if (m_callback) m_callback(m_state);
        return false;
    }

    m_processHandle = hProc;
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
