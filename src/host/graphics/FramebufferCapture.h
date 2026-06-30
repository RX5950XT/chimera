#pragma once

#include <QObject>
#include <QImage>
#include <QSize>

namespace chimera::graphics {

/**
 * @brief Abstract base for frame capture backends.
 *
 * Subclasses implement different protocols (ADB screencap, VNC, shared memory)
 * to receive frames from the guest Android display.
 */
class FramebufferCapture : public QObject {
    Q_OBJECT

public:
    explicit FramebufferCapture(QObject *parent = nullptr) : QObject(parent) {}
    ~FramebufferCapture() override = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
    virtual QString backendName() const = 0;

    // True while a capture request is still in flight. Lets the boot retry
    // timer skip a blind stop()/start() that would abort a slow-but-progressing
    // readback and reset the stream. Backends without an in-flight concept
    // report false, keeping their restart behaviour unchanged.
    virtual bool hasInFlight() const { return false; }

    // Target capture interval in ms (0 = unlimited)
    void setIntervalMs(int ms) { m_intervalMs = ms; }
    int intervalMs() const { return m_intervalMs; }

signals:
    void streamFrameReceived(bool contentChanged);
    void frameReady(const QImage &img);
    void sharedD3D11TextureReady(const QString &textureName,
                                 const QSize &size,
                                 quint64 sequence,
                                 bool hasAlpha);
    void captureError(const QString &message);

protected:
    int m_intervalMs = 50; // Default 20 FPS
};

} // namespace chimera::graphics
