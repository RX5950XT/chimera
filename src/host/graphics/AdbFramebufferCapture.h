#pragma once

#include "FramebufferCapture.h"
#include <QProcess>
#include <QTimer>
#include <QString>

namespace chimera::graphics {

/**
 * @brief Frame capture via ADB screencap command.
 *
 * Uses QProcess to invoke `adb exec-out screencap` periodically.
 * Supports both PNG and raw formats; raw is faster.
 */
class AdbFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    explicit AdbFramebufferCapture(const QString &adbPath, int adbPort = 5555,
                                    bool usePng = false, QObject *parent = nullptr);
    ~AdbFramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QString backendName() const override { return QStringLiteral("adb-screencap"); }

    void setAdbPort(int port) { m_adbPort = port; }

private slots:
    void onCaptureTimeout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);

private:
    QString m_adbPath;
    int m_adbPort = 5555;
    bool m_usePng = false;
    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    bool m_running = false;

    QImage decodeRawFrame(const QByteArray &data);
};

} // namespace chimera::graphics
