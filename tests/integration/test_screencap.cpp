/**
 * Integration test: screencap verify
 *
 * Assumes a running emulator on 127.0.0.1:5554 (ADB serial emulator-5554).
 * Uses AdbFramebufferCapture to grab a frame and verify it's non-trivial.
 * Skipped if CHIMERA_ADB_PATH is not set.
 */
#include <QTest>
#include <QCoreApplication>
#include <QElapsedTimer>
#include "AdbFramebufferCapture.h"
#include "ProcessLauncher.h"

using namespace chimera::graphics;
using namespace chimera::instance;

static QString adbExe() { return qEnvironmentVariable("CHIMERA_ADB_PATH"); }

class TestScreencap : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        if (adbExe().isEmpty())
            QSKIP("CHIMERA_ADB_PATH not set — skipping screencap test");
    }

    void adbScreencapProducesNonBlankFrame() {
        AdbFramebufferCapture cap(adbExe(), /*adbPort=*/5555, /*usePng=*/false, nullptr);

        QImage frame;
        bool gotFrame = false;
        QObject::connect(&cap, &FramebufferCapture::frameReady,
                         [&](const QImage &img) {
                             frame = img;
                             gotFrame = true;
                         });

        cap.start();

        // Wait up to 15s for a frame
        QElapsedTimer t;
        t.start();
        while (!gotFrame && t.elapsed() < 15000)
            QTest::qWait(200);

        cap.stop();

        QVERIFY2(gotFrame, "No frame received within 15s");
        QVERIFY(!frame.isNull());
        QVERIFY(frame.width() > 0 && frame.height() > 0);

        // Frame should not be entirely black (emulator is running)
        bool hasNonZero = false;
        for (int y = 0; y < frame.height() && !hasNonZero; ++y) {
            const QRgb *row = reinterpret_cast<const QRgb *>(frame.constScanLine(y));
            for (int x = 0; x < frame.width() && !hasNonZero; ++x) {
                if (qRed(row[x]) > 10 || qGreen(row[x]) > 10 || qBlue(row[x]) > 10)
                    hasNonZero = true;
            }
        }
        QVERIFY2(hasNonZero, "Frame is entirely black — emulator may not be fully booted");
    }
};

QTEST_MAIN(TestScreencap)
#include "test_screencap.moc"
