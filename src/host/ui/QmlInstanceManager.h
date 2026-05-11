#pragma once

#include <QObject>
#include <QStringList>

namespace chimera {

/**
 * @brief QML-friendly wrapper around InstanceManager.
 */
class QmlInstanceManager : public QObject {
    Q_OBJECT
public:
    explicit QmlInstanceManager(QObject *parent = nullptr);

    Q_INVOKABLE QStringList listInstances() const;
    Q_INVOKABLE bool createInstance(const QString &name, int cpus, int ramMB, int width, int height);
    Q_INVOKABLE bool cloneInstance(const QString &sourceName, const QString &newName);
    Q_INVOKABLE bool deleteInstance(const QString &name);
    Q_INVOKABLE bool startInstance(const QString &name);
    Q_INVOKABLE bool stopInstance(const QString &name);
    Q_INVOKABLE int getInstanceState(const QString &name) const;

    // State enum for QML
    enum class VMState {
        Stopped = 0,
        Creating = 1,
        Created = 2,
        Starting = 3,
        Running = 4,
        Paused = 5,
        Stopping = 6,
        Error = 7
    };
    Q_ENUM(VMState)
};

} // namespace chimera
