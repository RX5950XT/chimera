/**
 * Integration test: Console input injection
 *
 * Assumes a running emulator on 127.0.0.1:5554.
 * Skipped if CHIMERA_ADB_PATH is not set.
 */
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include "AndroidConsoleInput.h"
#include "ProcessLauncher.h"

using namespace chimera::input;
using namespace chimera::instance;

static QString adbExe() { return qEnvironmentVariable("CHIMERA_ADB_PATH"); }

class TestInputInject : public QObject {
    Q_OBJECT

    AndroidConsoleInput *m_console = nullptr;

private slots:
    void initTestCase() {
        if (adbExe().isEmpty())
            QSKIP("CHIMERA_ADB_PATH not set — skipping input injection test");
    }

    void cleanup() {
        if (m_console) {
            m_console->disconnectFromHost();
            m_console->deleteLater();
            m_console = nullptr;
        }
    }

    void consoleConnectsAndReachesReady() {
        m_console = new AndroidConsoleInput(this);
        m_console->setAutoReconnect(false);

        bool ready = false;
        QObject::connect(m_console, &AndroidConsoleInput::stateChanged,
                         [&](AndroidConsoleInput::State s) {
                             if (s == AndroidConsoleInput::State::Ready) ready = true;
                         });

        m_console->connectToHost(QStringLiteral("127.0.0.1"), 5554);

        // Wait up to 10s for Ready state
        QElapsedTimer t;
        t.start();
        while (!ready && t.elapsed() < 10000)
            QTest::qWait(100);

        QVERIFY2(ready, "AndroidConsoleInput did not reach Ready state within 10s");
        QVERIFY(m_console->isConnected());
    }

    void mouseMoveInjected() {
        QVERIFY2(m_console && m_console->isConnected(),
                 "Prerequisite: consoleConnectsAndReachesReady must pass first");

        // Inject a mouse move — accepted silently by the emulator
        const bool sent = m_console->sendMouseMove(100, 200);
        QVERIFY(sent);

        QTest::qWait(50);  // allow send to flush
        QVERIFY(m_console->isConnected());
    }

    void keyEventInjected() {
        QVERIFY2(m_console && m_console->isConnected(),
                 "Prerequisite: consoleConnectsAndReachesReady must pass first");

        if (!m_console->isKeyboardReady())
            QSKIP("Keyboard probe failed — keyboard uses ADB fallback, skipping key injection");

        // KEYCODE_BACK = 4 — fires Back; we press+release quickly
        QVERIFY(m_console->sendKeyEvent(4, true));
        QTest::qWait(30);
        QVERIFY(m_console->sendKeyEvent(4, false));
        QTest::qWait(50);

        QVERIFY(m_console->isConnected());
    }

    void injectionLatencyIsLow() {
        QVERIFY2(m_console && m_console->isConnected(),
                 "Prerequisite: consoleConnectsAndReachesReady must pass first");

        m_console->sendMouseMove(50, 50);
        QTest::qWait(100);
        // Injection latency should be <50ms (target <10ms, but allow headroom in CI)
        QVERIFY2(m_console->lastLatencyMs() < 50.0,
                 qPrintable(QStringLiteral("Injection latency %1ms exceeds 50ms")
                                .arg(m_console->lastLatencyMs())));
    }
};

QTEST_MAIN(TestInputInject)
#include "test_input_inject.moc"
