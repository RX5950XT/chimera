#pragma once

#include <QQuickPaintedItem>
#include <QImage>
#include <QSize>
#include "CoordinateMapper.h"

namespace chimera {

/**
 * @brief QML item that renders the Android guest display framebuffer.
 *
 * Receives QImage updates from the graphics bridge and paints them
 * scaled to fit the item geometry.
 * Forwards mouse/keyboard events to InputBridge → ADB.
 */
class GuestDisplay : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)

public:
    explicit GuestDisplay(QQuickItem *parent = nullptr);

    QImage frame() const;
    void setFrame(const QImage &img);
    bool hasFrame() const;
    void setGuestSize(const QSize &size);
    void setRotation(int degrees);

    chimera::input::CoordinateMapper &coordinateMapper() { return m_mapper; }

    Q_INVOKABLE bool saveScreenshot(const QString &filePath) const;

signals:
    void frameChanged();

protected:
    void paint(QPainter *painter) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

protected:
    void geometryChange(const QRectF &newGeom, const QRectF &oldGeom) override;

private:
    QImage m_frame;
    QSize  m_guestSize;
    chimera::input::CoordinateMapper m_mapper;
};

} // namespace chimera
