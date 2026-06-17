#pragma once

#include "FramebufferCapture.h"
#include "SharedD3D11TexturePublisher.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <QStringList>
#include <memory>

namespace chimera::graphics {

class AdbH264FramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    AdbH264FramebufferCapture(QString adbPath,
                              QString ffmpegPath,
                              QString serial,
                              int requestedWidth,
                              int requestedHeight,
                              QObject *parent = nullptr);
    ~AdbH264FramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QString backendName() const override { return QStringLiteral("ADB-H264"); }

    static QSize normalizedCaptureSize(int requestedWidth, int requestedHeight);
    static QStringList buildAdbArgs(const QString &serial, const QSize &size, int bitRate);
    static QStringList buildFfmpegArgs(const QSize &size);

private slots:
    void onAdbStderr();
    void onFfmpegReadyRead();
    void onFfmpegStderr();
    void onAdbFinished(int exitCode, QProcess::ExitStatus status);
    void onFfmpegFinished(int exitCode, QProcess::ExitStatus status);

private:
    void startProcesses();
    void stopProcesses();
    void restartSoon(const QString &reason);
    void parseRawFrames();
    void publishFrame(const char *data, qsizetype frameBytes);

    QString m_adbPath;
    QString m_ffmpegPath;
    QString m_serial;
    QSize m_size;
    int m_bitRate = 24000000;
    bool m_running = false;
    bool m_stopping = false;
    bool m_restartPending = false;
    QProcess *m_adb = nullptr;
    QProcess *m_ffmpeg = nullptr;
    QByteArray m_rawBuffer;
    QByteArray m_adbError;
    QByteArray m_ffmpegError;
    int m_restartBackoffMs = 1500;
    QElapsedTimer m_frameTimer;
    std::unique_ptr<SharedD3D11TexturePublisher> m_texturePublisher;
    bool m_texturePublishFailed = false;
    bool m_frameLogged = false;
    bool m_pipeLogged = false;
};

} // namespace chimera::graphics
