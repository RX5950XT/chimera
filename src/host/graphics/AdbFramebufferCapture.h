#pragma once

#include "FramebufferCapture.h"
#include <QProcess>
#include <QTimer>
#include <QString>
#include <QByteArray>
#include <QElapsedTimer>

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
    static QImage decodeRawFrameData(const QByteArray &data);

private slots:
    void onCaptureTimeout();
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onReadyRead();
    void onWatchdogTimeout();

private:
    void startRawStream();
    void startSingleCapture();
    void restartRawStream(const QString &reason);
    void parseRawStream();

    QString m_adbPath;
    int m_adbPort = 5555;
    bool m_usePng = false;
    QProcess *m_process = nullptr;
    QTimer *m_timer = nullptr;
    QTimer *m_watchdogTimer = nullptr;
    QElapsedTimer m_lastFrameTimer;
    QByteArray m_readBuffer;
    bool m_running = false;
    bool m_restartPending = false;

    static QImage decodeRawFrameBytes(const char *data, qsizetype size);
};

} // namespace chimera::graphics
