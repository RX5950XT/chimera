#include "AdbH264FramebufferCapture.h"

#include <QDebug>
#include <QImage>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace chimera::graphics {

namespace {

constexpr int kMinWidth = 1920;
constexpr int kMinHeight = 1080;
constexpr int kDefaultBitRate = 24000000;
constexpr int kMaxBufferedFrames = 3;
constexpr qsizetype kMaxErrorBytes = 4096;
std::atomic_uint s_h264PublisherId{0};

QString sizeArg(const QSize &size) {
    return QStringLiteral("%1x%2").arg(size.width()).arg(size.height());
}

void appendBounded(QByteArray *buffer, const QByteArray &data) {
    if (data.isEmpty()) return;
    buffer->append(data);
    if (buffer->size() > kMaxErrorBytes)
        buffer->remove(0, buffer->size() - kMaxErrorBytes);
}

QString errorTail(const QByteArray &data) {
    QString text = QString::fromLocal8Bit(data).trimmed();
    if (text.size() > 600)
        text = text.right(600);
    return text;
}

QString localObjectName(const QString &prefix) {
#ifdef _WIN32
    const uint id = ++s_h264PublisherId;
    return QStringLiteral("Local\\ChimeraH264%1_%2_%3")
        .arg(prefix)
        .arg(GetCurrentProcessId())
        .arg(id);
#else
    return QStringLiteral("ChimeraH264%1_%2").arg(prefix).arg(++s_h264PublisherId);
#endif
}

void applyLowInterferencePriority(QProcess *process) {
#ifdef _WIN32
    if (!process || process->processId() <= 0) return;
    HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                static_cast<DWORD>(process->processId()));
    if (!handle) return;
    SetPriorityClass(handle, BELOW_NORMAL_PRIORITY_CLASS);
#ifdef MEMORY_PRIORITY_LOW
    MEMORY_PRIORITY_INFORMATION memoryPriority = {};
    memoryPriority.MemoryPriority = MEMORY_PRIORITY_LOW;
    SetProcessInformation(handle, ProcessMemoryPriority,
                          &memoryPriority, sizeof(memoryPriority));
#endif
#ifdef PROCESS_POWER_THROTTLING_CURRENT_VERSION
    PROCESS_POWER_THROTTLING_STATE throttling = {};
    throttling.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    throttling.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    throttling.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
#ifdef PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION
    throttling.ControlMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
    throttling.StateMask |= PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION;
#endif
    SetProcessInformation(handle, ProcessPowerThrottling, &throttling, sizeof(throttling));
#endif
    CloseHandle(handle);
#else
    Q_UNUSED(process)
#endif
}

// Lighter version for real-time decode processes: only BELOW_NORMAL CPU, no efficiency-mode throttle.
void applyDecodePriority(QProcess *process) {
#ifdef _WIN32
    if (!process || process->processId() <= 0) return;
    HANDLE handle = OpenProcess(PROCESS_SET_INFORMATION, FALSE,
                                static_cast<DWORD>(process->processId()));
    if (!handle) return;
    SetPriorityClass(handle, BELOW_NORMAL_PRIORITY_CLASS);
    CloseHandle(handle);
#else
    Q_UNUSED(process)
#endif
}

} // namespace

AdbH264FramebufferCapture::AdbH264FramebufferCapture(QString adbPath,
                                                     QString ffmpegPath,
                                                     QString serial,
                                                     int requestedWidth,
                                                     int requestedHeight,
                                                     QObject *parent)
    : FramebufferCapture(parent),
      m_adbPath(std::move(adbPath)),
      m_ffmpegPath(std::move(ffmpegPath)),
      m_serial(std::move(serial)),
      m_size(normalizedCaptureSize(requestedWidth, requestedHeight)),
      m_bitRate(kDefaultBitRate) {
}

AdbH264FramebufferCapture::~AdbH264FramebufferCapture() {
    stop();
}

QSize AdbH264FramebufferCapture::normalizedCaptureSize(int requestedWidth,
                                                       int requestedHeight) {
    return QSize((std::max)(requestedWidth, kMinWidth),
                 (std::max)(requestedHeight, kMinHeight));
}

QStringList AdbH264FramebufferCapture::buildAdbArgs(const QString &serial,
                                                    const QSize &size,
                                                    int bitRate) {
    QStringList args;
    if (!serial.isEmpty())
        args << QStringLiteral("-s") << serial;
    args << QStringLiteral("exec-out")
         << QStringLiteral("screenrecord")
         << QStringLiteral("--output-format=h264")
         << QStringLiteral("--size") << sizeArg(normalizedCaptureSize(size.width(), size.height()))
         << QStringLiteral("--bit-rate") << QString::number((std::max)(bitRate, kDefaultBitRate))
         << QStringLiteral("-");
    return args;
}

QStringList AdbH264FramebufferCapture::buildFfmpegArgs(const QSize &size) {
    const QSize normalized = normalizedCaptureSize(size.width(), size.height());
    return {
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-fflags"), QStringLiteral("nobuffer"),
        QStringLiteral("-flags"), QStringLiteral("low_delay"),
        QStringLiteral("-probesize"), QStringLiteral("65536"),
        QStringLiteral("-analyzeduration"), QStringLiteral("0"),
        QStringLiteral("-f"), QStringLiteral("h264"),
        QStringLiteral("-i"), QStringLiteral("pipe:0"),
        QStringLiteral("-f"), QStringLiteral("rawvideo"),
        QStringLiteral("-pix_fmt"), QStringLiteral("bgra"),
        QStringLiteral("pipe:1")
    };
}

bool AdbH264FramebufferCapture::start() {
    if (m_running) return true;
    if (m_adbPath.isEmpty() || m_ffmpegPath.isEmpty()) {
        emit captureError(QStringLiteral("ADB H.264 capture requires adb and ffmpeg paths"));
        return false;
    }
    m_running = true;
    m_stopping = false;
    m_restartPending = false;
    m_rawBuffer.clear();
    m_adbError.clear();
    m_ffmpegError.clear();
    m_frameTimer.start();
    m_restartBackoffMs = 1500;
    m_texturePublishFailed = false;
    m_frameLogged = false;
    m_pipeLogged = false;
    SharedD3D11TexturePublisher::Config textureConfig;
    textureConfig.metadataName = localObjectName(QStringLiteral("Meta"));
    textureConfig.textureName = localObjectName(QStringLiteral("Texture"));
    textureConfig.frameEventName = localObjectName(QStringLiteral("Frame"));
    textureConfig.size = m_size;
    m_texturePublisher = std::make_unique<SharedD3D11TexturePublisher>(textureConfig);
    QString textureError;
    if (!m_texturePublisher->start(&textureError)) {
        m_texturePublisher.reset();
        m_texturePublishFailed = true;
        emit captureError(QStringLiteral("ADB H.264 D3D11 texture publisher unavailable; using QImage fallback: %1")
                              .arg(textureError));
    }
    startProcesses();
    return true;
}

void AdbH264FramebufferCapture::stop() {
    m_running = false;
    m_stopping = true;
    m_restartPending = false;
    stopProcesses();
    if (m_texturePublisher) {
        m_texturePublisher->stop();
        m_texturePublisher.reset();
    }
    m_rawBuffer.clear();
    m_stopping = false;
}

bool AdbH264FramebufferCapture::isRunning() const {
    return m_running;
}

void AdbH264FramebufferCapture::startProcesses() {
    if (!m_running || m_adb || m_ffmpeg) return;

    m_adbError.clear();
    m_ffmpegError.clear();

    m_ffmpeg = new QProcess(this);
    m_ffmpeg->setProgram(m_ffmpegPath);
    m_ffmpeg->setArguments(buildFfmpegArgs(m_size));
    m_ffmpeg->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_ffmpeg, &QProcess::readyReadStandardOutput,
            this, &AdbH264FramebufferCapture::onFfmpegReadyRead);
    connect(m_ffmpeg, &QProcess::readyReadStandardError,
            this, &AdbH264FramebufferCapture::onFfmpegStderr);
    connect(m_ffmpeg, &QProcess::finished,
            this, &AdbH264FramebufferCapture::onFfmpegFinished);

    m_adb = new QProcess(this);
    m_adb->setProgram(m_adbPath);
    m_adb->setArguments(buildAdbArgs(m_serial, m_size, m_bitRate));
    m_adb->setProcessChannelMode(QProcess::SeparateChannels);
    // Wire adb stdout directly to ffmpeg stdin at OS level (same as a shell |
    // pipe). This avoids manual Qt event-loop forwarding which can stall on
    // Windows when the main loop is busy with other ADB operations at boot.
    m_adb->setStandardOutputProcess(m_ffmpeg);
    connect(m_adb, &QProcess::readyReadStandardError,
            this, &AdbH264FramebufferCapture::onAdbStderr);
    connect(m_adb, &QProcess::finished,
            this, &AdbH264FramebufferCapture::onAdbFinished);

    qDebug() << "[AdbH264] ffmpeg args:" << buildFfmpegArgs(m_size);
    qDebug() << "[AdbH264] adb args:" << buildAdbArgs(m_serial, m_size, m_bitRate);

    // ffmpeg must be started before adb so its stdin pipe is ready.
    m_ffmpeg->start();
    if (!m_ffmpeg->waitForStarted(3000)) {
        emit captureError(QStringLiteral("failed to start ffmpeg for ADB H.264 capture: %1")
                              .arg(m_ffmpeg->errorString()));
        stopProcesses();
        return;
    }
    // Use lighter priority for ffmpeg: real-time H.264 decode needs CPU time.
    // Full efficiency-mode throttle (applyLowInterferencePriority) starves the decoder.
    applyDecodePriority(m_ffmpeg);
    qDebug() << "[AdbH264] ffmpeg started pid:" << m_ffmpeg->processId();

    m_adb->start();
    if (!m_adb->waitForStarted(3000)) {
        emit captureError(QStringLiteral("failed to start adb screenrecord: %1")
                              .arg(m_adb->errorString()));
        stopProcesses();
        return;
    }
    applyLowInterferencePriority(m_adb);
    qDebug() << "[AdbH264] adb started pid:" << m_adb->processId();

    // Diagnostic: check pipe health 5 seconds after startup.
    QTimer::singleShot(5000, this, [this]() {
        if (!m_adb || !m_ffmpeg) return;
        qDebug() << "[AdbH264] 5s check:"
                 << "adb running=" << (m_adb->state() == QProcess::Running)
                 << "ffmpeg running=" << (m_ffmpeg->state() == QProcess::Running)
                 << "rawBuffer bytes=" << m_rawBuffer.size()
                 << "ffmpeg bytesAvailable=" << m_ffmpeg->bytesAvailable();
    });
}

void AdbH264FramebufferCapture::stopProcesses() {
    if (m_adb) {
        disconnect(m_adb, nullptr, this, nullptr);
        m_adb->kill();
        m_adb->waitForFinished(3000);
        m_adb->deleteLater();
        m_adb = nullptr;
    }
    if (m_ffmpeg) {
        disconnect(m_ffmpeg, nullptr, this, nullptr);
        m_ffmpeg->closeWriteChannel();
        if (m_ffmpeg->state() != QProcess::NotRunning) {
            m_ffmpeg->kill();
            m_ffmpeg->waitForFinished(3000);
        }
        m_ffmpeg->deleteLater();
        m_ffmpeg = nullptr;
    }
}

void AdbH264FramebufferCapture::onAdbStderr() {
    if (!m_adb) return;
    const QByteArray data = m_adb->readAllStandardError();
    appendBounded(&m_adbError, data);
    if (!data.isEmpty())
        qDebug() << "[AdbH264] adb:" << QString::fromLocal8Bit(data).trimmed();
}

void AdbH264FramebufferCapture::onFfmpegReadyRead() {
    if (!m_ffmpeg) return;
    const QByteArray chunk = m_ffmpeg->readAllStandardOutput();
    if (!m_pipeLogged) {
        m_pipeLogged = true;
        qDebug() << "[AdbH264] ffmpeg readyReadStandardOutput fired, bytes:" << chunk.size();
    }
    m_rawBuffer.append(chunk);
    parseRawFrames();
}

void AdbH264FramebufferCapture::onFfmpegStderr() {
    if (!m_ffmpeg) return;
    const QByteArray data = m_ffmpeg->readAllStandardError();
    appendBounded(&m_ffmpegError, data);
    if (!data.isEmpty())
        qWarning() << "[AdbH264] ffmpeg:" << QString::fromLocal8Bit(data).trimmed();
}

void AdbH264FramebufferCapture::parseRawFrames() {
    const qsizetype frameBytes = static_cast<qsizetype>(m_size.width()) *
                                 static_cast<qsizetype>(m_size.height()) * 4;
    if (frameBytes <= 0) return;
    const qsizetype maxBuffer = frameBytes * kMaxBufferedFrames;
    if (m_rawBuffer.size() > maxBuffer)
        m_rawBuffer.remove(0, m_rawBuffer.size() - maxBuffer);

    while (m_rawBuffer.size() >= frameBytes) {
        publishFrame(m_rawBuffer.constData(), frameBytes);
        m_rawBuffer.remove(0, frameBytes);
    }
}

void AdbH264FramebufferCapture::publishFrame(const char *data, qsizetype frameBytes) {
    if (!data || frameBytes <= 0) return;
    if (!m_frameLogged) {
        m_frameLogged = true;
        qDebug() << "[AdbH264] first frame received, bytes:" << frameBytes;
    }
    emit streamFrameReceived(true);
    const int bytesPerLine = m_size.width() * 4;
    if (m_texturePublisher && !m_texturePublishFailed) {
        QString error;
        if (m_texturePublisher->publishBgraFrame(data, bytesPerLine, &error)) {
            emit sharedD3D11TextureReady(m_texturePublisher->textureName(),
                                         m_size,
                                         m_texturePublisher->sequence(),
                                         true);
            return;
        }
        m_texturePublishFailed = true;
        emit captureError(QStringLiteral("ADB H.264 D3D11 texture publish failed; using QImage fallback: %1")
                              .arg(error));
    }
    QImage image(m_size, QImage::Format_ARGB32);
    std::memcpy(image.bits(), data, static_cast<size_t>(frameBytes));
    emit frameReady(image);
}

void AdbH264FramebufferCapture::onAdbFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    if (!m_stopping)
        restartSoon(QStringLiteral("adb screenrecord exited with code %1").arg(exitCode));
}

void AdbH264FramebufferCapture::onFfmpegFinished(int exitCode, QProcess::ExitStatus status) {
    Q_UNUSED(status);
    if (!m_stopping)
        restartSoon(QStringLiteral("ffmpeg decoder exited with code %1").arg(exitCode));
}

void AdbH264FramebufferCapture::restartSoon(const QString &reason) {
    if (!m_running || m_restartPending) return;
    m_restartPending = true;
    QString detail = reason;
    const QString adbErr = errorTail(m_adbError);
    const QString ffmpegErr = errorTail(m_ffmpegError);
    if (!adbErr.isEmpty())
        detail += QStringLiteral("; adb: ") + adbErr;
    if (!ffmpegErr.isEmpty())
        detail += QStringLiteral("; ffmpeg: ") + ffmpegErr;
    emit captureError(detail);
    stopProcesses();
    const int delayMs = m_restartBackoffMs;
    m_restartBackoffMs = (std::min)(m_restartBackoffMs * 2, 8000);
    QTimer::singleShot(delayMs, this, [this]() {
        m_restartPending = false;
        if (m_running)
            startProcesses();
    });
}

} // namespace chimera::graphics
