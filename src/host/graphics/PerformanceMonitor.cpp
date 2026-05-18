#include "PerformanceMonitor.h"
#include <QDebug>
#include <algorithm>

using namespace chimera::graphics;

PerformanceMonitor::PerformanceMonitor(QObject *parent)
    : QObject(parent) {
    m_frameTimer.start();
    m_fpsTimer.start();
}

void PerformanceMonitor::onFrameReceived() {
    qint64 elapsed = m_frameTimer.restart();
    if (!m_hasLastFrame) {
        m_hasLastFrame = true;
        ++m_totalFrames;
        ++m_framesInInterval;
        recalculate();
        return;
    }
    if (elapsed <= 0) elapsed = 1;

    double frameTime = static_cast<double>(elapsed);
    m_frameTimes.enqueue(frameTime);
    if (m_frameTimes.size() > kMaxSamples)
        m_frameTimes.dequeue();

    ++m_totalFrames;
    ++m_framesInInterval;
    updateTargetHitRate(frameTime);
    recalculate();
}

void PerformanceMonitor::onFrameDropped() {
    ++m_droppedFrames;
    emit metricsChanged();
}

void PerformanceMonitor::onInputEvent() {
    m_inputTimer.start();
    m_inputPending = true;
}

void PerformanceMonitor::onCaptureStart() {
    m_captureStageTimer.start();
}

void PerformanceMonitor::onCaptureEnd() {
    if (m_captureStageTimer.isValid())
        m_captureLatencyMs = static_cast<double>(m_captureStageTimer.elapsed());
}

void PerformanceMonitor::onDecodeStart() {
    m_decodeStageTimer.start();
}

void PerformanceMonitor::onDecodeEnd() {
    if (m_decodeStageTimer.isValid())
        m_decodeLatencyMs = static_cast<double>(m_decodeStageTimer.elapsed());
}

void PerformanceMonitor::onRenderStart() {
    m_renderStageTimer.start();
}

void PerformanceMonitor::onRenderEnd() {
    if (m_renderStageTimer.isValid())
        m_renderLatencyMs = static_cast<double>(m_renderStageTimer.elapsed());
}

void PerformanceMonitor::onFrameRendered() {
    if (m_inputPending) {
        m_visibleLatencyMs = static_cast<double>(m_inputTimer.elapsed());
        m_inputPending = false;
        emit metricsChanged();
    }
}

void PerformanceMonitor::setTargetFps(int fps) {
    m_targetFps = (fps > 0) ? fps : 60;
    m_framesOnTime = 0;
    m_framesForRate = 0;
    m_targetHitRate = 0.0;
}

void PerformanceMonitor::updateTargetHitRate(double frameTimeMs) {
    ++m_framesForRate;
    // On-time = delivered within 1.5× the target interval
    const double threshold = 1500.0 / static_cast<double>(m_targetFps);
    if (frameTimeMs <= threshold)
        ++m_framesOnTime;
    m_targetHitRate = (m_framesForRate > 0)
        ? static_cast<double>(m_framesOnTime) / static_cast<double>(m_framesForRate)
        : 0.0;
}

void PerformanceMonitor::reset() {
    m_frameTimes.clear();
    m_fps = 0.0;
    m_droppedFrames = 0;
    m_totalFrames = 0;
    m_framesInInterval = 0;
    m_framesOnTime = 0;
    m_framesForRate = 0;
    m_hasLastFrame = false;
    m_inputPending = false;
    m_visibleLatencyMs = -1.0;
    m_captureLatencyMs = 0.0;
    m_decodeLatencyMs  = 0.0;
    m_renderLatencyMs  = 0.0;
    m_targetHitRate    = 0.0;
    m_frameTimer.start();
    m_fpsTimer.start();
}

void PerformanceMonitor::recalculate() {
    if (m_fpsTimer.elapsed() >= 1000) {
        m_fps = static_cast<double>(m_framesInInterval) * 1000.0 / m_fpsTimer.elapsed();
        emit fpsChanged(m_fps);
        m_framesInInterval = 0;
        m_fpsTimer.restart();
        emit metricsChanged();
    }
}

double PerformanceMonitor::averageFrameTimeMs() const {
    if (m_frameTimes.isEmpty()) return 0.0;
    double sum = 0.0;
    for (double t : m_frameTimes) sum += t;
    return sum / m_frameTimes.size();
}

double PerformanceMonitor::maxFrameTimeMs() const {
    double maxTime = 0.0;
    for (double t : m_frameTimes) maxTime = std::max(maxTime, t);
    return maxTime;
}
