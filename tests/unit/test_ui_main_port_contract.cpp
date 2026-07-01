#include <QtTest/QtTest>
#include <QFile>
#include <QString>

class TestUiMainPortContract : public QObject {
    Q_OBJECT

private slots:
    void adbDisplayFallbackUsesDerivedAdbPort() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/ui/main.cpp"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open src/host/ui/main.cpp");
        const QString source = QString::fromUtf8(file.readAll());
        const int flag = source.indexOf(QStringLiteral("--adb-display-fallback"));
        QVERIFY2(flag >= 0, "adb display fallback block not found");
        const int ctor = source.indexOf(QStringLiteral("AdbFramebufferCapture"), flag);
        QVERIFY2(ctor >= 0, "AdbFramebufferCapture wiring not found");
        const int end = source.indexOf(QStringLiteral("wireCapture(adbCapture"), ctor);
        QVERIFY2(end > ctor, "adb fallback wireCapture not found");
        const QString block = source.mid(ctor, end - ctor);
        QVERIFY2(block.contains(QStringLiteral("g_runtimeCfg.adbPort")),
                 "adb display fallback must use derived adb port");
        QVERIFY2(!block.contains(QStringLiteral(", 5555,")),
                 "adb display fallback must not hardcode emulator-5554/ADB 5555");
    }
};

QTEST_MAIN(TestUiMainPortContract)
#include "test_ui_main_port_contract.moc"
