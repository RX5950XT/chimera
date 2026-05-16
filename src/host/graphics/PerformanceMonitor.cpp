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
    if (m_frameTimes.size() > MAX_SAMPLES) {
        m_frameTimes.dequeue();
    }

    ++m_totalFrames;
    ++m_framesInInterval;

    recalculate();
}

void PerformanceMonitor::onFrameDropped() {
    ++m_droppedFrames;
    emit metricsChanged();
}

void PerformanceMonitor::reset() {
    m_frameTimes.clear();
    m_fps = 0.0;
    m_droppedFrames = 0;
    m_totalFrames = 0;
    m_framesInInterval = 0;
    m_hasLastFrame = false;
    m_frameTimer.start();
    m_fpsTimer.start();
}

void PerformanceMonitor::recalculate() {
    // Calculate FPS over 1-second windows. Metric notifications are emitted
    // only once per window instead of per-frame to avoid 60Hz QML churn.
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
    for (double t : m_frameTimes) {
        sum += t;
    }
    return sum / m_frameTimes.size();
}

double PerformanceMonitor::maxFrameTimeMs() const {
    double maxTime = 0.0;
    for (double t : m_frameTimes) {
        maxTime = std::max(maxTime, t);
    }
    return maxTime;
}
