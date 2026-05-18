#include "QmlAndroidControls.h"

#include "InputBridge.h"
#include "KeyCodes.h"
#include "LocationSimulator.h"
#include "ClipboardBridge.h"
#include "AndroidConsoleInput.h"
#include <QProcess>
#include <QUrl>
#include <algorithm>

namespace chimera {

QmlAndroidControls::QmlAndroidControls(QObject *parent)
    : QObject(parent)
{
}

bool QmlAndroidControls::back() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Back));
}

bool QmlAndroidControls::home() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Home));
}

bool QmlAndroidControls::recents() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::AppSwitch));
}

bool QmlAndroidControls::menu() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::Menu));
}

bool QmlAndroidControls::volumeUp() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::VolumeUp));
}

bool QmlAndroidControls::volumeDown() {
    return sendKey(static_cast<int>(input::AndroidKeyCode::VolumeDown));
}

bool QmlAndroidControls::sendKey(int keyCode) const {
    return input::InputBridge::instance().sendAndroidKeyCode(keyCode);
}

void QmlAndroidControls::setAdbConfig(const QString &adbExe, const QString &adbSerial) {
    m_adbExe    = adbExe;
    m_adbSerial = adbSerial;
}

void QmlAndroidControls::installApk(const QString &fileUrl) {
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        setInstallStatus(tr("安裝失敗：路徑無效"));
        return;
    }
    runAdbAsync({"-s", m_adbSerial, "install", "-r", localPath},
                tr("APK 安裝成功"),
                tr("安裝失敗"));
}

void QmlAndroidControls::adbRoot() {
    runAdbAsync({"-s", m_adbSerial, "root"},
                tr("ADB root 成功"),
                tr("ADB root 失敗（需 google_apis AVD）"));
}

void QmlAndroidControls::setGuestRotation(int degrees) {
    // Map to 0/90/180/270; default to 0 for any unrecognised value
    const int valid[] = {0, 90, 180, 270};
    const int deg = (degrees == 90 || degrees == 180 || degrees == 270) ? degrees : 0;
    (void)valid;

    // Update host-side coordinate mapper
    input::InputBridge::instance().setRotation(deg);

    // Tell Android to rotate its own UI (user_rotation: 0=0°, 1=90°, 2=180°, 3=270°)
    if (!m_adbExe.isEmpty()) {
        const int sysRot = deg / 90;  // 0→0, 90→1, 180→2, 270→3
        runAdbAsync({"-s", m_adbSerial, "shell", "settings", "put", "system",
                     "user_rotation", QString::number(sysRot)},
                    tr("旋轉已套用（%1°）").arg(deg),
                    tr("旋轉設定失敗"));
    }
}

void QmlAndroidControls::runAdbAsync(const QStringList &args,
                                     const QString &onSuccess,
                                     const QString &onFail) {
    if (m_adbExe.isEmpty()) {
        setInstallStatus(tr("ADB 尚未設定"));
        return;
    }
    // If a previous command is still running, queue this one only if it's a different process
    if (m_adbProcess) {
        if (m_adbProcess->state() != QProcess::NotRunning) {
            setInstallStatus(tr("操作中，請稍候…"));
            return;
        }
        // Disconnect old signal to avoid stale lambda
        m_adbProcess->disconnect();
    } else {
        m_adbProcess = new QProcess(this);
    }

    connect(m_adbProcess, &QProcess::finished,
            this, [this, onSuccess, onFail](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            setInstallStatus(onSuccess);
        } else {
            QString err = QString::fromUtf8(
                m_adbProcess->readAllStandardError()).trimmed().left(120);
            if (err.isEmpty())
                err = QString::fromUtf8(
                    m_adbProcess->readAllStandardOutput()).trimmed().left(120);
            setInstallStatus(onFail + (err.isEmpty() ? QString{} : (QStringLiteral("：") + err)));
        }
    });

    setInstallStatus(tr("執行中…"));
    m_adbProcess->start(m_adbExe, args);
}

void QmlAndroidControls::setGpsLocation(double lat, double lon, double altMetres) {
    integration::LocationSimulator::instance().setLocation(lat, lon, altMetres);
    if (m_gpsLat != lat || m_gpsLon != lon) {
        m_gpsLat = lat;
        m_gpsLon = lon;
        emit gpsChanged();
    }
}

void QmlAndroidControls::setConsoleInput(input::AndroidConsoleInput *consoleInput) {
    m_consoleInput = consoleInput;
}

void QmlAndroidControls::setSensor(const QString &sensorName, double x, double y, double z) {
    if (m_consoleInput)
        m_consoleInput->sendSensor(sensorName.toStdString(), x, y, z);
}

void QmlAndroidControls::setBatteryLevel(int percent) {
    if (m_consoleInput)
        m_consoleInput->sendPowerCapacity(percent);
}

void QmlAndroidControls::setBatteryStatus(const QString &status) {
    if (m_consoleInput)
        m_consoleInput->sendPowerStatus(status.toStdString());
}

void QmlAndroidControls::syncClipboardToGuest() {
    integration::ClipboardBridge::instance().syncHostToGuest();
    setInstallStatus(tr("剪貼簿已同步至 Android"));
}

void QmlAndroidControls::setInstallStatus(const QString &s) {
    if (m_installStatus != s) {
        m_installStatus = s;
        emit installStatusChanged(m_installStatus);
    }
}

} // namespace chimera
