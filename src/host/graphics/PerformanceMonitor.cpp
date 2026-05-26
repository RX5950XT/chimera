#include "PerformanceMonitor.h"
#include <QDebug>
#include <algorithm>
#include <cmath>

using namespace chimera::graphics;

PerformanceMonitor::PerformanceMonitor(QObject *parent)
    : QObject(parent) {
    m_guestFrameTimer.start();
    m_fpsTimer.start();
    m_recalcTimer.setInterval(250);
    connect(&m_recalcTimer, &QTimer::timeout, this, &PerformanceMonitor::recalculate);
    m_recalcTimer.start();
}

void PerformanceMonitor::onFrameReceived(bool contentChanged) {
    ++m_totalFrames;
    ++m_streamFramesInInterval;

    if (!contentChanged) {
        ++m_duplicateFrames;
        ++m_duplicateFramesInInterval;
        ++m_consecutiveDuplicateFrames;
        if (m_consecutiveDuplicateFrames >= 6)
            m_hasLastGuestFrame = false;
        recalculate();
        return;
    }

    m_consecutiveDuplicateFrames = 0;
    qint64 elapsed = m_guestFrameTimer.restart();
    if (!m_hasLastGuestFrame) {
        m_hasLastGuestFrame = true;
        ++m_guestFramesInInterval;
        recalculate();
        return;
    }
    if (elapsed <= 0) elapsed = 1;

    double frameTime = static_cast<double>(elapsed);
    m_frameTimes.enqueue(frameTime);
    if (m_frameTimes.size() > kMaxSamples)
        m_frameTimes.dequeue();

    ++m_guestFramesInInterval;
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
    ++m_renderFramesInInterval;
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
    m_streamFps = 0.0;
    m_renderFps = 0.0;
    m_droppedFrames = 0;
    m_totalFrames = 0;
    m_duplicateFrames = 0;
    m_streamFramesInInterval = 0;
    m_guestFramesInInterval = 0;
    m_renderFramesInInterval = 0;
    m_duplicateFramesInInterval = 0;
    m_consecutiveDuplicateFrames = 0;
    m_framesOnTime = 0;
    m_framesForRate = 0;
    m_hasLastGuestFrame = false;
    m_inputPending = false;
    m_visibleLatencyMs = -1.0;
    m_captureLatencyMs = 0.0;
    m_decodeLatencyMs  = 0.0;
    m_renderLatencyMs  = 0.0;
    m_targetHitRate    = 0.0;
    m_duplicateRate    = 0.0;
    m_guestFrameTimer.start();
    m_fpsTimer.start();
    emit fpsChanged(m_fps);
    emit metricsChanged();
}

void PerformanceMonitor::recalculate() {
    const qint64 elapsed = m_fpsTimer.elapsed();
    if (elapsed < 1000) return;

    const double elapsedSeconds = static_cast<double>(elapsed) / 1000.0;
    m_fps = static_cast<double>(m_guestFramesInInterval) / elapsedSeconds;
    m_streamFps = static_cast<double>(m_streamFramesInInterval) / elapsedSeconds;
    m_renderFps = static_cast<double>(m_renderFramesInInterval) / elapsedSeconds;
    m_duplicateRate = (m_streamFramesInInterval > 0)
        ? static_cast<double>(m_duplicateFramesInInterval)
              / static_cast<double>(m_streamFramesInInterval)
        : 0.0;

    m_streamFramesInInterval = 0;
    m_guestFramesInInterval = 0;
    m_renderFramesInInterval = 0;
    m_duplicateFramesInInterval = 0;
    m_fpsTimer.restart();

    emit fpsChanged(m_fps);
    emit metricsChanged();
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
