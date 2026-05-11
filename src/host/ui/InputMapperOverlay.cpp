#include "InputMapperOverlay.h"
#include "InputMapper.h"
#include <QPainter>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDir>

namespace chimera {

InputMapperOverlay::InputMapperOverlay(QQuickItem *parent)
    : QQuickPaintedItem(parent), m_visible(true), m_opacity(0.5)
{
}

bool InputMapperOverlay::isVisible() const {
    return m_visible;
}

void InputMapperOverlay::setVisible(bool visible) {
    if (m_visible != visible) {
        m_visible = visible;
        emit visibleChanged();
        update();
    }
}

void InputMapperOverlay::loadScheme(const QString &packageName) {
    m_currentPackage = packageName;
    m_controls.clear();
    if (chimera::input::InputMapper::instance().loadScheme(packageName.toStdString())) {
        for (auto &m : chimera::input::InputMapper::instance().getMappings()) {
            Control ctrl;
            ctrl.type = QString::fromStdString(m.type);
            ctrl.rect = QRectF(m.x / 100.0 - m.width / 200.0,
                               m.y / 100.0 - m.height / 200.0,
                               m.width / 100.0,
                               m.height / 100.0);
            ctrl.key = QString::fromStdString(m.key);
            ctrl.altKey = QString::fromStdString(m.altKey);
            ctrl.guidance = QString::fromStdString(m.guidance);
            m_controls.append(ctrl);
        }
    }
    update();
}

void InputMapperOverlay::saveScheme() {
    if (m_currentPackage.isEmpty()) return;
    auto &mapper = chimera::input::InputMapper::instance();
    mapper.clearMappings();
    for (const auto &ctrl : m_controls) {
        chimera::input::InputMapping m;
        m.type = ctrl.type.toStdString();
        m.x = static_cast<float>((ctrl.rect.x() + ctrl.rect.width() / 2.0) * 100.0);
        m.y = static_cast<float>((ctrl.rect.y() + ctrl.rect.height() / 2.0) * 100.0);
        m.width = static_cast<float>(ctrl.rect.width() * 100.0);
        m.height = static_cast<float>(ctrl.rect.height() * 100.0);
        m.key = ctrl.key.toStdString();
        m.altKey = ctrl.altKey.toStdString();
        m.guidance = ctrl.guidance.toStdString();
        mapper.addMapping(m);
    }
    mapper.saveScheme(m_currentPackage.toStdString());
}

void InputMapperOverlay::addTapControl(QVector2D normPos, const QString &key) {
    Control ctrl;
    ctrl.type = QStringLiteral("tap");
    ctrl.rect = QRectF(normPos.x() - 0.05, normPos.y() - 0.05, 0.1, 0.1);
    ctrl.key = key;
    ctrl.guidance = key;
    m_controls.append(ctrl);
    update();
}

void InputMapperOverlay::removeControl(int index) {
    if (index >= 0 && index < m_controls.size()) {
        m_controls.removeAt(index);
        update();
    }
}

void InputMapperOverlay::paint(QPainter *painter) {
    if (!m_visible || m_controls.isEmpty()) return;

    painter->setOpacity(m_opacity);
    for (const auto &ctrl : m_controls) {
        QRectF screenRect(ctrl.rect.x() * width(),
                          ctrl.rect.y() * height(),
                          ctrl.rect.width() * width(),
                          ctrl.rect.height() * height());
        painter->setPen(QPen(Qt::white, 2));
        painter->setBrush(QColor(0, 150, 255, 120));
        if (ctrl.type == QStringLiteral("tap")) {
            painter->drawEllipse(screenRect);
        } else {
            painter->drawRect(screenRect);
        }
        painter->setPen(Qt::white);
        painter->drawText(screenRect, Qt::AlignCenter, ctrl.guidance);
    }
}

void InputMapperOverlay::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    update();
}

} // namespace chimera
