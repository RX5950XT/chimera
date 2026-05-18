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

    // Switch adbd to root (google_apis AVDs only; requires adb root support)
    Q_INVOKABLE void adbRoot();

    // Rotate the guest display and update coordinate mapping (degrees: 0, 90, 180, 270)
    Q_INVOKABLE void setGuestRotation(int degrees);

    // Configure ADB binary + device serial (called from main.cpp after emulator starts)
    void setAdbConfig(const QString &adbExe, const QString &adbSerial);

    QString installStatus() const { return m_installStatus; }

signals:
    void installStatusChanged(const QString &status);

private:
    bool sendKey(int keyCode) const;
    void setInstallStatus(const QString &s);
    void runAdbAsync(const QStringList &args, const QString &onSuccess, const QString &onFail);

    QProcess *m_adbProcess = nullptr;
    QString   m_adbExe;
    QString   m_adbSerial;
    QString   m_installStatus;
};

} // namespace chimera
