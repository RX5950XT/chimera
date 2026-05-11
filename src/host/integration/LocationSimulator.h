#pragma once

#include <string>
#include <vector>

namespace chimera::integration {

struct GeoPoint {
    double latitude;
    double longitude;
    double altitude;
};

/**
 * @brief Simulates GPS location for the Android guest.
 */
class LocationSimulator {
public:
    static LocationSimulator &instance();

    void setLocation(double lat, double lon, double altitude = 0.0);
    GeoPoint currentLocation() const;

    // Route simulation
    void loadRoute(const std::vector<GeoPoint> &route);
    void startSimulation(double speedMps);
    void stopSimulation();
    bool isSimulating() const;

    // Called every frame to update location along route
    void update(double deltaSeconds);

private:
    LocationSimulator() = default;
    GeoPoint m_current{0.0, 0.0, 0.0};
    std::vector<GeoPoint> m_route;
    size_t m_routeIndex = 0;
    bool m_simulating = false;
    double m_speed = 0.0;
};

} // namespace chimera::integration
