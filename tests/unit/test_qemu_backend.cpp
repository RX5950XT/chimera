#include <QTest>
#include <QFile>
#include "QemuBackend.h"
#include "HyperVManager.h"

#include <algorithm>
#include <iterator>

using namespace chimera::instance;

namespace {

bool containsArg(const std::vector<std::string> &args, const std::string &arg) {
    return std::find(args.begin(), args.end(), arg) != args.end();
}

std::string valueAfterArg(const std::vector<std::string> &args, const std::string &arg) {
    const auto it = std::find(args.begin(), args.end(), arg);
    if (it == args.end() || std::next(it) == args.end()) return {};
    return *std::next(it);
}

} // namespace

class TestQemuBackend : public QObject {
    Q_OBJECT

private slots:
    void cuttlefishDisplayKeeps1080pFloor() {
        QemuInstanceConfig cfg;
        cfg.mode = "cuttlefish";
        cfg.displayWidth = 800;
        cfg.displayHeight = 450;
        cfg.kernelPath = "kernel";

        const QemuBackend backend(cfg);
        const auto args = backend.buildArgs();

        QVERIFY(containsArg(args, "virtio-gpu-pci,xres=1920,yres=1080"));
        QVERIFY(!containsArg(args, "virtio-gpu-pci,xres=800,yres=450"));
    }

    void defaultsUseLowInterferenceVmSize() {
        QemuInstanceConfig cfg;
        cfg.diskImage = "guest.qcow2";

        const QemuBackend backend(cfg);
        const auto args = backend.buildArgs();

        QCOMPARE(QString::fromStdString(valueAfterArg(args, "-smp")), QStringLiteral("2"));
        QCOMPARE(QString::fromStdString(valueAfterArg(args, "-m")), QStringLiteral("2048"));
    }

    void qemuBackendDoesNotCloseAfterWaitForExit() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/instance/QemuBackend.cpp"));
        QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
        const QString source = QString::fromUtf8(file.readAll());
        QVERIFY2(!source.contains(QStringLiteral("ProcessLauncher::waitForExit(m_processHandle, 5000);\n        }\n        CloseHandle(m_processHandle);")),
                 "stop() must not CloseHandle after waitForExit already closed the handle");
        QVERIFY2(!source.contains(QStringLiteral("const int exitCode = ProcessLauncher::waitForExit(m_processHandle, 0);\n        CloseHandle(m_processHandle);")),
                 "onHealthCheck() must not CloseHandle after waitForExit already closed the handle");
    }
};

QTEST_MAIN(TestQemuBackend)
#include "test_qemu_backend.moc"
