#pragma once
#include <string>

namespace chimera::instance {

/**
 * @brief Per-instance runtime port configuration.
 *
 * Abstracts the console/ADB/gRPC ports and ADB serial so that callers
 * do not hardcode "emulator-5554". P4c multi-instance grid populates one
 * config per running instance; P0/P1 ships a single-instance default.
 */
struct InstanceRuntimeConfig {
    int         consolePort = 5554;   // Android Console telnet (AndroidConsoleInput)
    int         adbPort     = 5555;   // ADB TCP forward port
    int         grpcPort    = 8554;   // gRPC streamScreenshot port
    std::string adbSerial   = "emulator-5554"; // -s argument to adb commands
};

} // namespace chimera::instance
