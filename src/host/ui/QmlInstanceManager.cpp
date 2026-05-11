#include "QmlInstanceManager.h"
#include "InstanceManager.h"

namespace chimera {

QmlInstanceManager::QmlInstanceManager(QObject *parent)
    : QObject(parent)
{
}

QStringList QmlInstanceManager::listInstances() const {
    QStringList list;
    for (auto &name : chimera::instance::InstanceManager::instance().listInstances()) {
        list.append(QString::fromStdString(name));
    }
    return list;
}

bool QmlInstanceManager::createInstance(const QString &name, int cpus, int ramMB, int width, int height) {
    chimera::instance::InstanceConfig cfg;
    cfg.name = name.toStdString();
    cfg.cpus = cpus;
    cfg.ramMB = ramMB;
    cfg.width = width;
    cfg.height = height;
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
    return static_cast<int>(chimera::instance::InstanceManager::instance().getInstanceState(name.toStdString()));
}

} // namespace chimera
