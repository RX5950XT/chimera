#include "AdbFramebufferCapture.h"
#include <QDebug>

using namespace chimera::graphics;

AdbFramebufferCapture::AdbFramebufferCapture(const QString &adbPath, int adbPort,
                                              bool usePng, QObject *parent)
    : FramebufferCapture(parent)
    , m_adbPath(adbPath)
    , m_adbPort(adbPort)
    , m_usePng(usePng)
    , m_process(new QProcess(this))
    , m_timer(new QTimer(this))
{
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &AdbFramebufferCapture::onProcessFinished);
    connect(m_timer, &QTimer::timeout, this, &AdbFramebufferCapture::onCaptureTimeout);
}

AdbFramebufferCapture::~AdbFramebufferCapture() {
    stop();
}

bool AdbFramebufferCapture::start() {
    if (m_running) return true;
    m_running = true;
    m_timer->start(m_intervalMs);
    onCaptureTimeout(); // Capture immediately
    return true;
}

void AdbFramebufferCapture::stop() {
    m_running = false;
    m_timer->stop();
    if (m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(1000);
    }
}

bool AdbFramebufferCapture::isRunning() const {
    return m_running;
}

void AdbFramebufferCapture::onCaptureTimeout() {
    if (m_process->state() != QProcess::NotRunning) {
        // Previous capture still running, skip this frame
        return;
    }

    QStringList args;
    args << "-P" << QString::number(m_adbPort)
         << "exec-out" << "screencap";
    if (m_usePng) args << "-p";

    m_process->setProgram(m_adbPath);
    m_process->setArguments(args);
    m_process->start();
}

void AdbFramebufferCapture::onProcessFinished(int exitCode, QProcess::ExitStatus status) {
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
        img = decodeRawFrame(data);
        if (!img.isNull()) {
            emit frameReady(img);
        } else {
            emit captureError(QStringLiteral("Failed to decode raw screencap"));
        }
    }
}

QImage AdbFramebufferCapture::decodeRawFrame(const QByteArray &data) {
    if (data.size() < 12) return QImage();

    const uint8_t *p = reinterpret_cast<const uint8_t*>(data.constData());
    int w = *reinterpret_cast<const int32_t*>(p);
    int h = *reinterpret_cast<const int32_t*>(p + 4);
    int fmt = *reinterpret_cast<const int32_t*>(p + 8);

    if (w <= 0 || h <= 0 || w > 10000 || h > 10000) return QImage();

    int headerSize = 12;
    int expectedSize = w * h * 4;
    if (data.size() < headerSize + expectedSize) return QImage();

    QImage::Format qfmt = (fmt == 1) ? QImage::Format_RGBA8888 : QImage::Format_ARGB32;
    // Android screencap raw is BGRA on x86, but let's use RGBA8888_Premultiplied as fallback
    if (qfmt == QImage::Format_ARGB32) {
        qfmt = QImage::Format_RGBA8888;
    }

    return QImage(p + headerSize, w, h, qfmt).copy();
}
