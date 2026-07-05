#include <QTcpServer>
#include <QTest>
#include "EmulatorGrpcInput.h"

using namespace chimera::input;

// gRPC input transport breaker (Session 108). gRPC is the only pointer-input
// channel once wired and post() is fire-and-forget, so a dead endpoint used to
// drop every click silently with no fallback ("picture but nothing responds").
// The breaker must open after 3 consecutive transport failures (flipping
// InputBridge to console/QMP/ADB) and close again on any transport success.
class TestEmulatorGrpcInput : public QObject {
    Q_OBJECT

private slots:
    void startsHealthy() {
        EmulatorGrpcInput g(QStringLiteral("127.0.0.1"), 8554);
        QVERIFY(g.isHealthy());
    }

    void opensAfterThreeConsecutiveFailures() {
        EmulatorGrpcInput g(QStringLiteral("127.0.0.1"), 8554);
        g.recordTransportResult(false);
        g.recordTransportResult(false);
        QVERIFY(g.isHealthy()); // 2 strikes: a dropped reply must not exile the channel
        g.recordTransportResult(false);
        QVERIFY(!g.isHealthy());
    }

    void successResetsConsecutiveCount() {
        EmulatorGrpcInput g(QStringLiteral("127.0.0.1"), 8554);
        g.recordTransportResult(false);
        g.recordTransportResult(false);
        g.recordTransportResult(true); // interleaved success = channel alive
        g.recordTransportResult(false);
        g.recordTransportResult(false);
        QVERIFY(g.isHealthy()); // never 3 in a row
    }

    void recoversOnSuccess() {
        EmulatorGrpcInput g(QStringLiteral("127.0.0.1"), 8554);
        for (int i = 0; i < 5; ++i) g.recordTransportResult(false);
        QVERIFY(!g.isHealthy());
        g.recordTransportResult(true); // probe (or any reply) succeeded
        QVERIFY(g.isHealthy());
    }

    // End-to-end wiring: real POSTs to a refused port must open the breaker
    // through the actual QNetworkReply::finished path (not just the counter).
    void deadEndpointOpensBreakerViaNetwork() {
        // Grab a port that is verifiably free right now, then close it so
        // connections are refused.
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
        const int deadPort = probe.serverPort();
        probe.close();

        EmulatorGrpcInput g(QStringLiteral("127.0.0.1"), deadPort);
        for (int i = 0; i < 4; ++i) g.sendTouch(0, 100, 100, 1);
        QTRY_VERIFY_WITH_TIMEOUT(!g.isHealthy(), 10000);
    }
};

QTEST_MAIN(TestEmulatorGrpcInput)
#include "test_emulator_grpc_input.moc"
