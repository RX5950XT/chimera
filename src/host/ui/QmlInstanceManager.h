#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace chimera {

/**
 * @brief QML-friendly wrapper around InstanceManager.
 *
 * Exposes multi-instance grid operations, batch start/stop, per-instance
 * port assignments, and sort-by-name for the BlueStacks-parity UI.
 */
class QmlInstanceManager : public QObject {
    Q_OBJECT
public:
    explicit QmlInstanceManager(QObject *parent = nullptr);

    // Basic CRUD
    Q_INVOKABLE QStringList listInstances() const;
    Q_INVOKABLE bool createInstance(const QString &name, int cpus, int ramMB, int width, int height);
    Q_INVOKABLE bool cloneInstance(const QString &sourceName, const QString &newName);
    Q_INVOKABLE bool deleteInstance(const QString &name);

    // Lifecycle
    Q_INVOKABLE bool startInstance(const QString &name);
    Q_INVOKABLE bool stopInstance(const QString &name);
    Q_INVOKABLE int  getInstanceState(const QString &name) const;

    // Batch operations
    Q_INVOKABLE bool batchStart(const QStringList &names);
    Q_INVOKABLE bool batchStop(const QStringList &names);

    // Grid model: returns list of {name, state, gridRow, gridCol, consolePort, adbPort, grpcPort}
    Q_INVOKABLE QVariantList instanceModel() const;

    // Grid layout
    Q_INVOKABLE void setGridPosition(const QString &name, int row, int col);

    // Sort
    Q_INVOKABLE void sortByName();

    // Per-instance runtime config (ports)
    Q_INVOKABLE QVariantMap instanceRuntimeConfig(const QString &name) const;

    // Full instance config (cpus, ramMB, width, height, maxFps, dpi, graphicsEngine, graphicsRenderer)
    Q_INVOKABLE QVariantMap instanceFullConfig(const QString &name) const;

    // Update FPS cap for an existing instance (takes effect on next start)
    Q_INVOKABLE bool updateInstanceFps(const QString &name, int maxFps);

    // Toggle root mode (takes effect on next start)
    Q_INVOKABLE bool setEnableRoot(const QString &name, bool enabled);

    // Toggle audio output (takes effect on next start)
    Q_INVOKABLE bool setEnableAudio(const QString &name, bool enabled);

    // Set device spoofing profile by name (takes effect on next start)
    Q_INVOKABLE bool setDeviceProfile(const QString &name, const QString &profileName);

    // Returns built-in device profile names (for UI selector)
    Q_INVOKABLE QStringList availableDeviceProfiles() const;

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
