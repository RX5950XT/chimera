#include "LocationSimulator.h"
#include <cmath>

namespace chimera::integration {

LocationSimulator &LocationSimulator::instance() {
    static LocationSimulator inst;
    return inst;
}

void LocationSimulator::setGeoSink(GeoSink sink) {
    m_geoSink = std::move(sink);
}

void LocationSimulator::setLocation(double lat, double lon, double altitude) {
    m_current = {lat, lon, altitude};
    emitGeoFix(m_current, /*force=*/true);
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

    const double step = m_speed * deltaSeconds;
    const GeoPoint &next = m_route[m_routeIndex + 1];
    const double dx = next.latitude  - m_current.latitude;
    const double dy = next.longitude - m_current.longitude;
    const double dist = std::sqrt(dx * dx + dy * dy);

    if (dist < 1e-9) {
        m_routeIndex++;
        return;
    }
    if (step >= dist) {
        m_current = next;
        m_routeIndex++;
    } else {
        m_current.latitude  += (dx / dist) * step;
        m_current.longitude += (dy / dist) * step;
    }

    emitGeoFix(m_current);
}

void LocationSimulator::emitGeoFix(const GeoPoint &pt, bool force) {
    if (!m_geoSink) return;

    // Throttle: only emit if enough time has passed OR location moved enough.
    // Explicit setLocation() passes force=true so a deliberate fix is never dropped.
    const auto now = std::chrono::steady_clock::now();
    const double elapsedSec = std::chrono::duration<double>(now - m_lastEmitTime).count();
    const double dLat = std::abs(pt.latitude  - m_lastEmitted.latitude);
    const double dLon = std::abs(pt.longitude - m_lastEmitted.longitude);

    if (!force && elapsedSec < kMinIntervalSec && dLat < kMinMovementDeg && dLon < kMinMovementDeg)
        return;

    m_lastEmitted = pt;
    m_lastEmitTime = now;

    // Android Console "geo fix" order: longitude, latitude, altitude
    m_geoSink(pt.longitude, pt.latitude, pt.altitude);
}

} // namespace chimera::integration
