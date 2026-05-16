#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>

namespace chimera::graphics {

/**
 * @brief Tracks frame delivery performance (FPS, latency, drop rate).
 *
 * Attach to FramebufferCapture::frameReady to get real-time metrics.
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(double averageFrameTimeMs READ averageFrameTimeMs NOTIFY metricsChanged)
    Q_PROPERTY(double maxFrameTimeMs READ maxFrameTimeMs NOTIFY metricsChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY metricsChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY metricsChanged)

public:
    explicit PerformanceMonitor(QObject *parent = nullptr);

    /**
     * @brief Call this on every frame received.
     */
    void onFrameReceived();

    /**
     * @brief Call this when a frame was expected but not received.
     */
    void onFrameDropped();

    double fps() const { return m_fps; }
    double averageFrameTimeMs() const;
    double maxFrameTimeMs() const; // Worst frame time over the recent window.
    int droppedFrames() const { return m_droppedFrames; }
    int totalFrames() const { return m_totalFrames; }

    void reset();

signals:
    void fpsChanged(double fps);
    void metricsChanged();

private:
    void recalculate();

    QElapsedTimer m_frameTimer;
    QElapsedTimer m_fpsTimer;
    QQueue<double> m_frameTimes; // Last N frame times in ms
    static constexpr int MAX_SAMPLES = 60;

    double m_fps = 0.0;
    bool m_hasLastFrame = false;
    int m_droppedFrames = 0;
    int m_totalFrames = 0;
    int m_framesInInterval = 0;
};

} // namespace chimera::graphics
