#include "ScreenRecorder.h"
#include "LowInterferenceProcess.h"
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QFileInfo>
#include <QDateTime>
#include <QRegularExpression>

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
    return !findFFmpeg(explicitPath).isEmpty();
}

QString ScreenRecorder::findFFmpeg(const QString &explicitPath) {
    if (!explicitPath.isEmpty()) {
        return QFileInfo::exists(explicitPath) ? explicitPath : QString();
    }

    QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/ffmpeg.exe",
        "ffmpeg.exe",
        "C:/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files/ffmpeg/bin/ffmpeg.exe",
        "C:/Program Files (x86)/ffmpeg/bin/ffmpeg.exe",
    };
    for (const auto &path : candidates) {
        if (QFileInfo::exists(path)) return path;
    }

    QProcess tester;
    tester.start("where", QStringList() << "ffmpeg");
    tester.waitForFinished(2000);
    if (tester.exitCode() != 0) return QString();

    const QString found = QString::fromLocal8Bit(tester.readAllStandardOutput())
                              .split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts)
                              .value(0);
    return found.trimmed();
}

void ScreenRecorder::startRecording(const QString &outputPath, int fps) {
    if (m_recording) return;
    m_fps = fps;
    m_outputPath = outputPath;
    m_frameCounter = 0;
    m_frameWidth = 0;
    m_frameHeight = 0;

    // Determine if we can use FFmpeg
    m_ffmpegPath = findFFmpeg();
    m_usingFFmpeg = !m_ffmpegPath.isEmpty();

    if (!m_usingFFmpeg) {
        // PNG fallback: create directory
        m_pngDirectory = outputPath;
        if (!m_pngDirectory.endsWith("/") && !m_pngDirectory.endsWith("\\")) {
            m_pngDirectory += "_frames";
        }
        QDir().mkpath(m_pngDirectory);
    } else {
        QFileInfo outInfo(outputPath);
        QDir().mkpath(outInfo.absolutePath());
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

        if (m_usingFFmpeg && !startFFmpegProcess()) {
            qWarning() << "Failed to start FFmpeg, falling back to PNG";
            m_usingFFmpeg = false;
            m_pngDirectory = m_outputPath + "_frames";
            QDir().mkpath(m_pngDirectory);
        }
    }

    if (m_usingFFmpeg && m_ffmpegProcess) {
        writeFrameToFFmpeg(frame);
    } else {
        saveFrameAsPng(frame);
    }
}

bool ScreenRecorder::startFFmpegProcess() {
    if (!m_usingFFmpeg || m_ffmpegProcess || m_frameWidth <= 0 || m_frameHeight <= 0) {
        return m_ffmpegProcess != nullptr;
    }

    m_ffmpegProcess = new QProcess(this);
    QStringList args;
    args << "-y"
         << "-f" << "rawvideo"
         << "-pix_fmt" << "rgb24"
         << "-s" << QStringLiteral("%1x%2").arg(m_frameWidth).arg(m_frameHeight)
         << "-r" << QString::number(m_fps)
         << "-i" << "-"
         << "-pix_fmt" << "yuv420p"
         << "-c:v" << "libx264"
         << "-preset" << "veryfast"
         << "-crf" << "23"
         << m_outputPath;
    m_ffmpegProcess->setProgram(m_ffmpegPath);
    m_ffmpegProcess->setArguments(args);
    m_ffmpegProcess->start();
    utils::applyLowInterferencePriority(m_ffmpegProcess);
    if (m_ffmpegProcess->waitForStarted(3000)) return true;

    delete m_ffmpegProcess;
    m_ffmpegProcess = nullptr;
    return false;
}

void ScreenRecorder::writeFrameToFFmpeg(const QImage &frame) {
    if (!m_ffmpegProcess || m_ffmpegProcess->state() != QProcess::Running) return;

    // Convert to RGB24 and write to stdin
    QImage rgbFrame = frame.convertToFormat(QImage::Format_RGB888);
    const qint64 bytes = m_ffmpegProcess->write(
        reinterpret_cast<const char*>(rgbFrame.bits()), rgbFrame.sizeInBytes());
    if (bytes != rgbFrame.sizeInBytes()) {
        emit errorOccurred(QStringLiteral("FFmpeg input pipe is not accepting frames"));
    }
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
