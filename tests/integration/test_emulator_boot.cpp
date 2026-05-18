/**
 * Integration test: emulator boot + ADB connection
 *
 * Requires:
 *   - CHIMERA_ADB_PATH env var pointing to adb.exe
 *   - CHIMERA_AVD_NAME env var with a valid AVD name (e.g. "chimera_dev")
 *   - CHIMERA_EMULATOR_PATH env var pointing to emulator.exe
 *
 * If any required env var is missing the test is skipped automatically.
 */
#include <QTest>
#include <QProcess>
#include <QElapsedTimer>
#include "ProcessLauncher.h"

using namespace chimera::instance;

static QString adbExe()      { return qEnvironmentVariable("CHIMERA_ADB_PATH"); }
static QString emulatorExe() { return qEnvironmentVariable("CHIMERA_EMULATOR_PATH"); }
static QString avdName()     { return qEnvironmentVariable("CHIMERA_AVD_NAME"); }

static bool envReady() {
    return !adbExe().isEmpty() && !emulatorExe().isEmpty() && !avdName().isEmpty();
}

class TestEmulatorBoot : public QObject {
    Q_OBJECT

    HANDLE m_emulatorHandle = nullptr;

private slots:
    void initTestCase() {
        if (!envReady())
            QSKIP("CHIMERA_ADB_PATH / CHIMERA_EMULATOR_PATH / CHIMERA_AVD_NAME not set — skipping");
    }

    void cleanupTestCase() {
        if (m_emulatorHandle) {
            ProcessLauncher::terminate(m_emulatorHandle);
            ProcessLauncher::waitForExit(m_emulatorHandle, 5000);
            m_emulatorHandle = nullptr;
        }
    }

    void launchEmulator() {
        m_emulatorHandle = ProcessLauncher::runAsync(
            emulatorExe().toStdString(),
            {"-avd", avdName().toStdString(),
             "-no-audio", "-no-window",
             "-crash-report-mode", "never",
             "-no-metrics"},
            nullptr, nullptr, /*startHidden=*/true);
        QVERIFY(m_emulatorHandle != nullptr);
        QVERIFY(ProcessLauncher::isRunning(m_emulatorHandle));
    }

    void adbWaitForDevice() {
        // Poll until "emulator-5554 device" appears (up to 60s)
        const QString serial = QStringLiteral("emulator-5554");
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 60000) {
            const auto res = ProcessLauncher::runSync(
                adbExe().toStdString(),
                {"-s", serial.toStdString(), "get-state"});
            if (res.exitCode == 0 && res.stdoutText.find("device") != std::string::npos)
                break;
            QTest::qWait(2000);
        }
        const auto res = ProcessLauncher::runSync(
            adbExe().toStdString(),
            {"-s", "emulator-5554", "get-state"});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stdoutText.find("device") != std::string::npos);
    }

    void adbShellEcho() {
        const auto res = ProcessLauncher::runSync(
            adbExe().toStdString(),
            {"-s", "emulator-5554", "shell", "echo", "chimera_ok"});
        QCOMPARE(res.exitCode, 0);
        QVERIFY(res.stdoutText.find("chimera_ok") != std::string::npos);
    }
};

QTEST_MAIN(TestEmulatorBoot)
#include "test_emulator_boot.moc"
