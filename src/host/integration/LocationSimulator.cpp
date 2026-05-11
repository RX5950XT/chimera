#include "LocationSimulator.h"
#include <cmath>

namespace chimera::integration {

LocationSimulator &LocationSimulator::instance() {
    static LocationSimulator inst;
    return inst;
}

void LocationSimulator::setLocation(double lat, double lon, double altitude) {
    m_current = {lat, lon, altitude};
}

GeoPoint LocationSimulator::currentLocation() const {
    return m_current;
}

void LocationSimulator::loadRoute(const std::vector<GeoPoint> &route) {
    m_route = route;
    m_routeIndex = 0;
}

void LocationSimulator::startSimulation(double speedMps) {
    m_speed = speedMps;
    m_simulating = true;
}

void LocationSimulator::stopSimulation() {
    m_simulating = false;
}

bool LocationSimulator::isSimulating() const {
    return m_simulating;
}

void LocationSimulator::update(double deltaSeconds) {
    if (!m_simulating || m_route.empty() || m_routeIndex >= m_route.size() - 1) return;
    double step = m_speed * deltaSeconds;
    GeoPoint &next = m_route[m_routeIndex + 1];
    double dx = next.latitude - m_current.latitude;
    double dy = next.longitude - m_current.longitude;
    double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 1e-9) {
        m_routeIndex++;
        return;
    }
    if (step >= dist) {
        m_current = next;
        m_routeIndex++;
    } else {
        m_current.latitude += (dx / dist) * step;
        m_current.longitude += (dy / dist) * step;
    }
}

} // namespace chimera::integration
