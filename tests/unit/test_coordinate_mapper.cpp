#include <QtTest>
#include "CoordinateMapper.h"

using namespace chimera::input;

class TestCoordinateMapper : public QObject {
    Q_OBJECT

private slots:
    // displayRect() tests
    void displayRectEmptyWhenNoSizes() {
        CoordinateMapper m;
        QVERIFY(m.displayRect().isEmpty());
    }

    void displayRectFillsWhenSameAspect() {
        CoordinateMapper m;
        m.setHostViewSize(1280, 720);
        m.setGuestSize(1280, 720);
        const QRectF dr = m.displayRect();
        QCOMPARE(dr.x(), 0.0);
        QCOMPARE(dr.y(), 0.0);
        QCOMPARE(dr.width(), 1280.0);
        QCOMPARE(dr.height(), 720.0);
    }

    void displayRectLetterboxesWhenHostTooWide() {
        CoordinateMapper m;
        // Host is 16:9, guest is 4:3 → letterbox on sides
        m.setHostViewSize(1600, 900);
        m.setGuestSize(640, 480);  // 4:3
        const QRectF dr = m.displayRect();
        // Guest aspect 4:3; host height is 900, so nw = 900*(4/3) = 1200, nh = 900
        QCOMPARE(dr.height(), 900.0);
        QCOMPARE(dr.width(), 1200.0);
        QCOMPARE(dr.y(), 0.0);
        QVERIFY(dr.x() > 0.0);  // letterbox on left/right
    }

    void displayRectPillarboxesWhenHostTooTall() {
        CoordinateMapper m;
        m.setHostViewSize(640, 960);  // portrait host
        m.setGuestSize(1280, 720);    // landscape guest
        const QRectF dr = m.displayRect();
        QCOMPARE(dr.width(), 640.0);
        QVERIFY(dr.y() > 0.0);  // letterbox top/bottom
    }

    // mapToGuest() tests — rotation 0
    void mapToGuestCenterRot0() {
        CoordinateMapper m;
        m.setHostViewSize(1280, 720);
        m.setGuestSize(1280, 720);
        QPoint g;
        QVERIFY(m.mapToGuest(QPointF(640, 360), g));
        // nx=0.5 → gx = 0.5*(1280-1) = 639.5 → 639; same for y
        QCOMPARE(g.x(), 639);
        QCOMPARE(g.y(), 359);
    }

    void mapToGuestTopLeftRot0() {
        CoordinateMapper m;
        m.setHostViewSize(1280, 720);
        m.setGuestSize(1280, 720);
        QPoint g;
        QVERIFY(m.mapToGuest(QPointF(0, 0), g));
        QCOMPARE(g.x(), 0);
        QCOMPARE(g.y(), 0);
    }

    void mapToGuestOutsideLetterboxReturnsFalse() {
        CoordinateMapper m;
        m.setHostViewSize(1600, 900);
        m.setGuestSize(640, 480);  // displayRect has x offset
        QPoint g;
        QVERIFY(!m.mapToGuest(QPointF(0, 0), g));  // in the letterbox
    }

    // rotation 180: center still maps to center
    void mapToGuestCenterRot180() {
        CoordinateMapper m;
        m.setHostViewSize(1280, 720);
        m.setGuestSize(1280, 720);
        m.setRotation(180);
        QPoint g;
        QVERIFY(m.mapToGuest(QPointF(640, 360), g));
        QCOMPARE(g.x(), 639);  // (1-0.5)*(1280-1) = 639.5 → 639
        QCOMPARE(g.y(), 359);
    }

    // rotation 90: host top-left maps to guest bottom-left
    void mapToGuestRot90TopLeft() {
        CoordinateMapper m;
        // Guest 720×1280 displayed at 90° CW → host sees 1280×720
        m.setGuestSize(720, 1280);
        m.setRotation(90);
        m.setHostViewSize(1280, 720);
        QPoint g;
        QVERIFY(m.mapToGuest(QPointF(0, 0), g));
        // nx=0, ny=0 → gx=(1-0)*(720-1)=719, gy=0*(1280-1)=0
        QCOMPARE(g.x(), 719);
        QCOMPARE(g.y(), 0);
    }

    // guestToHvSocket
    void hvSocketTopLeft() {
        CoordinateMapper m;
        m.setGuestSize(1280, 720);
        QCOMPARE(m.guestToHvSocket(QPoint(0, 0)), QPoint(0, 0));
    }

    void hvSocketBottomRight() {
        CoordinateMapper m;
        m.setGuestSize(1280, 720);
        QCOMPARE(m.guestToHvSocket(QPoint(1279, 719)), QPoint(32767, 32767));
    }

    void hvSocketCenter() {
        CoordinateMapper m;
        m.setGuestSize(1280, 720);
        const QPoint hv = m.guestToHvSocket(QPoint(640, 360));
        QVERIFY(hv.x() > 16000 && hv.x() < 17000);
        QVERIFY(hv.y() > 16000 && hv.y() < 17000);
    }

    void hvSocketClampsNegative() {
        CoordinateMapper m;
        m.setGuestSize(1280, 720);
        const QPoint hv = m.guestToHvSocket(QPoint(-10, -10));
        QCOMPARE(hv.x(), 0);
        QCOMPARE(hv.y(), 0);
    }
};

QTEST_MAIN(TestCoordinateMapper)
#include "test_coordinate_mapper.moc"
