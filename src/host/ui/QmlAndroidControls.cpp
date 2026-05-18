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
    runAdbAsync({"-s", m_adbSerial, "install", "-r", localPath},
                tr("APK 安裝成功"),
                tr("安裝失敗"));
}

void QmlAndroidControls::adbRoot() {
    runAdbAsync({"-s", m_adbSerial, "root"},
                tr("ADB root 成功"),
                tr("ADB root 失敗（需 google_apis AVD）"));
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

void QmlAndroidControls::setInstallStatus(const QString &s) {
    if (m_installStatus != s) {
        m_installStatus = s;
        emit installStatusChanged(m_installStatus);
    }
}

} // namespace chimera
