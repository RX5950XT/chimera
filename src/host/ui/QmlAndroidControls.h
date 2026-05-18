#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

class QProcess;

namespace chimera::input { class AndroidConsoleInput; }

namespace chimera {

class QmlAndroidControls : public QObject {
    Q_OBJECT

    Q_PROPERTY(QString installStatus READ installStatus NOTIFY installStatusChanged)
    Q_PROPERTY(double  gpsLatitude   READ gpsLatitude   NOTIFY gpsChanged)
    Q_PROPERTY(double  gpsLongitude  READ gpsLongitude  NOTIFY gpsChanged)

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

    // Set GPS location in the guest (lat, lon in decimal degrees, alt in metres)
    Q_INVOKABLE void setGpsLocation(double lat, double lon, double altMetres = 0.0);

    // GPS route simulation: waypoints as [[lat,lon], ...], speedKmh in km/h
    Q_INVOKABLE void startGpsRoute(const QVariantList &waypoints, double speedKmh = 30.0);
    Q_INVOKABLE void stopGpsRoute();
    Q_INVOKABLE bool isGpsSimulating() const;

    // Clipboard: push current Windows clipboard text to the guest Android
    Q_INVOKABLE void syncClipboardToGuest();

    // Sensor simulation via Android Console protocol
    // sensorName: "acceleration" | "gyroscope" | "magneticfield" | "orientation"
    Q_INVOKABLE void setSensor(const QString &sensorName, double x, double y, double z);

    // Battery simulation
    Q_INVOKABLE void setBatteryLevel(int percent);             // 0–100
    Q_INVOKABLE void setBatteryStatus(const QString &status);  // "charging" | "discharging" | "full"

    // File sharing: host → /sdcard/Download/ via ADB push
    Q_INVOKABLE void pushFileToGuest(const QString &fileUrl);
    // File sharing: /sdcard/Download/<guestFilename> → host dir (empty = Downloads)
    Q_INVOKABLE void pullFileFromGuest(const QString &guestFilename);

    // Eco mode: lower emulator process priority when minimized (saves CPU)
    Q_INVOKABLE void setEcoMode(bool enabled);

    // Runtime display controls (take effect immediately via ADB)
    Q_INVOKABLE void setScreenDensity(int dpi);   // adb shell wm density <dpi>
    Q_INVOKABLE void resetScreenDensity();         // adb shell wm density reset
    Q_INVOKABLE void setScreenSize(int width, int height); // adb shell wm size <w>x<h>
    Q_INVOKABLE void resetScreenSize();            // adb shell wm size reset
    Q_INVOKABLE void setScreenBrightness(int level); // 0–255

    // System controls
    Q_INVOKABLE void setAirplaneMode(bool enabled);

    // App management
    Q_INVOKABLE QStringList listInstalledPackages();  // user-installed packages (3rd-party)
    Q_INVOKABLE void launchPackage(const QString &packageName);
    Q_INVOKABLE void forceStopPackage(const QString &packageName);
    Q_INVOKABLE void uninstallPackage(const QString &packageName);
    Q_INVOKABLE void clearPackageData(const QString &packageName);

    // Configure ADB binary + device serial (called from main.cpp after emulator starts)
    void setAdbConfig(const QString &adbExe, const QString &adbSerial);
    // Wire emulator PID for eco mode
    void setEmulatorPid(uint32_t pid);

    // Wire Android Console input for sensor/battery commands
    void setConsoleInput(input::AndroidConsoleInput *consoleInput);

    QString installStatus() const { return m_installStatus; }
    double  gpsLatitude()   const { return m_gpsLat; }
    double  gpsLongitude()  const { return m_gpsLon; }

signals:
    void installStatusChanged(const QString &status);
    void gpsChanged();
    void notificationRequested(const QString &title, const QString &message);

private:
    bool sendKey(int keyCode) const;
    void setInstallStatus(const QString &s);
    void runAdbAsync(const QStringList &args, const QString &onSuccess, const QString &onFail);

    QProcess *m_adbProcess   = nullptr;
    input::AndroidConsoleInput *m_consoleInput = nullptr;
    QString   m_adbExe;
    QString   m_adbSerial;
    QString   m_installStatus;
    double    m_gpsLat = 0.0;
    double    m_gpsLon = 0.0;
    uint32_t  m_emulatorPid = 0;
};

} // namespace chimera
