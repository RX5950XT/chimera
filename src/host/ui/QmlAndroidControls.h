#pragma once

#include <QObject>
#include <QString>

class QProcess;

namespace chimera {

class QmlAndroidControls : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installStatusChanged)

public:
    explicit QmlAndroidControls(QObject *parent = nullptr);

    // Android navigation
    Q_INVOKABLE bool back();
    Q_INVOKABLE bool home();
    Q_INVOKABLE bool recents();
    Q_INVOKABLE bool menu();
    Q_INVOKABLE bool volumeUp();
    Q_INVOKABLE bool volumeDown();

    // APK installation (async; monitor installStatus property for result)
    Q_INVOKABLE void installApk(const QString &fileUrl);

    // Configure ADB binary + device serial (called from main.cpp after emulator starts)
    void setAdbConfig(const QString &adbExe, const QString &adbSerial);

    QString installStatus() const { return m_installStatus; }

signals:
    void installStatusChanged(const QString &status);

private:
    bool sendKey(int keyCode) const;
    void setInstallStatus(const QString &s);

    QProcess *m_installProcess = nullptr;
    QString   m_adbExe;
    QString   m_adbSerial;
    QString   m_installStatus;
};

} // namespace chimera
