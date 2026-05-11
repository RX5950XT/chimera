#include <QTest>
#include "InstanceManager.h"
#include <filesystem>

using namespace chimera::instance;

class TestInstanceManager : public QObject {
    Q_OBJECT

private slots:
    void testCreateInstance() {
        auto &mgr = InstanceManager::instance();
        InstanceConfig cfg;
        cfg.name = "UnitTest";
        cfg.cpus = 2;
        cfg.ramMB = 2048;
        cfg.dataDir = std::filesystem::temp_directory_path() / "chimera_test_instance";

        QVERIFY(mgr.createInstance(cfg));
        auto names = mgr.listInstances();
        QVERIFY(std::find(names.begin(), names.end(), "UnitTest") != names.end());

        auto state = mgr.getInstanceState("UnitTest");
        QCOMPARE(state, VMState::Created);

        mgr.deleteInstance("UnitTest");
    }
};

QTEST_MAIN(TestInstanceManager)
#include "test_instance_manager.moc"
