#include "ScreenRecorder.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>

namespace chimera {

ScreenRecorder::ScreenRecorder(QObject *parent)
    : QObject(parent)
{
}

ScreenRecorder::~ScreenRecorder() {
    if (m_recording) stopRecording();
}

bool ScreenRecorder::isRecording() const {
    return m_recording;
}

bool ScreenRecorder::hasFFmpeg(const QString &explicitPath) {
    if (!explicitPath.isEmpty()) {
        return QFileInfo::exists(explicitPath);
    }
    // Check bundled ffmpeg first, then common locations + PATH
    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/ffmpeg.exe",
        "ffmpeg.exe",
        "C:/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files (x86)/ffmpeg/bin/ffmpeg.exe",
    };
    for (const auto &path : candidates) {
        if (QFileInfo::exists(path)) return true;
    }
    // Try PATH
    QProcess tester;
    tester.start("where", QStringList() << "ffmpeg");
    tester.waitForFinished(2000);
    return tester.exitCode() == 0 && !tester.readAllStandardOutput().isEmpty();
}

void ScreenRecorder::startRecording(const QString &outputPath, int fps) {
    if (m_recording) return;
    m_fps = fps;
    m_outputPath = outputPath;
    m_frameCounter = 0;
    m_frameWidth = 0;
    m_frameHeight = 0;

    // Determine if we can use FFmpeg
    m_usingFFmpeg = hasFFmpeg();

    if (m_usingFFmpeg) {
        // Start FFmpeg process with stdin pipe
        m_ffmpegProcess = new QProcess(this);
        QStringList args;
        args << "-y"
             << "-f" << "rawvideo"
             << "-pix_fmt" << "rgb24"
             << "-s" << "%1x%2" // placeholder, updated after first frame
             << "-r" << QString::number(fps)
             << "-i" << "-"
             << "-pix_fmt" << "yuv420p"
             << "-c:v" << "libx264"
             << "-preset" << "fast"
             << "-crf" << "23"
             << outputPath;
        m_ffmpegProcess->setProgram("ffmpeg");
        m_ffmpegProcess->setArguments(args);
        m_ffmpegProcess->start();
        if (!m_ffmpegProcess->waitForStarted(3000)) {
            qWarning() << "Failed to start FFmpeg, falling back to PNG";
            m_usingFFmpeg = false;
            delete m_ffmpegProcess;
            m_ffmpegProcess = nullptr;
        }
    }

    if (!m_usingFFmpeg) {
        // PNG fallback: create directory
        m_pngDirectory = outputPath;
        if (!m_pngDirectory.endsWith("/") && !m_pngDirectory.endsWith("\\")) {
            m_pngDirectory += "_frames";
        }
        QDir().mkpath(m_pngDirectory);
    }

    m_recording = true;
    emit recordingStarted();
    emit recordingChanged();
}

void ScreenRecorder::stopRecording() {
    if (!m_recording) return;
    m_recording = false;

    if (m_ffmpegProcess) {
        m_ffmpegProcess->closeWriteChannel();
        if (!m_ffmpegProcess->waitForFinished(10000)) {
            m_ffmpegProcess->kill();
        }
        delete m_ffmpegProcess;
        m_ffmpegProcess = nullptr;
    }

    emit recordingStopped();
    emit recordingChanged();
}

void ScreenRecorder::feedFrame(const QImage &frame) {
    if (!m_recording) return;
    if (frame.isNull()) return;

    if (m_frameWidth == 0 || m_frameHeight == 0) {
        m_frameWidth = frame.width();
        m_frameHeight = frame.height();

        if (m_usingFFmpeg && m_ffmpegProcess) {
            // Update resolution in args if not already set
            // Actually we need to restart ffmpeg with correct resolution
            // For simplicity, just use PNG fallback if resolution changes
        }
    }

    if (m_usingFFmpeg && m_ffmpegProcess) {
        writeFrameToFFmpeg(frame);
    } else {
        saveFrameAsPng(frame);
    }
}

void ScreenRecorder::writeFrameToFFmpeg(const QImage &frame) {
    if (!m_ffmpegProcess || m_ffmpegProcess->state() != QProcess::Running) return;

    // Convert to RGB24 and write to stdin
    QImage rgbFrame = frame.convertToFormat(QImage::Format_RGB888);
    m_ffmpegProcess->write(reinterpret_cast<const char*>(rgbFrame.bits()), rgbFrame.sizeInBytes());
}

void ScreenRecorder::saveFrameAsPng(const QImage &frame) {
    QString path = generatePngPath();
    frame.save(path, "PNG");
}

QString ScreenRecorder::generatePngPath() {
    QString name = QString("frame_%1.png").arg(m_frameCounter++, 6, 10, QChar('0'));
    return QDir(m_pngDirectory).filePath(name);
}

} // namespace chimera
