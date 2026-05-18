#include "QmlInstanceManager.h"
#include "InstanceManager.h"

namespace chimera {

QmlInstanceManager::QmlInstanceManager(QObject *parent)
    : QObject(parent)
{
}

QStringList QmlInstanceManager::listInstances() const {
    QStringList list;
    for (const auto &name : chimera::instance::InstanceManager::instance().listInstances())
        list.append(QString::fromStdString(name));
    return list;
}

bool QmlInstanceManager::createInstance(const QString &name, int cpus, int ramMB,
                                         int width, int height) {
    chimera::instance::InstanceConfig cfg;
    cfg.name  = name.toStdString();
    cfg.cpus  = cpus;
    cfg.ramMB = ramMB;
    cfg.width = width;
    cfg.height= height;
    return chimera::instance::InstanceManager::instance().createInstance(cfg);
}

bool QmlInstanceManager::cloneInstance(const QString &sourceName, const QString &newName) {
    return chimera::instance::InstanceManager::instance().cloneInstance(
        sourceName.toStdString(), newName.toStdString());
}

bool QmlInstanceManager::deleteInstance(const QString &name) {
    return chimera::instance::InstanceManager::instance().deleteInstance(name.toStdString());
}

bool QmlInstanceManager::startInstance(const QString &name) {
    return chimera::instance::InstanceManager::instance().startInstance(name.toStdString());
}

bool QmlInstanceManager::stopInstance(const QString &name) {
    return chimera::instance::InstanceManager::instance().stopInstance(name.toStdString());
}

int QmlInstanceManager::getInstanceState(const QString &name) const {
    return static_cast<int>(
        chimera::instance::InstanceManager::instance().getInstanceState(name.toStdString()));
}

bool QmlInstanceManager::batchStart(const QStringList &names) {
    std::vector<std::string> ns;
    ns.reserve(static_cast<size_t>(names.size()));
    for (const auto &n : names) ns.push_back(n.toStdString());
    return chimera::instance::InstanceManager::instance().batchStartInstances(ns);
}

bool QmlInstanceManager::batchStop(const QStringList &names) {
    std::vector<std::string> ns;
    ns.reserve(static_cast<size_t>(names.size()));
    for (const auto &n : names) ns.push_back(n.toStdString());
    return chimera::instance::InstanceManager::instance().batchStopInstances(ns);
}

QVariantList QmlInstanceManager::instanceModel() const {
    QVariantList model;
    auto &mgr = chimera::instance::InstanceManager::instance();
    for (const auto &name : mgr.listInstances()) {
        const auto cfg = mgr.getInstanceConfig(name);
        const auto rc  = mgr.getRuntimeConfig(name);
        QVariantMap item;
        item[QStringLiteral("name")]        = QString::fromStdString(name);
        item[QStringLiteral("state")]       = static_cast<int>(mgr.getInstanceState(name));
        item[QStringLiteral("gridRow")]     = cfg.gridRow;
        item[QStringLiteral("gridCol")]     = cfg.gridCol;
        item[QStringLiteral("consolePort")] = rc.consolePort;
        item[QStringLiteral("adbPort")]     = rc.adbPort;
        item[QStringLiteral("grpcPort")]    = rc.grpcPort;
        item[QStringLiteral("adbSerial")]   = QString::fromStdString(rc.adbSerial);
        model.append(item);
    }
    return model;
}

void QmlInstanceManager::setGridPosition(const QString &name, int row, int col) {
    chimera::instance::InstanceManager::instance().setGridPosition(
        name.toStdString(), row, col);
}

void QmlInstanceManager::sortByName() {
    chimera::instance::InstanceManager::instance().sortByName();
}

QVariantMap QmlInstanceManager::instanceRuntimeConfig(const QString &name) const {
    const auto rc = chimera::instance::InstanceManager::instance().getRuntimeConfig(
        name.toStdString());
    QVariantMap m;
    m[QStringLiteral("consolePort")] = rc.consolePort;
    m[QStringLiteral("adbPort")]     = rc.adbPort;
    m[QStringLiteral("grpcPort")]    = rc.grpcPort;
    m[QStringLiteral("adbSerial")]   = QString::fromStdString(rc.adbSerial);
    return m;
}

} // namespace chimera
