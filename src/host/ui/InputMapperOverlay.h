#pragma once

#include <QQuickPaintedItem>
#include <QVector2D>
#include <QMap>

namespace chimera {

/**
 * @brief On-screen overlay for input mapping controls (tap zones, D-pads, etc.).
 *
 * Renders semi-transparent control hints over the guest display.
 * Editable via the input mapper editor.
 */
class InputMapperOverlay : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(bool visible READ isVisible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)

public:
    explicit InputMapperOverlay(QQuickItem *parent = nullptr);

    bool isVisible() const;
    void setVisible(bool visible);

    Q_INVOKABLE void loadScheme(const QString &packageName);
    Q_INVOKABLE void saveScheme();
    Q_INVOKABLE void addTapControl(QVector2D normPos, const QString &key);
    Q_INVOKABLE void removeControl(int index);

signals:
    void visibleChanged();
    void opacityChanged();
    void controlActivated(int index, const QString &type);

protected:
    void paint(QPainter *painter) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;

private:
    struct Control {
        QString type;      // "tap", "swipe", "dpad", "moba_skill"
        QRectF rect;       // Normalized [0,1] coordinates
        QString key;       // Keyboard key or gamepad button
        QString altKey;    // Alternative binding
        QString guidance;  // Display label
    };

    bool m_visible = true;
    qreal m_opacity = 0.5;
    QList<Control> m_controls;
    QString m_currentPackage;
};

} // namespace chimera
