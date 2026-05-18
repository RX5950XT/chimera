#include "QmlAndroidControls.h"

#include "InputBridge.h"
#include "KeyCodes.h"
#include <QProcess>
#include <QUrl>

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
    if (m_adbExe.isEmpty()) {
        setInstallStatus(tr("安裝失敗：ADB 尚未設定"));
        return;
    }
    if (m_installProcess && m_installProcess->state() != QProcess::NotRunning) {
        setInstallStatus(tr("安裝中，請稍候…"));
        return;
    }
    if (!m_installProcess) {
        m_installProcess = new QProcess(this);
        connect(m_installProcess, &QProcess::finished,
                this, [this](int exitCode, QProcess::ExitStatus) {
            if (exitCode == 0) {
                setInstallStatus(tr("APK 安裝成功"));
            } else {
                QString err = QString::fromUtf8(
                    m_installProcess->readAllStandardError()).trimmed().left(120);
                if (err.isEmpty())
                    err = QString::fromUtf8(
                        m_installProcess->readAllStandardOutput()).trimmed().left(120);
                setInstallStatus(tr("安裝失敗：") + err);
            }
        });
    }
    setInstallStatus(tr("正在安裝…"));
    const QStringList args = {"-s", m_adbSerial, "install", "-r", localPath};
    m_installProcess->start(m_adbExe, args);
}

void QmlAndroidControls::setInstallStatus(const QString &s) {
    if (m_installStatus != s) {
        m_installStatus = s;
        emit installStatusChanged(m_installStatus);
    }
}

} // namespace chimera
