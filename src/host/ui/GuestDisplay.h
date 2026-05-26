#pragma once

#include <QQuickItem>
#include <QImage>
#include <QSize>
#include <QPointF>
#include <QString>
#include <memory>
#include "CoordinateMapper.h"

namespace chimera {

/**
 * @brief QML item that renders the Android guest display framebuffer.
 *
 * Receives frame updates from the graphics bridge and uploads them directly
 * as Qt scene graph textures, avoiding the QQuickPaintedItem/QPainter path.
 * Forwards mouse/keyboard events to InputBridge → ADB.
 */
class GuestDisplay : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QImage frame READ frame WRITE setFrame NOTIFY frameChanged)
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY frameChanged)
    Q_PROPERTY(bool mouseLocked READ isMouseLocked NOTIFY mouseLockChanged)
    Q_PROPERTY(int cursorMode READ cursorMode WRITE setCursorMode NOTIFY cursorModeChanged)

public:
    explicit GuestDisplay(QQuickItem *parent = nullptr);
    ~GuestDisplay() override;

    QImage frame() const;
    void setFrame(const QImage &img);
    bool hasFrame() const;
    void setNativeD3D11Texture(void *texture, const QSize &size, quint64 sequence, bool hasAlpha);
    void setSharedD3D11Texture(const QString &textureName, const QSize &size, quint64 sequence, bool hasAlpha);
    void clearNativeD3D11Texture();
    void setGuestSize(const QSize &size);
    void setRotation(int degrees);

    chimera::input::CoordinateMapper &coordinateMapper() { return m_mapper; }

    Q_INVOKABLE bool saveScreenshot(const QString &filePath) const;
    Q_INVOKABLE void setMouseLocked(bool locked);
    bool isMouseLocked() const { return m_mouseLocked; }

    // cursorMode: 0=default arrow, 1=crosshair (game aim mode)
    Q_INVOKABLE void setCursorMode(int mode);
    int cursorMode() const { return m_cursorMode; }

signals:
    void frameChanged();
    void framePainted();
    void mouseLockChanged();
    void cursorModeChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void touchEvent(QTouchEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    void geometryChange(const QRectF &newGeom, const QRectF &oldGeom) override;

private:
    QImage m_frame;
    quint64 m_frameSequence = 0;
    void  *m_nativeD3D11Texture = nullptr;
    QSize  m_nativeD3D11TextureSize;
    quint64 m_nativeD3D11TextureSequence = 0;
    bool   m_nativeD3D11TextureHasAlpha = false;
    QString m_sharedD3D11TextureName;
    struct NativeD3D11TextureState;
    std::unique_ptr<NativeD3D11TextureState> m_nativeD3D11State;
    std::unique_ptr<NativeD3D11TextureState> m_uploadD3D11State;
    QSize  m_guestSize;
    chimera::input::CoordinateMapper m_mapper;
    bool   m_mouseLocked = false;
    QPointF m_virtualMouse;   // virtual cursor position in guest coordinates (FPS mode)
    int    m_cursorMode = 0;  // 0=arrow, 1=crosshair
};

} // namespace chimera
