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
    const bool sizeChanged = m_frame.isNull() || img.size() != m_frame.size();
    m_frame = img;
    if (sizeChanged && !m_frame.isNull() && !m_guestSize.isValid()) {
        m_mapper.setGuestSize(m_frame.width(), m_frame.height());
    }
    update();
    if (sizeChanged) emit frameChanged();
}

bool GuestDisplay::hasFrame() const {
    return !m_frame.isNull();
}

void GuestDisplay::setGuestSize(const QSize &size) {
    if (size.width() <= 0 || size.height() <= 0 || m_guestSize == size) return;
    m_guestSize = size;
    m_mapper.setGuestSize(size.width(), size.height());
}

void GuestDisplay::setRotation(int degrees) {
    m_mapper.setRotation(degrees);
    update();
}

void GuestDisplay::geometryChange(const QRectF &newGeom, const QRectF &oldGeom) {
    QQuickPaintedItem::geometryChange(newGeom, oldGeom);
    m_mapper.setHostViewSize(static_cast<int>(newGeom.width()),
                             static_cast<int>(newGeom.height()));
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
    const QRectF dr = m_mapper.displayRect();
    if (dr.isEmpty()) {
        // Mapper not configured yet — fall back to full-rect stretch
        painter->drawImage(boundingRect().toRect(), m_frame);
        return;
    }
    painter->drawImage(dr, m_frame, QRectF(0, 0, m_frame.width(), m_frame.height()));
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
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        true, event->button(), guestPos.x(), guestPos.y());
    event->accept();
}

void GuestDisplay::mouseReleaseEvent(QMouseEvent *event) {
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseButton(
        false, event->button(), guestPos.x(), guestPos.y());
    event->accept();
}

void GuestDisplay::mouseMoveEvent(QMouseEvent *event) {
    QPoint guestPos;
    if (!m_mapper.mapToGuest(event->position(), guestPos)) {
        event->accept();
        return;
    }
    chimera::input::InputBridge::instance().onMouseMove(guestPos.x(), guestPos.y(), 0, 0);
    event->accept();
}

void GuestDisplay::wheelEvent(QWheelEvent *event) {
    auto delta = event->angleDelta();
    chimera::input::InputBridge::instance().onWheel(delta.x(), delta.y());
    event->accept();
}

} // namespace chimera
