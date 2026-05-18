#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace chimera {

/**
 * @brief QML-friendly wrapper around InputMapper.
 *
 * Exposes per-game key binding schemes: list, load, add, remove, save.
 */
class QmlInputMapper : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList mappings READ mappings NOTIFY mappingsChanged)
    Q_PROPERTY(QString currentScheme READ currentScheme NOTIFY currentSchemeChanged)

public:
    explicit QmlInputMapper(QObject *parent = nullptr);

    QVariantList mappings() const;
    QString currentScheme() const { return m_currentScheme; }

    Q_INVOKABLE QStringList listSchemes() const;
    Q_INVOKABLE bool loadScheme(const QString &packageName);
    Q_INVOKABLE bool saveScheme(const QString &packageName);

    // Add a tap binding: key → screen position (0–100 percent)
    Q_INVOKABLE void addTapMapping(const QString &key, double xPct, double yPct,
                                   const QString &label = {});
    Q_INVOKABLE void removeMapping(int index);
    Q_INVOKABLE void clearMappings();

signals:
    void mappingsChanged();
    void currentSchemeChanged();

private:
    QString m_currentScheme;
};

} // namespace chimera
