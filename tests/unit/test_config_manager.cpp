#include <QTest>
#include "ConfigManager.h"

using namespace chimera::config;

class TestConfigManager : public QObject {
    Q_OBJECT

private slots:
    void testSingleton() {
        auto &a = ConfigManager::instance();
        auto &b = ConfigManager::instance();
        QVERIFY(&a == &b);
    }

    void testSetGet() {
        auto &cfg = ConfigManager::instance();
        cfg.setString("test.key", "value");
        QCOMPARE(cfg.getString("test.key"), std::string("value"));

        cfg.setInt("test.number", 42);
        QCOMPARE(cfg.getInt("test.number"), 42);

        cfg.setBool("test.flag", true);
        QVERIFY(cfg.getBool("test.flag"));
    }

    void testInstanceCRUD() {
        auto &cfg = ConfigManager::instance();
        cfg.createInstance("TestInstance");
        QCOMPARE(cfg.getInt("instance.TestInstance.cpus"), 4);
        QCOMPARE(cfg.getInt("instance.TestInstance.ram"), 4096);

        auto names = cfg.listInstances();
        QVERIFY(std::find(names.begin(), names.end(), "TestInstance") != names.end());

        cfg.removeInstance("TestInstance");
        names = cfg.listInstances();
        QVERIFY(std::find(names.begin(), names.end(), "TestInstance") == names.end());
    }
};

QTEST_MAIN(TestConfigManager)
#include "test_config_manager.moc"
