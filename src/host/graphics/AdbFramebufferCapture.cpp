#include "AdbFramebufferCapture.h"
#include "LowInterferenceProcess.h"
#include <QDebug>
#include <QtEndian>

using namespace chimera::graphics;

namespace {

constexpr int kRawHeaderSize = 16;
constexpr int kMaxDimension = 10000;
constexpr qsizetype kMaxBufferedBytes = 64 * 1024 * 1024;
constexpr int kWatchdogIntervalMs = 250;
constexpr int kStalledCaptureMs = 1500;
constexpr int kRawRestartDelayMs = 1500;

int bytesPerPixelForFormat(int fmt) {
    switch (fmt) {
    case 1: // RGBA_8888
    case 2: // RGBX_8888
    case 5: // BGRA_8888
        return 4;
    case 3: // RGB_888
        return 3;
    case 4: // RGB_565
        return 2;
    default:
        return 0;
    }
}

QString serialForAdbPort(int adbPort) {
    return QStringLiteral("emulator-%1").arg(adbPort - 1);
}

} // namespace

AdbFramebufferCapture::AdbFramebufferCapture(const QString &adbPath, int adbPort,
                                              bool usePng, QObject *parent)
    : FramebufferCapture(parent)
    , m_adbPath(adbPath)
    , m_adbPort(adbPort)
    , m_usePng(usePng)
    , m_process(new QProcess(this))
    , m_timer(new QTimer(this))
    , m_watchdogTimer(new QTimer(this))
{
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AdbFramebufferCapture::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &AdbFramebufferCapture::onReadyRead);
    connect(m_timer, &QTimer::timeout, this, &AdbFramebufferCapture::onCaptureTimeout);
    connect(m_watchdogTimer, &QTimer::timeout, this, &AdbFramebufferCapture::onWatchdogTimeout);
    m_watchdogTimer->setInterval(kWatchdogIntervalMs);
}

AdbFramebufferCapture::~AdbFramebufferCapture() {
    stop();
}

bool AdbFramebufferCapture::start() {
    if (m_running) return true;
    m_running = true;
    m_lastFrameTimer.start();
    if (m_usePng) {
        m_timer->start(m_intervalMs);
        onCaptureTimeout();
    } else {
        m_watchdogTimer->start();
        startRawStream();
    }
    return true;
}

void AdbFramebufferCapture::stop() {
    m_running = false;
    m_timer->stop();
    m_watchdogTimer->stop();
    m_readBuffer.clear();
    m_restartPending = false;
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(1000);
    }
}

bool AdbFramebufferCapture::isRunning() const {
    return m_running;
}

void AdbFramebufferCapture::onCaptureTimeout() {
    if (!m_usePng) return;
    if (m_process->state() != QProcess::NotRunning) {
        // Previous capture still running, skip this frame
        emit captureError(QStringLiteral("ADB screencap skipped; previous capture still running"));
        return;
    }

    startSingleCapture();
}

void AdbFramebufferCapture::startSingleCapture() {
    const QString serial = serialForAdbPort(m_adbPort);
    QStringList args;
    args << "-s" << serial
         << "exec-out" << "screencap";
    if (m_usePng) args << "-p";

    m_process->setProgram(m_adbPath);
    m_process->setArguments(args);
    m_process->start();
    chimera::utils::applyLowInterferencePriority(m_process);
}

void AdbFramebufferCapture::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
    if (!m_usePng && m_running) {
        restartRawStream(QStringLiteral("ADB raw stream stopped"));
        return;
    }

    if (exitCode != 0 || status != QProcess::NormalExit) {
        emit captureError(QStringLiteral("ADB screencap failed"));
        return;
    }

    QByteArray data = m_process->readAllStandardOutput();
    if (data.isEmpty()) {
        emit captureError(QStringLiteral("ADB screencap returned empty data"));
        return;
    }

    QImage img;
    if (m_usePng) {
        if (img.loadFromData(data, "PNG")) {
            emit frameReady(img);
        } else {
            emit captureError(QStringLiteral("Failed to decode PNG screencap"));
        }
    } else {
        img = decodeRawFrameData(data);
        if (!img.isNull()) {
            emit frameReady(img);
        } else {
            emit captureError(QStringLiteral("Failed to decode raw screencap"));
        }
    }
}

void AdbFramebufferCapture::startRawStream() {
    if (m_process->state() != QProcess::NotRunning) return;

    m_restartPending = false;
    m_readBuffer.clear();
    const QString serial = serialForAdbPort(m_adbPort);
    const QString loop = QStringLiteral("while true; do screencap; done");
    QStringList args;
    args << "-s" << serial
         << "exec-out" << "sh" << "-c" << loop;

    m_process->setProgram(m_adbPath);
    m_process->setArguments(args);
    m_process->start();
    chimera::utils::applyLowInterferencePriority(m_process);
}

void AdbFramebufferCapture::restartRawStream(const QString &reason) {
    if (!m_running || m_usePng) return;
    if (m_restartPending) return;
    m_restartPending = true;

    emit captureError(reason);
    m_readBuffer.clear();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(500);
    }
    QTimer::singleShot(kRawRestartDelayMs, this, [this]() {
        if (m_running && !m_usePng) {
            m_lastFrameTimer.restart();
            startRawStream();
        } else {
            m_restartPending = false;
        }
    });
}

void AdbFramebufferCapture::onReadyRead() {
    if (m_usePng || !m_running) return;

    m_readBuffer.append(m_process->readAllStandardOutput());
    if (m_readBuffer.size() > kMaxBufferedBytes) {
        restartRawStream(QStringLiteral("ADB raw stream buffer exceeded safety limit"));
        return;
    }
    parseRawStream();
}

void AdbFramebufferCapture::parseRawStream() {
    while (m_readBuffer.size() >= kRawHeaderSize) {
        const char *base = m_readBuffer.constData();
        const int w = qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(base));
        const int h = qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(base + 4));
        const int fmt = qFromLittleEndian<qint32>(reinterpret_cast<const uchar*>(base + 8));
        const int bytesPerPixel = bytesPerPixelForFormat(fmt);

        if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension || bytesPerPixel == 0) {
            restartRawStream(QStringLiteral("ADB raw stream returned invalid frame header"));
            return;
        }

        const qsizetype pixelsSize = static_cast<qsizetype>(w) * h * bytesPerPixel;
        const qsizetype frameSize = kRawHeaderSize + pixelsSize;
        if (m_readBuffer.size() < frameSize) return;

        QImage img = decodeRawFrameBytes(base, frameSize);
        m_readBuffer.remove(0, frameSize);
        if (img.isNull()) {
            emit captureError(QStringLiteral("Failed to decode raw screencap"));
            continue;
        }

        m_lastFrameTimer.restart();
        emit frameReady(img);
    }
}

void AdbFramebufferCapture::onWatchdogTimeout() {
    if (!m_running || m_usePng) return;

    if (m_process->state() == QProcess::NotRunning) {
        restartRawStream(QStringLiteral("ADB raw stream is not running"));
        return;
    }
    if (m_lastFrameTimer.isValid() && m_lastFrameTimer.elapsed() > kStalledCaptureMs) {
        restartRawStream(QStringLiteral("ADB raw stream stalled"));
    }
}

QImage AdbFramebufferCapture::decodeRawFrameData(const QByteArray &data) {
    return decodeRawFrameBytes(data.constData(), data.size());
}

QImage AdbFramebufferCapture::decodeRawFrameBytes(const char *data, qsizetype size) {
    if (!data || size < kRawHeaderSize) return QImage();

    const auto *p = reinterpret_cast<const uchar*>(data);
    const int w = qFromLittleEndian<qint32>(p);
    const int h = qFromLittleEndian<qint32>(p + 4);
    const int fmt = qFromLittleEndian<qint32>(p + 8);
    const int bytesPerPixel = bytesPerPixelForFormat(fmt);

    if (w <= 0 || h <= 0 || w > kMaxDimension || h > kMaxDimension || bytesPerPixel == 0) {
        return QImage();
    }

    const qsizetype expectedSize = static_cast<qsizetype>(w) * h * bytesPerPixel;
    if (size < kRawHeaderSize + expectedSize) return QImage();

    const uchar *pixels = p + kRawHeaderSize;
    switch (fmt) {
    case 1:
        return QImage(pixels, w, h, QImage::Format_RGBA8888).copy();
    case 2:
        return QImage(pixels, w, h, QImage::Format_RGBX8888).copy();
    case 3:
        return QImage(pixels, w, h, w * bytesPerPixel, QImage::Format_RGB888).copy();
    case 4:
        return QImage(pixels, w, h, w * bytesPerPixel, QImage::Format_RGB16).copy();
    case 5:
        return QImage(pixels, w, h, QImage::Format_ARGB32).copy();
    default:
        return QImage();
    }
}
