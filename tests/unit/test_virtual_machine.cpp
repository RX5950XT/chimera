#include <QTest>
#include "VirtualMachine.h"

#include <algorithm>
#include <iterator>

using namespace chimera::instance;

namespace {

bool containsArg(const std::vector<std::string> &args, const std::string &arg) {
    return std::find(args.begin(), args.end(), arg) != args.end();
}

std::string argAfter(const std::vector<std::string> &args, const std::string &arg) {
    const auto it = std::find(args.begin(), args.end(), arg);
    if (it == args.end() || std::next(it) == args.end()) return {};
    return *std::next(it);
}

} // namespace

class TestVirtualMachine : public QObject {
    Q_OBJECT

private slots:
    void quickBootDefaultsToFullBoot() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-no-snapstorage"));
        QVERIFY(containsArg(args, "-no-snapshot"));
        QVERIFY(containsArg(args, "-no-snapshot-load"));
        QVERIFY(containsArg(args, "-no-snapshot-save"));
        QVERIFY(!containsArg(args, "-snapshot"));
    }

    void quickBootUsesNamedSnapshot() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.quickBoot = true;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-snapshot"));
        QVERIFY(containsArg(args, "chimera_quickboot"));
        QVERIFY(containsArg(args, "-no-snapshot-save"));
        QVERIFY(!containsArg(args, "-no-snapshot"));
    }

    void quickBootCanBeDisabled() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.quickBoot = false;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-no-snapstorage"));
        QVERIFY(containsArg(args, "-no-snapshot"));
        QVERIFY(containsArg(args, "-no-snapshot-load"));
        QVERIFY(containsArg(args, "-no-snapshot-save"));
        QVERIFY(!containsArg(args, "-snapshot"));
    }

    void emulatorWindowSizeKeeps1080pFloor() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.width = 800;
        cfg.height = 450;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QCOMPARE(QString::fromStdString(argAfter(args, "-window-size")),
                 QStringLiteral("1920x1080"));
        QVERIFY(!containsArg(args, "800x450"));
    }

    void emulatorDefaultsToHeadlessWindowless() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-no-window"));
    }

    void visibleWindowRequiresExplicitUnsafeAllowance() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.headless = false;
        cfg.allowVisibleEmulatorWindow = false;

        const VirtualMachine blockedVm(cfg);
        QVERIFY(containsArg(blockedVm.buildEmulatorArgs(), "-no-window"));

        cfg.allowVisibleEmulatorWindow = true;
        const VirtualMachine unsafeVm(cfg);
        QVERIFY(!containsArg(unsafeVm.buildEmulatorArgs(), "-no-window"));
    }

    void classicEmuglRuntimeUsesCompatibleFlags() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.headless = true;
        cfg.useClassicEmuglRuntime = true;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-no-window"));
        QVERIFY(containsArg(args, "-ports"));
        QVERIFY(containsArg(args, "-no-audio"));
        QVERIFY(!containsArg(args, "-grpc"));
        QVERIFY(!containsArg(args, "-idle-grpc-timeout"));
        QVERIFY(!containsArg(args, "-window-size"));
        QVERIFY(!containsArg(args, "-fixed-scale"));
        QVERIFY(!containsArg(args, "-vsync-rate"));
        QVERIFY(!containsArg(args, "-no-metrics"));
        QVERIFY(!containsArg(args, "-crash-report-mode"));
    }
};

QTEST_MAIN(TestVirtualMachine)
#include "test_virtual_machine.moc"
