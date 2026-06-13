#include "ChimeraWindow.h"
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
    setTitle("Chimera");
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

} // namespace chimera
