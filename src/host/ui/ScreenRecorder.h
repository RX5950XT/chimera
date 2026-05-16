#pragma once

#include <QImage>
#include <QString>
#include <QProcess>
#include <QTimer>
#include <QObject>
#include <atomic>
#include <vector>

namespace chimera {

/**
 * @brief Screen recorder that captures GuestDisplay frames to video.
 *
 * Uses FFmpeg subprocess for real-time H.264 encoding if available,
 * otherwise falls back to numbered PNG sequence.
 */
class ScreenRecorder : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
public:
    explicit ScreenRecorder(QObject *parent = nullptr);
    ~ScreenRecorder();

    bool isRecording() const;

    // Returns true if FFmpeg binary was found on PATH or at explicit path
    static bool hasFFmpeg(const QString &explicitPath = QString());

public slots:
    void startRecording(const QString &outputPath, int fps = 30);
    void stopRecording();
    void feedFrame(const QImage &frame);

signals:
    void recordingStarted();
    void recordingStopped();
    void recordingChanged();
    void errorOccurred(const QString &message);

private:
    static QString findFFmpeg(const QString &explicitPath = QString());
    bool startFFmpegProcess();
    void writeFrameToFFmpeg(const QImage &frame);
    void saveFrameAsPng(const QImage &frame);
    QString generatePngPath();

    bool m_recording = false;
    bool m_usingFFmpeg = false;
    int m_fps = 30;
    QString m_outputPath;
    QString m_ffmpegPath;
    QProcess *m_ffmpegProcess = nullptr;

    // PNG fallback
    int m_frameCounter = 0;
    QString m_pngDirectory;

    // Frame dimensions (fixed at start)
    int m_frameWidth = 0;
    int m_frameHeight = 0;
};

} // namespace chimera
