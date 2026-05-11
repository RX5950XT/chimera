#include "GuestDisplay.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include "InputBridge.h"

namespace chimera {

GuestDisplay::GuestDisplay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAcceptedMouseButtons(Qt::AllButtons);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFlag(QQuickItem::ItemHasContents, true);
    setFocus(true);
}

QImage GuestDisplay::frame() const {
    return m_frame;
}

void GuestDisplay::setFrame(const QImage &img) {
    if (m_frame.isNull() || img.size() != m_frame.size()) {
        m_frame = img.copy();
        update();
        emit frameChanged();
        return;
    }
    m_frame = img.copy();
    update();
}

bool GuestDisplay::saveScreenshot(const QString &filePath) const {
    if (m_frame.isNull()) return false;
    return m_frame.save(filePath);
}

void GuestDisplay::paint(QPainter *painter) {
    if (m_frame.isNull()) {
        painter->fillRect(boundingRect(), Qt::black);
        painter->setPen(Qt::white);
        painter->drawText(boundingRect(), Qt::AlignCenter, "Waiting for guest display...");
        return;
    }
    QRectF target = boundingRect();
    QRectF source(0, 0, m_frame.width(), m_frame.height());
    // Preserve aspect ratio, letterbox
    qreal sx = target.width() / source.width();
    qreal sy = target.height() / source.height();
    qreal s = qMin(sx, sy);
    qreal nw = source.width() * s;
    qreal nh = source.height() * s;
    qreal nx = target.x() + (target.width() - nw) / 2;
    qreal ny = target.y() + (target.height() - nh) / 2;
    painter->drawImage(QRectF(nx, ny, nw, nh), m_frame, source);
}

QPointF GuestDisplay::mapToGuest(const QPointF &pos) const {
    QRectF target = boundingRect();
    QRectF source(0, 0, m_frame.width(), m_frame.height());
    qreal sx = target.width() / source.width();
    qreal sy = target.height() / source.height();
    qreal scale = qMin(sx, sy);
    qreal nw = source.width() * scale;
    qreal nh = source.height() * scale;
    qreal nx = target.x() + (target.width() - nw) / 2;
    qreal ny = target.y() + (target.height() - nh) / 2;

    qreal gx = (pos.x() - nx) / scale;
    qreal gy = (pos.y() - ny) / scale;
    return QPointF(qBound(qreal(0), gx, source.width()), qBound(qreal(0), gy, source.height()));
}

void GuestDisplay::keyPressEvent(QKeyEvent *event) {
    chimera::input::InputBridge::instance().onKeyEvent(
        true, event->nativeScanCode(), event->key());
    event->accept();
}

void GuestDisplay::keyReleaseEvent(QKeyEvent *event) {
    chimera::input::InputBridge::instance().onKeyEvent(
        false, event->nativeScanCode(), event->key());
    event->accept();
}

void GuestDisplay::mousePressEvent(QMouseEvent *event) {
    auto guestPos = mapToGuest(event->position());
    chimera::input::InputBridge::instance().onMouseButton(
        true, event->button(), static_cast<int>(guestPos.x()), static_cast<int>(guestPos.y()));
    event->accept();
}

void GuestDisplay::mouseReleaseEvent(QMouseEvent *event) {
    auto guestPos = mapToGuest(event->position());
    chimera::input::InputBridge::instance().onMouseButton(
        false, event->button(), static_cast<int>(guestPos.x()), static_cast<int>(guestPos.y()));
    event->accept();
}

void GuestDisplay::mouseMoveEvent(QMouseEvent *event) {
    auto guestPos = mapToGuest(event->position());
    chimera::input::InputBridge::instance().onMouseMove(
        static_cast<int>(guestPos.x()), static_cast<int>(guestPos.y()), 0, 0);
    event->accept();
}

void GuestDisplay::wheelEvent(QWheelEvent *event) {
    auto delta = event->angleDelta();
    chimera::input::InputBridge::instance().onWheel(delta.x(), delta.y());
    event->accept();
}

} // namespace chimera
