#include "HyperVManager.h"
#include <QDebug>
#include <QSysInfo>
#include <QLibrary>

#ifdef Q_OS_WIN
#include <windows.h>
#include <combaseapi.h>
// HCS API types (minimal definitions to avoid SDK dependency)
typedef void* HCS_SYSTEM;
typedef void* HCS_OPERATION;
#define HCS_OPERATION_SUCCESS 0
#endif

using namespace chimera::instance;

// ---------------------------------------------------------------------------
// Private implementation
// ---------------------------------------------------------------------------
class HyperVManager::Impl {
public:
    bool running = false;
    QString errorString;
    HcsConfig config;

#ifdef Q_OS_WIN
    HCS_SYSTEM system = nullptr;

    // HCS function pointers (dynamically loaded)
    using HcsCreateOperationPtr = HCS_OPERATION (*)(void* completion, void* context);
    using HcsCloseOperationPtr = void (*)(HCS_OPERATION operation);
    using HcsGetOperationResultPtr = HRESULT (*)(HCS_OPERATION operation, HRESULT* result);
    using HcsCreateComputeSystemPtr = HRESULT (*)(const wchar_t* id, const wchar_t* configuration, HCS_OPERATION operation, HCS_SYSTEM* computeSystem);
    using HcsStartComputeSystemPtr = HRESULT (*)(HCS_SYSTEM computeSystem, HCS_OPERATION operation);
    using HcsShutDownComputeSystemPtr = HRESULT (*)(HCS_SYSTEM computeSystem, HCS_OPERATION operation);
    using HcsCloseComputeSystemPtr = HRESULT (*)(HCS_SYSTEM computeSystem);

    HcsCreateOperationPtr HcsCreateOperation = nullptr;
    HcsCloseOperationPtr HcsCloseOperation = nullptr;
    HcsGetOperationResultPtr HcsGetOperationResult = nullptr;
    HcsCreateComputeSystemPtr HcsCreateComputeSystem = nullptr;
    HcsStartComputeSystemPtr HcsStartComputeSystem = nullptr;
    HcsShutDownComputeSystemPtr HcsShutDownComputeSystem = nullptr;
    HcsCloseComputeSystemPtr HcsCloseComputeSystem = nullptr;

    bool loadHcs() {
        QLibrary computecore("computecore");
        if (!computecore.load()) {
            errorString = QStringLiteral("Failed to load computecore.dll: ") + computecore.errorString();
            return false;
        }

        HcsCreateOperation = (HcsCreateOperationPtr)computecore.resolve("HcsCreateOperation");
        HcsCloseOperation = (HcsCloseOperationPtr)computecore.resolve("HcsCloseOperation");
        HcsGetOperationResult = (HcsGetOperationResultPtr)computecore.resolve("HcsGetOperationResult");
        HcsCreateComputeSystem = (HcsCreateComputeSystemPtr)computecore.resolve("HcsCreateComputeSystem");
        HcsStartComputeSystem = (HcsStartComputeSystemPtr)computecore.resolve("HcsStartComputeSystem");
        HcsShutDownComputeSystem = (HcsShutDownComputeSystemPtr)computecore.resolve("HcsShutDownComputeSystem");
        HcsCloseComputeSystem = (HcsCloseComputeSystemPtr)computecore.resolve("HcsCloseComputeSystem");

        if (!HcsCreateOperation || !HcsCreateComputeSystem) {
            errorString = QStringLiteral("Failed to resolve HCS functions");
            return false;
        }
        return true;
    }
#endif
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
HyperVManager::HyperVManager(QObject *parent)
    : QObject(parent)
    , d(std::make_unique<Impl>()) {
}

HyperVManager::~HyperVManager() {
    if (isRunning()) {
        stopVm();
    }
}

bool HyperVManager::isAvailable() {
#ifdef Q_OS_WIN
    QLibrary computecore("computecore");
    if (!computecore.load()) {
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool HyperVManager::isGpuPartitionSupported() {
#ifdef Q_OS_WIN
    // Check for GPU partition support via registry
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SYSTEM\\CurrentControlSet\\Control\\DeviceGuard\\Scenarios\\HypervisorEnforcedCodeIntegrity",
                      0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS) {
        RegCloseKey(hKey);
    }

    // Check for GPU with partition support (simplified: any modern dGPU)
    // Real check would use DXCore or SetupAPI
    DISPLAY_DEVICEW dd = {};
    dd.cb = sizeof(dd);
    DWORD i = 0;
    while (EnumDisplayDevicesW(nullptr, i, &dd, 0)) {
        QString name = QString::fromWCharArray(dd.DeviceString);
        if (name.contains("NVIDIA", Qt::CaseInsensitive) ||
            name.contains("AMD", Qt::CaseInsensitive) ||
            name.contains("Intel", Qt::CaseInsensitive)) {
            if (!name.contains("Basic", Qt::CaseInsensitive)) {
                return true;
            }
        }
        ++i;
    }
#endif
    return false;
}

int HyperVManager::availableGpuPartitions() {
#ifdef Q_OS_WIN
    // Query GPU partition count from DriverStore
    // Simplified: return 1 if GPU partition is supported
    if (isGpuPartitionSupported()) {
        return 1; // Conservative default
    }
#endif
    return 0;
}

bool HyperVManager::createVm(const HcsConfig &config) {
    d->config = config;

#ifdef Q_OS_WIN
    if (!d->loadHcs()) {
        return false;
    }

    // Build HCS JSON configuration (placeholder)
    Q_UNUSED(config)
    QString gpuConfig;

    qDebug() << "HyperVManager: createVm not fully implemented yet";
    d->errorString = QStringLiteral("HCS VM creation is experimental and not yet implemented");
    return false;
#else
    d->errorString = QStringLiteral("HCS is only available on Windows");
    return false;
#endif
}

bool HyperVManager::startVm() {
#ifdef Q_OS_WIN
    if (!d->system) {
        d->errorString = QStringLiteral("VM not created");
        return false;
    }
    qDebug() << "HyperVManager: startVm not yet implemented";
    return false;
#else
    return false;
#endif
}

bool HyperVManager::stopVm() {
#ifdef Q_OS_WIN
    if (!d->system) return true;
    qDebug() << "HyperVManager: stopVm not yet implemented";
    d->running = false;
    emit stateChanged(false);
    return false;
#else
    return false;
#endif
}

bool HyperVManager::isRunning() const {
    return d->running;
}

QString HyperVManager::lastError() const {
    return d->errorString;
}
