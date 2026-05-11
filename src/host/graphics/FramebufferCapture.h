#pragma once

#include <QObject>
#include <QImage>

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

    // Target capture interval in ms (0 = unlimited)
    void setIntervalMs(int ms) { m_intervalMs = ms; }
    int intervalMs() const { return m_intervalMs; }

signals:
    void frameReady(const QImage &img);
    void captureError(const QString &message);

protected:
    int m_intervalMs = 50; // Default 20 FPS
};

} // namespace chimera::graphics
