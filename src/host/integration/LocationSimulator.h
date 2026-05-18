#pragma once

#include <string>
#include <vector>
#include <functional>
#include <chrono>

namespace chimera::integration {

struct GeoPoint {
    double latitude;
    double longitude;
    double altitude;
};

/**
 * @brief Simulates GPS location for the Android guest.
 *
 * Wiring: call setGeoSink() with AndroidConsoleInput::sendGeoFix to push
 * "geo fix <lon> <lat> <alt>" updates to the emulator via the console channel.
 * Sink signature uses (lon, lat, alt) order matching the Android Console protocol.
 */
class LocationSimulator {
public:
    // (lon, lat, alt) — matches Android Console "geo fix" argument order
    using GeoSink = std::function<void(double lon, double lat, double alt)>;

    static LocationSimulator &instance();

    // Wire the sink that delivers geo fixes to the guest (e.g. AndroidConsoleInput)
    void setGeoSink(GeoSink sink);

    void setLocation(double lat, double lon, double altitude = 0.0);
    GeoPoint currentLocation() const;

    // Route simulation
    void loadRoute(const std::vector<GeoPoint> &route);
    void startSimulation(double speedMps);
    void stopSimulation();
    bool isSimulating() const;

    // Called every frame to advance location along route
    void update(double deltaSeconds);

private:
    LocationSimulator() = default;

    void emitGeoFix(const GeoPoint &pt);

    GeoPoint m_current{0.0, 0.0, 0.0};
    GeoPoint m_lastEmitted{0.0, 0.0, 0.0};
    std::vector<GeoPoint> m_route;
    size_t m_routeIndex = 0;
    bool m_simulating = false;
    double m_speed = 0.0;

    GeoSink m_geoSink;
    std::chrono::steady_clock::time_point m_lastEmitTime{};

    static constexpr double kMinMovementDeg = 1e-6;   // ~0.1m threshold
    static constexpr double kMinIntervalSec = 1.0;    // 1 Hz cap
};

} // namespace chimera::integration
