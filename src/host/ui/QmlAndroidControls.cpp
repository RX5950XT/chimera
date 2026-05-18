#include "QmlAndroidControls.h"

#include "InputBridge.h"
#include "KeyCodes.h"
#include "LocationSimulator.h"
#include "ClipboardBridge.h"
#include "AndroidConsoleInput.h"
#include <QProcess>
#include <QUrl>
#include <QFileInfo>
#include <QFile>
#include <QDateTime>
#include <QStandardPaths>
#include <algorithm>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

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
    const QString apkName = QFileInfo(localPath).fileName();
    if (m_adbExe.isEmpty()) { setInstallStatus(tr("ADB 尚未設定")); return; }
    if (m_adbProcess) {
        if (m_adbProcess->state() != QProcess::NotRunning) {
            setInstallStatus(tr("操作中，請稍候…"));
            return;
        }
        m_adbProcess->disconnect();
    } else {
        m_adbProcess = new QProcess(this);
    }
    connect(m_adbProcess, &QProcess::finished, this,
            [this, apkName](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            setInstallStatus(tr("APK 安裝成功"));
            emit notificationRequested(tr("Chimera"), tr("已安裝：") + apkName);
        } else {
            QString err = QString::fromUtf8(m_adbProcess->readAllStandardError()).trimmed().left(120);
            if (err.isEmpty()) err = QString::fromUtf8(m_adbProcess->readAllStandardOutput()).trimmed().left(120);
            setInstallStatus(tr("安裝失敗") + (err.isEmpty() ? QString{} : (QStringLiteral("：") + err)));
        }
    });
    setInstallStatus(tr("安裝中…"));
    m_adbProcess->start(m_adbExe, {"-s", m_adbSerial, "install", "-r", localPath});
}

void QmlAndroidControls::installObb(const QString &fileUrl, const QString &packageName) {
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) { setInstallStatus(tr("OBB 路徑無效")); return; }
    if (packageName.isEmpty()) { setInstallStatus(tr("OBB 需指定 package 名稱")); return; }
    if (m_adbExe.isEmpty()) { setInstallStatus(tr("ADB 尚未設定")); return; }

    const QString obbName   = QFileInfo(localPath).fileName();
    const QString obbDir    = "/sdcard/Android/obb/" + packageName;
    const QString guestPath = obbDir + "/" + obbName;

    // Use two independent one-shot processes (never touches m_adbProcess) to avoid
    // contention with concurrent runAdbAsync calls and prevent silent push drop.
    auto *mkdirProc = new QProcess(this);
    auto *pushProc  = new QProcess(this);

    connect(mkdirProc, &QProcess::finished, this,
            [this, mkdirProc, pushProc, localPath, guestPath, obbName](int, QProcess::ExitStatus) {
        mkdirProc->deleteLater();
        connect(pushProc, &QProcess::finished, this,
                [this, pushProc, obbName](int exitCode, QProcess::ExitStatus) {
            pushProc->deleteLater();
            if (exitCode == 0) {
                setInstallStatus(tr("OBB 已安裝：") + obbName);
                emit notificationRequested(tr("Chimera"), tr("已安裝 OBB：") + obbName);
            } else {
                QString err = QString::fromUtf8(pushProc->readAllStandardError()).trimmed().left(120);
                setInstallStatus(tr("OBB 安裝失敗：") + obbName +
                    (err.isEmpty() ? QString{} : QStringLiteral("：") + err));
            }
        });
        pushProc->start(m_adbExe, {"-s", m_adbSerial, "push", localPath, guestPath});
    });
    setInstallStatus(tr("OBB 安裝中…"));
    mkdirProc->start(m_adbExe, {"-s", m_adbSerial, "shell", "mkdir", "-p", obbDir});
}

void QmlAndroidControls::adbRoot() {
    runAdbAsync({"-s", m_adbSerial, "root"},
                tr("ADB root 成功"),
                tr("ADB root 失敗（需 google_apis AVD）"));
}

void QmlAndroidControls::rebootGuest() {
    runAdbAsync({"-s", m_adbSerial, "reboot"},
                tr("Android 重啟中…"),
                tr("重啟失敗"));
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
    if (!m_consoleInput || !m_consoleInput->isConnected()) {
        setInstallStatus(tr("Console 未連線，感應器注入無效"));
        return;
    }
    if (m_consoleInput->sendSensor(sensorName.toStdString(), x, y, z))
        setInstallStatus(tr("感應器已注入：") + sensorName);
    else
        setInstallStatus(tr("感應器注入失敗"));
}

void QmlAndroidControls::setBatteryLevel(int percent) {
    if (!m_consoleInput || !m_consoleInput->isConnected()) {
        setInstallStatus(tr("Console 未連線，電池模擬無效"));
        return;
    }
    if (m_consoleInput->sendPowerCapacity(percent))
        setInstallStatus(tr("電池電量已設定：%1%").arg(percent));
    else
        setInstallStatus(tr("電池電量設定失敗"));
}

void QmlAndroidControls::setBatteryStatus(const QString &status) {
    if (!m_consoleInput || !m_consoleInput->isConnected()) {
        setInstallStatus(tr("Console 未連線，電池狀態無效"));
        return;
    }
    if (m_consoleInput->sendPowerStatus(status.toStdString()))
        setInstallStatus(tr("電池狀態已設定：") + status);
    else
        setInstallStatus(tr("電池狀態設定失敗"));
}

void QmlAndroidControls::pushFileToGuest(const QString &fileUrl) {
    const QString localPath = QUrl(fileUrl).toLocalFile();
    if (localPath.isEmpty()) {
        setInstallStatus(tr("檔案路徑無效"));
        return;
    }
    const QString name = QFileInfo(localPath).fileName();
    runAdbAsync({"-s", m_adbSerial, "push", localPath, "/sdcard/Download/" + name},
                tr("已推送：") + name + tr(" → /sdcard/Download/"),
                tr("推送失敗：") + name);
}

void QmlAndroidControls::startGpsRoute(const QVariantList &waypoints, double speedKmh) {
    std::vector<integration::GeoPoint> route;
    route.reserve(static_cast<size_t>(waypoints.size()));
    for (const QVariant &pt : waypoints) {
        const QVariantList ll = pt.toList();
        if (ll.size() < 2) continue;
        route.push_back({ll[0].toDouble(), ll[1].toDouble(), ll.size() > 2 ? ll[2].toDouble() : 0.0});
    }
    if (route.size() < 2) {
        setInstallStatus(tr("路線模擬需至少 2 個航點"));
        return;
    }
    const double speedMps = speedKmh / 3.6;
    integration::LocationSimulator::instance().loadRoute(route);
    integration::LocationSimulator::instance().startSimulation(speedMps);
    setInstallStatus(tr("GPS 路線模擬已啟動（%1 km/h）").arg(static_cast<int>(speedKmh)));
    emit gpsChanged();
}

void QmlAndroidControls::stopGpsRoute() {
    integration::LocationSimulator::instance().stopSimulation();
    setInstallStatus(tr("GPS 路線模擬已停止"));
    emit gpsChanged();
}

bool QmlAndroidControls::isGpsSimulating() const {
    return integration::LocationSimulator::instance().isSimulating();
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

void QmlAndroidControls::setEmulatorPid(uint32_t pid) {
    m_emulatorPid = pid;
}

void QmlAndroidControls::pullFileFromGuest(const QString &guestFilename) {
    if (guestFilename.isEmpty()) return;
    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString guestPath = "/sdcard/Download/" + guestFilename;
    runAdbAsync({"-s", m_adbSerial, "pull", guestPath, destDir},
                tr("已拉取：") + guestFilename + tr(" → ") + destDir,
                tr("拉取失敗：") + guestFilename);
}

void QmlAndroidControls::refreshInstalledPackages() {
    if (m_adbExe.isEmpty() || m_adbSerial.isEmpty()) return;
    setInstallStatus(tr("取得應用程式清單…"));
    auto *proc = new QProcess(this);
    proc->setProgram(m_adbExe);
    proc->setArguments({"-s", m_adbSerial, "shell", "pm", "list", "packages", "-3"});
    connect(proc, &QProcess::finished, this, [this, proc](int, QProcess::ExitStatus) {
        proc->deleteLater();
        QStringList packages;
        const QString output = QString::fromUtf8(proc->readAllStandardOutput());
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const QString pkg = line.trimmed();
            if (pkg.startsWith(QLatin1String("package:")))
                packages.append(pkg.mid(8));
        }
        m_installedPackages = packages;
        emit installedPackagesChanged();
        setInstallStatus(tr("共 %1 個應用程式").arg(packages.size()));
    });
    proc->start();
}

QStringList QmlAndroidControls::listInstalledPackages() {
    return m_installedPackages;
}

void QmlAndroidControls::refreshGuestDownloads() {
    if (m_adbExe.isEmpty() || m_adbSerial.isEmpty()) return;
    auto *proc = new QProcess(this);
    proc->setProgram(m_adbExe);
    proc->setArguments({"-s", m_adbSerial, "shell", "ls", "/sdcard/Download/"});
    connect(proc, &QProcess::finished, this, [this, proc](int exitCode, QProcess::ExitStatus) {
        proc->deleteLater();
        if (exitCode != 0) return;
        QStringList files;
        const QString output = QString::fromUtf8(proc->readAllStandardOutput());
        for (const QString &line : output.split(QLatin1Char('\n'))) {
            const QString f = line.trimmed();
            if (!f.isEmpty())
                files.append(f);
        }
        m_guestDownloads = files;
        emit guestDownloadsChanged();
    });
    proc->start();
}

void QmlAndroidControls::launchPackage(const QString &packageName) {
    runAdbAsync({"-s", m_adbSerial, "shell", "monkey",
                 "-p", packageName, "-c", "android.intent.category.LAUNCHER", "1"},
                tr("已啟動：") + packageName,
                tr("啟動失敗：") + packageName);
}

void QmlAndroidControls::forceStopPackage(const QString &packageName) {
    runAdbAsync({"-s", m_adbSerial, "shell", "am", "force-stop", packageName},
                tr("已強制停止：") + packageName,
                tr("停止失敗：") + packageName);
}

void QmlAndroidControls::takeScreenshot() {
    if (m_adbExe.isEmpty()) { setInstallStatus(tr("ADB 尚未設定")); return; }
    const QString destDir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    const QString ts = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    const QString destPath = destDir + "/chimera_" + ts + ".png";

    if (m_adbProcess) {
        if (m_adbProcess->state() != QProcess::NotRunning) {
            setInstallStatus(tr("操作中，請稍候…"));
            return;
        }
        m_adbProcess->disconnect();
    } else {
        m_adbProcess = new QProcess(this);
    }

    connect(m_adbProcess, &QProcess::finished, this,
            [this, destPath](int exitCode, QProcess::ExitStatus) {
        if (exitCode == 0) {
            const QByteArray png = m_adbProcess->readAllStandardOutput();
            QFile f(destPath);
            if (f.open(QIODevice::WriteOnly) && f.write(png) == png.size()) {
                const QString name = QFileInfo(destPath).fileName();
                setInstallStatus(tr("截圖已儲存：") + name);
                emit notificationRequested(tr("Chimera"), tr("截圖已儲存：") + name);
            } else {
                setInstallStatus(tr("截圖儲存失敗"));
            }
        } else {
            setInstallStatus(tr("截圖失敗"));
        }
    });

    setInstallStatus(tr("截圖中…"));
    m_adbProcess->start(m_adbExe, {"-s", m_adbSerial, "exec-out", "screencap", "-p"});
}

QString QmlAndroidControls::screenshotDir() const {
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void QmlAndroidControls::uninstallPackage(const QString &packageName) {
    runAdbAsync({"-s", m_adbSerial, "uninstall", packageName},
                tr("已解除安裝：") + packageName,
                tr("解除安裝失敗：") + packageName);
}

void QmlAndroidControls::clearPackageData(const QString &packageName) {
    runAdbAsync({"-s", m_adbSerial, "shell", "pm", "clear", packageName},
                tr("已清除資料：") + packageName,
                tr("清除資料失敗：") + packageName);
}

void QmlAndroidControls::setScreenDensity(int dpi) {
    if (dpi < 72 || dpi > 640) return;
    runAdbAsync({"-s", m_adbSerial, "shell", "wm", "density", QString::number(dpi)},
                tr("螢幕密度已設為 %1 DPI").arg(dpi),
                tr("無法設定螢幕密度"));
}

void QmlAndroidControls::resetScreenDensity() {
    runAdbAsync({"-s", m_adbSerial, "shell", "wm", "density", "reset"},
                tr("螢幕密度已重置"),
                tr("無法重置螢幕密度"));
}

void QmlAndroidControls::setScreenSize(int width, int height) {
    if (width < 320 || height < 320) return;
    runAdbAsync({"-s", m_adbSerial, "shell", "wm", "size",
                 QString("%1x%2").arg(width).arg(height)},
                tr("解析度已設為 %1×%2").arg(width).arg(height),
                tr("無法設定解析度"));
}

void QmlAndroidControls::resetScreenSize() {
    runAdbAsync({"-s", m_adbSerial, "shell", "wm", "size", "reset"},
                tr("解析度已重置"),
                tr("無法重置解析度"));
}

void QmlAndroidControls::setScreenBrightness(int level) {
    if (m_adbExe.isEmpty()) return;
    const int clamped = qBound(0, level, 255);
    // Chain: first disable auto-brightness, then set level (two independent one-shot processes).
    auto *p1 = new QProcess(this);
    auto *p2 = new QProcess(this);
    connect(p1, &QProcess::finished, this, [this, p1, p2, clamped](int, QProcess::ExitStatus) {
        p1->deleteLater();
        connect(p2, &QProcess::finished, this, [this, p2, clamped](int exitCode, QProcess::ExitStatus) {
            p2->deleteLater();
            if (exitCode == 0)
                setInstallStatus(tr("亮度已設為 %1").arg(clamped));
            else
                setInstallStatus(tr("無法設定亮度"));
        });
        p2->start(m_adbExe, {"-s", m_adbSerial, "shell", "settings", "put", "system",
                              "screen_brightness", QString::number(clamped)});
    });
    p1->start(m_adbExe, {"-s", m_adbSerial, "shell", "settings", "put", "system",
                          "screen_brightness_mode", "0"});
}

void QmlAndroidControls::setAirplaneMode(bool enabled) {
    const QString val = enabled ? "enable" : "disable";
    runAdbAsync({"-s", m_adbSerial, "shell", "cmd", "connectivity",
                 "airplane-mode", val},
                enabled ? tr("飛行模式已開啟") : tr("飛行模式已關閉"),
                tr("無法切換飛行模式"));
}

void QmlAndroidControls::setEcoMode(bool enabled) {
#ifdef _WIN32
    if (m_emulatorPid == 0) return;
    HANDLE hProc = OpenProcess(PROCESS_SET_INFORMATION, FALSE, m_emulatorPid);
    if (!hProc) return;
    SetPriorityClass(hProc, enabled ? BELOW_NORMAL_PRIORITY_CLASS : HIGH_PRIORITY_CLASS);
    CloseHandle(hProc);
#else
    Q_UNUSED(enabled)
#endif
}

} // namespace chimera
