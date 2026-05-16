#include "GuestDisplay.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QDebug>
#include <algorithm>
#include "InputBridge.h"

namespace chimera {

GuestDisplay::GuestDisplay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAcceptedMouseButtons(Qt::AllButtons);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFlag(QQuickItem::ItemHasContents, true);
    setActiveFocusOnTab(true);
    setFocus(true);
}

QImage GuestDisplay::frame() const {
    return m_frame;
}

void GuestDisplay::setFrame(const QImage &img) {
    if (m_frame.isNull() || img.size() != m_frame.size()) {
        m_frame = img;
        update();
        emit frameChanged();
        return;
    }
    m_frame = img;
    update();
}

bool GuestDisplay::hasFrame() const {
    return !m_frame.isNull();
}

void GuestDisplay::setGuestSize(const QSize &size) {
    if (size.width() <= 0 || size.height() <= 0 || m_guestSize == size) return;
    m_guestSize = size;
}

bool GuestDisplay::saveScreenshot(const QString &filePath) const {
    if (m_frame.isNull()) return false;
    return m_frame.save(filePath);
}

void GuestDisplay::paint(QPainter *painter) {
    if (m_frame.isNull()) {
        painter->fillRect(boundingRect(), Qt::black);
        painter->setPen(QColor(220, 230, 230));
        painter->drawText(boundingRect(), Qt::AlignCenter, QStringLiteral("等待 Android 畫面..."));
        return;
    }
    QRectF source(0, 0, m_frame.width(), m_frame.height());
    painter->drawImage(displayRect(), m_frame, source);
}

QRectF GuestDisplay::displayRect() const {
    if (m_frame.isNull() || m_frame.width() <= 0 || m_frame.height() <= 0) {
        return QRectF();
    }

    QRectF target = boundingRect();
    QRectF source(0, 0, m_frame.width(), m_frame.height());
    qreal sx = target.width() / source.width();
    qreal sy = target.height() / source.height();
    qreal scale = qMin(sx, sy);
    qreal nw = source.width() * scale;
    qreal nh = source.height() * scale;
    qreal nx = target.x() + (target.width() - nw) / 2;
    qreal ny = target.y() + (target.height() - nh) / 2;
    return QRectF(nx, ny, nw, nh);
}

bool GuestDisplay::mapToGuest(const QPointF &pos, QPointF &guestPos) const {
    const QRectF rect = displayRect();
    if (rect.isEmpty() || !rect.contains(pos)) return false;

    const QSize guestSize = m_guestSize.isValid() ? m_guestSize : m_frame.size();
    const qreal scaleX = guestSize.width() / rect.width();
    const qreal scaleY = guestSize.height() / rect.height();
    const qreal gx = (pos.x() - rect.x()) * scaleX;
    const qreal gy = (pos.y() - rect.y()) * scaleY;
    guestPos = QPointF(std::clamp(gx, qreal(0), qreal(guestSize.width() - 1)),
                       std::clamp(gy, qreal(0), qreal(guestSize.height() - 1)));
    return true;
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
    forceActiveFocus();
    QPointF guestPos;
    if (!mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        true, event->button(), static_cast<int>(guestPos.x()), static_cast<int>(guestPos.y()));
    event->accept();
}

void GuestDisplay::mouseReleaseEvent(QMouseEvent *event) {
    QPointF guestPos;
    if (!mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        false, event->button(), static_cast<int>(guestPos.x()), static_cast<int>(guestPos.y()));
    event->accept();
}

void GuestDisplay::mouseMoveEvent(QMouseEvent *event) {
    QPointF guestPos;
    if (!mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
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
