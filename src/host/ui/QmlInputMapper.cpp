#include "QmlInputMapper.h"
#include "InputMapper.h"

#include <QDir>
#include <QFileInfo>

namespace chimera {

QmlInputMapper::QmlInputMapper(QObject *parent)
    : QObject(parent)
{}

QVariantList QmlInputMapper::mappings() const {
    QVariantList list;
    for (const auto &m : input::InputMapper::instance().getMappings()) {
        QVariantMap item;
        item[QStringLiteral("type")]     = QString::fromStdString(m.type);
        item[QStringLiteral("key")]      = QString::fromStdString(m.key);
        item[QStringLiteral("altKey")]   = QString::fromStdString(m.altKey);
        item[QStringLiteral("x")]        = static_cast<double>(m.x);
        item[QStringLiteral("y")]        = static_cast<double>(m.y);
        item[QStringLiteral("guidance")] = QString::fromStdString(m.guidance);
        list.append(item);
    }
    return list;
}

QStringList QmlInputMapper::listSchemes() const {
    QStringList names;
    QDir dir(QStringLiteral("configs/input"));
    if (!dir.exists()) return names;
    for (const QFileInfo &fi : dir.entryInfoList({QStringLiteral("*.json")}, QDir::Files))
        names.append(fi.completeBaseName());
    return names;
}

bool QmlInputMapper::loadScheme(const QString &packageName) {
    bool ok = input::InputMapper::instance().loadScheme(packageName.toStdString());
    if (ok) {
        m_currentScheme = packageName;
        emit currentSchemeChanged();
        emit mappingsChanged();
    }
    return ok;
}

bool QmlInputMapper::saveScheme(const QString &packageName) {
    return input::InputMapper::instance().saveScheme(packageName.toStdString());
}

void QmlInputMapper::addTapMapping(const QString &key, double xPct, double yPct,
                                    const QString &label) {
    input::InputMapping m;
    m.type = "tap";
    m.key  = key.toStdString();
    m.x    = static_cast<float>(xPct);
    m.y    = static_cast<float>(yPct);
    m.guidance = label.toStdString();
    input::InputMapper::instance().addMapping(m);
    emit mappingsChanged();
}

void QmlInputMapper::removeMapping(int index) {
    if (index < 0) return;
    input::InputMapper::instance().removeMapping(static_cast<size_t>(index));
    emit mappingsChanged();
}

void QmlInputMapper::clearMappings() {
    input::InputMapper::instance().clearMappings();
    emit mappingsChanged();
}

void QmlInputMapper::updateMappingPosition(int index, double xPct, double yPct) {
    auto &mapper = input::InputMapper::instance();
    const auto &mappings = mapper.getMappings();
    if (index < 0 || static_cast<size_t>(index) >= mappings.size()) return;
    input::InputMapping m = mappings[static_cast<size_t>(index)];
    m.x = static_cast<float>(xPct);
    m.y = static_cast<float>(yPct);
    mapper.removeMapping(static_cast<size_t>(index));
    mapper.insertMapping(static_cast<size_t>(index), m);
    emit mappingsChanged();
}

} // namespace chimera
