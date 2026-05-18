#include <QtTest>
#include "DeviceSpoofer.h"

using namespace chimera::instance;

class TestDeviceSpoofer : public QObject {
    Q_OBJECT

private slots:
    void builtinProfilesNotEmpty() {
        const auto profiles = DeviceSpoofer::getBuiltinProfiles();
        QVERIFY(profiles.size() >= 5);
    }

    void builtinProfilesHaveNames() {
        const auto profiles = DeviceSpoofer::getBuiltinProfiles();
        for (const auto &p : profiles) {
            QVERIFY(!p.name.empty());
            QVERIFY(!p.manufacturer.empty());
            QVERIFY(!p.model.empty());
        }
    }

    void builtinProfilesHaveValidDpi() {
        const auto profiles = DeviceSpoofer::getBuiltinProfiles();
        for (const auto &p : profiles) {
            QVERIFY(p.dpi > 0);
            QVERIFY(p.screenWidth > 0);
            QVERIFY(p.screenHeight > 0);
        }
    }

    void applyProfileWithNonExistentAvdReturnsFalse() {
        auto &s = DeviceSpoofer::instance();
        const auto profiles = DeviceSpoofer::getBuiltinProfiles();
        QVERIFY(!profiles.empty());
        // Non-existent AVD → should return false without crashing
        const bool ok = s.applyProfile(profiles[0], "__nonexistent_avd_test__");
        QVERIFY(!ok);
    }

    void readBuildPropWithNonExistentAvdReturnsEmpty() {
        auto &s = DeviceSpoofer::instance();
        const auto result = s.readBuildProp("__nonexistent_avd_test__");
        QVERIFY(result.empty());
    }
};

QTEST_MAIN(TestDeviceSpoofer)
#include "test_device_spoofer.moc"
