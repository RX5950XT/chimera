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
    void sharedTextureKeepsGrpcFallbackWired() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/ui/main.cpp"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open src/host/ui/main.cpp");
        const QString source = QString::fromUtf8(file.readAll());
        const int start = source.indexOf(QStringLiteral("if (!hcsBackendMode && !qemuBackendMode && !noEmulator"));
        QVERIFY2(start >= 0, "gRPC capture wiring condition not found");
        const int end = source.indexOf(QStringLiteral(") {"), start);
        QVERIFY2(end > start, "gRPC capture wiring condition end not found");
        const QString condition = source.mid(start, end - start);
        QVERIFY2(!condition.contains(QStringLiteral("!sharedTextureCapture")),
                 "shared texture mode must keep gRPC fallback wired so a stalled producer does not freeze the visible guest");
    }

    void stalledSharedTextureRevivesGrpcCapture() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/ui/main.cpp"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open src/host/ui/main.cpp");
        const QString source = QString::fromUtf8(file.readAll());
        const int watchdog = source.indexOf(QStringLiteral("kSharedTextureStallMs"));
        QVERIFY2(watchdog >= 0, "shared texture stall watchdog not found");
        // Without a revival path a stalled gfxstream producer freezes the host frame,
        // which is indistinguishable from dead input (the reported bug).
        const int start = source.indexOf(QStringLiteral("grpcCapture->start()"), watchdog);
        QVERIFY2(start > watchdog, "stall watchdog must restart the gRPC capture");
        QVERIFY2(source.indexOf(QStringLiteral("lastSharedTextureFrameMs"), watchdog) > watchdog,
                 "stall watchdog must key off the last shared texture frame timestamp");
    }
};

QTEST_MAIN(TestUiMainPortContract)
#include "test_ui_main_port_contract.moc"
