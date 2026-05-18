#include "GuestDisplay.h"
#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTouchEvent>
#include <QInputMethodEvent>
#include <QGuiApplication>
#include <QCursor>
#include <QQuickWindow>
#include <QDebug>
#include <algorithm>
#include "InputBridge.h"

namespace chimera {

GuestDisplay::GuestDisplay(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setRenderTarget(QQuickPaintedItem::FramebufferObject);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
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

void GuestDisplay::setMouseLocked(bool locked) {
    if (m_mouseLocked == locked) return;
    m_mouseLocked = locked;
    if (locked) {
        // Initialize virtual cursor at widget center
        m_virtualMouse = QPointF(width() / 2.0, height() / 2.0);
        QGuiApplication::setOverrideCursor(Qt::BlankCursor);
        if (window()) {
            const QPoint globalCenter = window()->mapToGlobal(
                mapToScene(m_virtualMouse).toPoint());
            QCursor::setPos(globalCenter);
        }
    } else {
        QGuiApplication::restoreOverrideCursor();
        // Restore item cursor based on current cursor mode
        if (m_cursorMode == 1) setCursor(Qt::CrossCursor);
        else unsetCursor();
    }
    emit mouseLockChanged();
}

void GuestDisplay::setCursorMode(int mode) {
    if (m_cursorMode == mode) return;
    m_cursorMode = mode;
    if (!m_mouseLocked) {
        if (mode == 1) setCursor(Qt::CrossCursor);
        else unsetCursor();
    }
    emit cursorModeChanged();
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
        painter->drawImage(boundingRect().toRect(), m_frame);
    } else {
        painter->drawImage(dr, m_frame, QRectF(0, 0, m_frame.width(), m_frame.height()));
    }
    emit framePainted();
}

void GuestDisplay::keyPressEvent(QKeyEvent *event) {
    // Escape unlocks mouse (always, so users can escape FPS lock)
    if (m_mouseLocked && event->key() == Qt::Key_Escape) {
        setMouseLocked(false);
        event->accept();
        return;
    }
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
    if (m_mouseLocked) {
        // FPS relative mode: compute delta from widget center, accumulate into virtual cursor
        const QPointF center(width() / 2.0, height() / 2.0);
        const QPointF delta = event->position() - center;
        if (!delta.isNull()) {
            m_virtualMouse.rx() = qBound(0.0, m_virtualMouse.x() + delta.x(), width());
            m_virtualMouse.ry() = qBound(0.0, m_virtualMouse.y() + delta.y(), height());
            QPoint guestPos;
            if (m_mapper.mapToGuest(m_virtualMouse, guestPos))
                chimera::input::InputBridge::instance().onMouseMove(
                    guestPos.x(), guestPos.y(), 0, 0);
            // Warp physical cursor back to center
            if (window()) {
                const QPoint globalCenter = window()->mapToGlobal(
                    mapToScene(center).toPoint());
                QCursor::setPos(globalCenter);
            }
        }
        event->accept();
        return;
    }
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

void GuestDisplay::touchEvent(QTouchEvent *event) {
    for (const QEventPoint &tp : event->points()) {
        if (tp.state() == QEventPoint::Stationary) continue;
        const bool active = (tp.state() != QEventPoint::Released);
        QPoint guestPos;
        if (active && !m_mapper.mapToGuest(tp.position(), guestPos)) {
            chimera::input::InputBridge::instance().onTouchPoint(
                static_cast<int>(tp.id()), 0, 0, false);
            continue;
        }
        chimera::input::InputBridge::instance().onTouchPoint(
            static_cast<int>(tp.id()),
            active ? guestPos.x() : 0,
            active ? guestPos.y() : 0,
            active);
    }
    event->accept();
}

void GuestDisplay::inputMethodEvent(QInputMethodEvent *event) {
    const QString committed = event->commitString();
    if (!committed.isEmpty()) {
        chimera::input::InputBridge::instance().onTextInput(
            committed.toStdString());
    }
    event->accept();
}

QVariant GuestDisplay::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImEnabled) return QVariant(true);
    return QQuickPaintedItem::inputMethodQuery(query);
}

} // namespace chimera
