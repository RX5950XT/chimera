#include <QtTest/QtTest>
#include <QFile>
#include <QString>

class TestRuntimeSourceContract : public QObject {
    Q_OBJECT

private slots:
    void proxyD3D11SharedHandleUsesDxgiSharedAccess() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/runtime/gfxstream_proxy/gfxstream_proxy_d3d11.cpp"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open gfxstream_proxy_d3d11.cpp");
        const QString source = QString::fromUtf8(file.readAll());
        const int call = source.indexOf(QStringLiteral("CreateSharedHandle"));
        QVERIFY2(call >= 0, "CreateSharedHandle call not found");
        // The call's access flag may sit on the following line, so inspect the
        // argument window up to the closing paren rather than a single line.
        const int callEnd = source.indexOf(QStringLiteral(");"), call);
        QVERIFY2(callEnd > call, "CreateSharedHandle call end not found");
        const QString line = source.mid(call, callEnd - call);
        QVERIFY2(line.contains(QStringLiteral("DXGI_SHARED_RESOURCE_READ")),
                 "proxy shared texture handle must be openable by the DXGI consumer");
        QVERIFY2(line.contains(QStringLiteral("DXGI_SHARED_RESOURCE_WRITE")),
                 "proxy shared texture handle must allow producer writes");
        QVERIFY2(!line.contains(QStringLiteral("GENERIC_ALL")),
                 "GENERIC_ALL makes OpenSharedResourceByName fail with E_INVALIDARG on this path");
    }

    void virtualMachineExitMonitorClosesHandleExactlyOnce() {
        QFile file(QStringLiteral(CHIMERA_SOURCE_DIR) + QStringLiteral("/src/host/instance/VirtualMachine.cpp"));
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), "could not open VirtualMachine.cpp");
        const QString source = QString::fromUtf8(file.readAll());

        // The exit-monitor must dispose the process handle, not leak it by nulling.
        const int log = source.indexOf(QStringLiteral("Emulator process tree exited unexpectedly"));
        QVERIFY2(log >= 0, "exit-monitor diagnostic not found");
        const int setErr = source.indexOf(QStringLiteral("setState(VMState::Error)"), log);
        QVERIFY2(setErr > log, "exit-monitor Error transition not found");
        const QString block = source.mid(log, setErr - log);
        QVERIFY2(block.contains(QStringLiteral("InterlockedExchangePointer(&m_processHandle")),
                 "exit-monitor must atomically claim the handle to avoid a double-close race with stop()");
        QVERIFY2(block.contains(QStringLiteral("CloseHandle")),
                 "exit-monitor must close the claimed handle instead of leaking it");

        // stop() must also claim atomically so it cannot dispose a handle the monitor owns.
        const int stop = source.indexOf(QStringLiteral("bool VirtualMachine::stop()"));
        QVERIFY2(stop >= 0, "stop() not found");
        const int stopEnd = source.indexOf(QStringLiteral("joinExitMonitor"), stop);
        QVERIFY2(stopEnd > stop, "stop() body not found");
        QVERIFY2(source.mid(stop, stopEnd - stop).contains(QStringLiteral("InterlockedExchangePointer(&m_processHandle")),
                 "stop() must atomically claim the handle");
    }
};

QTEST_MAIN(TestRuntimeSourceContract)
#include "test_runtime_source_contract.moc"
