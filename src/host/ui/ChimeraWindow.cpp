#include "ChimeraWindow.h"
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>

namespace chimera {

class ChimeraWindow::Impl {
public:
    bool fullscreen = false;
    QSize guestResolution{1920, 1080};
    uint32_t guestTexture = 0;
};

ChimeraWindow::ChimeraWindow(QWindow *parent)
    : QQuickWindow(parent)
    , d(std::make_unique<Impl>())
{
    setTitle("Chimera — Android Emulator");
    setMinimumSize(QSize(640, 360));
    resize(1280, 720);
    setColor(Qt::black);
}

ChimeraWindow::~ChimeraWindow() = default;

void ChimeraWindow::toggleFullscreen() {
    d->fullscreen = !d->fullscreen;
    if (d->fullscreen) {
        showFullScreen();
    } else {
        showNormal();
    }
    emit fullscreenChanged(d->fullscreen);
}

void ChimeraWindow::showInputMapper() {
    qDebug() << "InputMapper requested";
    // TODO: emit signal to QML layer to show InputMapperOverlay
}

void ChimeraWindow::takeScreenshot() {
    qDebug() << "Screenshot requested";
    // TODO: capture current framebuffer and save to file
}

void ChimeraWindow::updateGuestTexture(uint32_t textureId, int width, int height) {
    d->guestTexture = textureId;
    d->guestResolution = QSize(width, height);
    emit frameSizeChanged(width, height);
    update();
}

void ChimeraWindow::keyPressEvent(QKeyEvent *event) {
    // TODO: forward to InputBridge → virtio-input
    QQuickWindow::keyPressEvent(event);
}

void ChimeraWindow::keyReleaseEvent(QKeyEvent *event) {
    // TODO: forward to InputBridge → virtio-input
    QQuickWindow::keyReleaseEvent(event);
}

void ChimeraWindow::mousePressEvent(QMouseEvent *event) {
    // TODO: forward to InputBridge → virtio-input (absolute coordinates)
    QQuickWindow::mousePressEvent(event);
}

void ChimeraWindow::mouseReleaseEvent(QMouseEvent *event) {
    QQuickWindow::mouseReleaseEvent(event);
}

void ChimeraWindow::mouseMoveEvent(QMouseEvent *event) {
    QQuickWindow::mouseMoveEvent(event);
}

void ChimeraWindow::wheelEvent(QWheelEvent *event) {
    QQuickWindow::wheelEvent(event);
}

void ChimeraWindow::resizeEvent(QResizeEvent *event) {
    // TODO: notify InstanceManager to resize guest framebuffer
    QQuickWindow::resizeEvent(event);
}

} // namespace chimera
