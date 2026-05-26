#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QQueue>
#include <QTimer>

namespace chimera::graphics {

/**
 * @brief Tracks frame delivery performance and visible latency.
 *
 * Frame pipeline stages measured (all in ms):
 *   captureMs  — time from capture-request-start to raw frame data arriving
 *   decodeMs   — time to decode compressed frame (PNG/JPEG) into QImage
 *   renderMs   — time Qt takes to paint the QImage to screen
 *
 * Visible latency:
 *   From the last input event timestamp to the next frameRendered() call.
 *   Approximates host-click → guest-pixels latency.
 *
 * Usage:
 *   connect(capture, &FramebufferCapture::frameReady, monitor, &PerformanceMonitor::onFrameReceived);
 *   Call onInputEvent() from input dispatch; onCaptureStart/End/Decode/Render from the pipeline.
 */
class PerformanceMonitor : public QObject {
    Q_OBJECT

    Q_PROPERTY(double fps                READ fps                NOTIFY fpsChanged)
    Q_PROPERTY(double guestFps           READ guestFps           NOTIFY fpsChanged)
    Q_PROPERTY(double streamFps          READ streamFps          NOTIFY metricsChanged)
    Q_PROPERTY(double renderFps          READ renderFps          NOTIFY metricsChanged)
    Q_PROPERTY(double averageFrameTimeMs READ averageFrameTimeMs NOTIFY metricsChanged)
    Q_PROPERTY(double maxFrameTimeMs     READ maxFrameTimeMs     NOTIFY metricsChanged)
    Q_PROPERTY(double visibleLatencyMs   READ visibleLatencyMs   NOTIFY metricsChanged)
    Q_PROPERTY(double captureLatencyMs   READ captureLatencyMs   NOTIFY metricsChanged)
    Q_PROPERTY(double decodeLatencyMs    READ decodeLatencyMs    NOTIFY metricsChanged)
    Q_PROPERTY(double renderLatencyMs    READ renderLatencyMs    NOTIFY metricsChanged)
    Q_PROPERTY(int    droppedFrames      READ droppedFrames      NOTIFY metricsChanged)
    Q_PROPERTY(int    duplicateFrames    READ duplicateFrames    NOTIFY metricsChanged)
    Q_PROPERTY(int    totalFrames        READ totalFrames        NOTIFY metricsChanged)
    Q_PROPERTY(double duplicateRate      READ duplicateRate      NOTIFY metricsChanged)
    Q_PROPERTY(double targetHitRate      READ targetHitRate      NOTIFY metricsChanged)

public:
    explicit PerformanceMonitor(QObject *parent = nullptr);

    // Core frame accounting
    void onFrameReceived(bool contentChanged = true);
    void onFrameDropped();

    // Input event (marks start of visible-latency measurement)
    void onInputEvent();

    // Per-stage timers — call start before the work, end after
    void onCaptureStart();
    void onCaptureEnd();
    void onDecodeStart();
    void onDecodeEnd();
    void onRenderStart();
    void onRenderEnd();

    // Called when the Qt view has finished painting a frame (visible-latency end)
    void onFrameRendered();

    // Target FPS for on-time tracking (default 60)
    void setTargetFps(int fps);
    int  targetFps() const { return m_targetFps; }

    double fps()                const { return m_fps; }
    double guestFps()           const { return m_fps; }
    double streamFps()          const { return m_streamFps; }
    double renderFps()          const { return m_renderFps; }
    double averageFrameTimeMs() const;
    double maxFrameTimeMs()     const;
    double visibleLatencyMs()   const { return m_visibleLatencyMs; }
    double captureLatencyMs()   const { return m_captureLatencyMs; }
    double decodeLatencyMs()    const { return m_decodeLatencyMs; }
    double renderLatencyMs()    const { return m_renderLatencyMs; }
    int    droppedFrames()      const { return m_droppedFrames; }
    int    duplicateFrames()    const { return m_duplicateFrames; }
    int    totalFrames()        const { return m_totalFrames; }
    double duplicateRate()      const { return m_duplicateRate; }
    // Fraction of frames delivered within 1.5× the target frame interval
    double targetHitRate()      const { return m_targetHitRate; }

    void reset();

signals:
    void fpsChanged(double fps);
    void metricsChanged();

private:
    void recalculate();
    void updateTargetHitRate(double frameTimeMs);

    QElapsedTimer m_guestFrameTimer;
    QElapsedTimer m_fpsTimer;
    QElapsedTimer m_captureStageTimer;
    QElapsedTimer m_decodeStageTimer;
    QElapsedTimer m_renderStageTimer;
    QElapsedTimer m_inputTimer;       // marks last input event

    QQueue<double> m_frameTimes;      // last N frame times (ms)
    static constexpr int kMaxSamples = 60;

    double m_fps             = 0.0;
    double m_streamFps       = 0.0;
    double m_renderFps       = 0.0;
    double m_visibleLatencyMs= -1.0;  // -1 = no input event pending
    double m_captureLatencyMs= 0.0;
    double m_decodeLatencyMs = 0.0;
    double m_renderLatencyMs = 0.0;
    double m_targetHitRate   = 0.0;
    double m_duplicateRate   = 0.0;

    int m_targetFps        = 60;
    int m_droppedFrames    = 0;
    int m_totalFrames      = 0;
    int m_duplicateFrames  = 0;
    int m_streamFramesInInterval = 0;
    int m_guestFramesInInterval = 0;
    int m_renderFramesInInterval = 0;
    int m_duplicateFramesInInterval = 0;
    int m_consecutiveDuplicateFrames = 0;
    int m_framesOnTime     = 0;
    int m_framesForRate    = 0;

    bool m_hasLastGuestFrame = false;
    bool m_inputPending    = false;
    QTimer m_recalcTimer;
};

} // namespace chimera::graphics
