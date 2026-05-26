#include <QTest>
#include "VirtualMachine.h"

#include <algorithm>

using namespace chimera::instance;

namespace {

bool containsArg(const std::vector<std::string> &args, const std::string &arg) {
    return std::find(args.begin(), args.end(), arg) != args.end();
}

} // namespace

class TestVirtualMachine : public QObject {
    Q_OBJECT

private slots:
    void quickBootUsesNamedSnapshot() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.quickBoot = true;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-snapshot"));
        QVERIFY(containsArg(args, "chimera_quickboot"));
        QVERIFY(!containsArg(args, "-no-snapshot"));
    }

    void quickBootCanBeDisabled() {
        VirtualMachineConfig cfg;
        cfg.name = "UnitTest";
        cfg.avdName = "chimera_dev";
        cfg.quickBoot = false;

        const VirtualMachine vm(cfg);
        const auto args = vm.buildEmulatorArgs();

        QVERIFY(containsArg(args, "-no-snapshot"));
        QVERIFY(!containsArg(args, "-snapshot"));
    }
};

QTEST_MAIN(TestVirtualMachine)
#include "test_virtual_machine.moc"
