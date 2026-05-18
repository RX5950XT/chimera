#include "ChimeraWindow.h"
#include "InputBridge.h"
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
    emit requestShowInputMapper();
}

void ChimeraWindow::takeScreenshot() {
    emit requestScreenshot();
}

void ChimeraWindow::updateGuestTexture(uint32_t textureId, int width, int height) {
    d->guestTexture = textureId;
    d->guestResolution = QSize(width, height);
    emit frameSizeChanged(width, height);
    update();
}

void ChimeraWindow::keyPressEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat())
        input::InputBridge::instance().onKeyEvent(true,
            event->nativeScanCode(), event->nativeVirtualKey());
    QQuickWindow::keyPressEvent(event);
}

void ChimeraWindow::keyReleaseEvent(QKeyEvent *event) {
    if (!event->isAutoRepeat())
        input::InputBridge::instance().onKeyEvent(false,
            event->nativeScanCode(), event->nativeVirtualKey());
    QQuickWindow::keyReleaseEvent(event);
}

void ChimeraWindow::mousePressEvent(QMouseEvent *event) {
    input::InputBridge::instance().onMouseButton(
        true, event->button(), event->position().x(), event->position().y());
    QQuickWindow::mousePressEvent(event);
}

void ChimeraWindow::mouseReleaseEvent(QMouseEvent *event) {
    input::InputBridge::instance().onMouseButton(
        false, event->button(), event->position().x(), event->position().y());
    QQuickWindow::mouseReleaseEvent(event);
}

void ChimeraWindow::mouseMoveEvent(QMouseEvent *event) {
    input::InputBridge::instance().onMouseMove(
        event->position().x(), event->position().y(), 0, 0);
    QQuickWindow::mouseMoveEvent(event);
}

void ChimeraWindow::wheelEvent(QWheelEvent *event) {
    input::InputBridge::instance().onWheel(
        event->angleDelta().x(), event->angleDelta().y());
    QQuickWindow::wheelEvent(event);
}

void ChimeraWindow::resizeEvent(QResizeEvent *event) {
    input::InputBridge::instance().setDisplaySize(
        event->size().width(), event->size().height());
    QQuickWindow::resizeEvent(event);
}

} // namespace chimera
