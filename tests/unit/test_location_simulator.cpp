#include <QtTest>
#include "LocationSimulator.h"

using namespace chimera::integration;

class TestLocationSimulator : public QObject {
    Q_OBJECT

private slots:
    void setLocationUpdatesCurrentLocation() {
        LocationSimulator::instance().setGeoSink(nullptr);
        LocationSimulator::instance().setLocation(25.0330, 121.5654, 10.0);
        const GeoPoint pt = LocationSimulator::instance().currentLocation();
        QCOMPARE(pt.latitude,  25.0330);
        QCOMPARE(pt.longitude, 121.5654);
        QCOMPARE(pt.altitude,  10.0);
    }

    void geoSinkCalledOnSetLocation() {
        double gotLon = 0, gotLat = 0, gotAlt = 0;
        LocationSimulator::instance().setGeoSink([&](double lon, double lat, double alt) {
            gotLon = lon;
            gotLat = lat;
            gotAlt = alt;
        });

        // Move far enough from previous to exceed throttle threshold
        LocationSimulator::instance().setLocation(35.6762, 139.6503, 5.0);

        // Sink receives (lon, lat, alt) — not (lat, lon, alt)
        QCOMPARE(gotLon, 139.6503);
        QCOMPARE(gotLat, 35.6762);
        QCOMPARE(gotAlt, 5.0);

        LocationSimulator::instance().setGeoSink(nullptr);
    }

    void isSimulatingFalseByDefault() {
        LocationSimulator::instance().stopSimulation();
        QVERIFY(!LocationSimulator::instance().isSimulating());
    }

    void loadRouteAndStartSimulationDoesNotCrash() {
        LocationSimulator::instance().loadRoute(
            {{25.0, 121.5, 0.0}, {25.1, 121.6, 0.0}, {25.2, 121.7, 0.0}});
        LocationSimulator::instance().startSimulation(10.0);
        QVERIFY(LocationSimulator::instance().isSimulating());
        LocationSimulator::instance().update(0.1);
        LocationSimulator::instance().stopSimulation();
        QVERIFY(!LocationSimulator::instance().isSimulating());
    }

    void setLocationNoSinkDoesNotCrash() {
        LocationSimulator::instance().setGeoSink(nullptr);
        LocationSimulator::instance().setLocation(1.0, 2.0, 3.0);
        QVERIFY(true);
    }

    void explicitSetLocationIsNotDroppedByRouteThrottle() {
        auto &sim = LocationSimulator::instance();
        int count = 0;
        sim.setGeoSink([&](double, double, double) { count++; });

        // Prime: a far jump always emits and resets the throttle clock to now.
        sim.setLocation(-40.0, -70.0, 0.0);
        count = 0;

        // An immediate, deliberate re-set to the same spot must still reach the
        // guest — the 1 Hz/movement throttle is for route simulation, not for
        // explicit user "teleport" calls.
        sim.setLocation(-40.0, -70.0, 0.0);
        QCOMPARE(count, 1);

        sim.setGeoSink(nullptr);
    }
};

QTEST_MAIN(TestLocationSimulator)
#include "test_location_simulator.moc"
