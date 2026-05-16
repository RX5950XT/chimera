#include "HyperVManager.h"
#include <QDebug>
#include <QLibrary>
#include <QPointer>
#include <QUuid>
#include <QMetaObject>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <mutex>
#include <functional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace chimera::instance;
using json = nlohmann::json;

// ---------------------------------------------------------------------------
// HCS opaque handle types
// ---------------------------------------------------------------------------
#ifdef Q_OS_WIN
typedef void* HCS_SYSTEM;
typedef void* HCS_OPERATION;

// HCS_E_OPERATION_PENDING — HCS async in-progress code (0x8037001b)
static constexpr HRESULT HCS_E_OP_PENDING = static_cast<HRESULT>(0x8037001b);

// computecore.dll function pointer types (Windows 10 1803+ / build 17134+)
using FnHcsCreateOperation       = HCS_OPERATION (WINAPI*)(void* ctx, void* callback);
using FnHcsCloseOperation        = void          (WINAPI*)(HCS_OPERATION);
using FnHcsWaitForOpResult       = HRESULT       (WINAPI*)(HCS_OPERATION, DWORD, wchar_t**);
using FnHcsCreateComputeSystem   = HRESULT       (WINAPI*)(const wchar_t* id,
                                                            const wchar_t* config,
                                                            HCS_OPERATION,
                                                            const void* secDesc,
                                                            HCS_SYSTEM*);
using FnHcsStartComputeSystem    = HRESULT       (WINAPI*)(HCS_SYSTEM, HCS_OPERATION,
                                                            const wchar_t* options);
using FnHcsShutDownComputeSystem = HRESULT       (WINAPI*)(HCS_SYSTEM, HCS_OPERATION,
                                                            const wchar_t* options);
using FnHcsTerminateComputeSystem= HRESULT       (WINAPI*)(HCS_SYSTEM, HCS_OPERATION,
                                                            const wchar_t* options);
using FnHcsCloseComputeSystem    = HRESULT       (WINAPI*)(HCS_SYSTEM);

// computestorage.dll — grants VM SID access to VHDX files
using FnHcsGrantVmAccess         = HRESULT       (WINAPI*)(const wchar_t* vmId,
                                                            const wchar_t* filePath);
#endif // Q_OS_WIN

// ---------------------------------------------------------------------------
// Private implementation
// ---------------------------------------------------------------------------
class HyperVManager::Impl {
public:
    HcsConfig config;
    QString   vmId;
    std::atomic<State> currentState{State::Idle};
    std::atomic<bool>  aborted{false};
    std::thread        workerThread;

    mutable std::mutex errorMutex;
    QString            errorStringInternal;

    void setError(const QString &msg) {
        std::lock_guard<std::mutex> lk(errorMutex);
        errorStringInternal = msg;
    }
    QString getError() const {
        std::lock_guard<std::mutex> lk(errorMutex);
        return errorStringInternal;
    }

#ifdef Q_OS_WIN
    HCS_SYSTEM system = nullptr;

    QLibrary computecore{"computecore"};
    QLibrary computestorage{"computestorage"};

    FnHcsCreateOperation        fnCreateOp    = nullptr;
    FnHcsCloseOperation         fnCloseOp     = nullptr;
    FnHcsWaitForOpResult        fnWaitResult  = nullptr;
    FnHcsCreateComputeSystem    fnCreateSys   = nullptr;
    FnHcsStartComputeSystem     fnStartSys    = nullptr;
    FnHcsShutDownComputeSystem  fnShutDown    = nullptr;
    FnHcsTerminateComputeSystem fnTerminate   = nullptr;
    FnHcsCloseComputeSystem     fnCloseSys    = nullptr;
    FnHcsGrantVmAccess          fnGrantAccess = nullptr;

    bool hcsLoaded = false;

    bool loadHcs() {
        if (hcsLoaded) return true;
        if (!computecore.load()) {
            setError("computecore.dll not available: " + computecore.errorString());
            return false;
        }
        auto res = [&](QLibrary &lib, const char *sym) -> QFunctionPointer {
            QFunctionPointer p = lib.resolve(sym);
            if (!p) qWarning() << "HCS: unresolved symbol" << sym;
            return p;
        };
        fnCreateOp   = (FnHcsCreateOperation)       res(computecore, "HcsCreateOperation");
        fnCloseOp    = (FnHcsCloseOperation)        res(computecore, "HcsCloseOperation");
        fnWaitResult = (FnHcsWaitForOpResult)       res(computecore, "HcsWaitForOperationResult");
        fnCreateSys  = (FnHcsCreateComputeSystem)   res(computecore, "HcsCreateComputeSystem");
        fnStartSys   = (FnHcsStartComputeSystem)    res(computecore, "HcsStartComputeSystem");
        fnShutDown   = (FnHcsShutDownComputeSystem) res(computecore, "HcsShutDownComputeSystem");
        fnTerminate  = (FnHcsTerminateComputeSystem)res(computecore, "HcsTerminateComputeSystem");
        fnCloseSys   = (FnHcsCloseComputeSystem)    res(computecore, "HcsCloseComputeSystem");

        if (!fnCreateOp || !fnCreateSys || !fnWaitResult || !fnStartSys) {
            setError("Required HCS functions not found in computecore.dll");
            return false;
        }
        if (computestorage.load())
            fnGrantAccess = (FnHcsGrantVmAccess)res(computestorage, "HcsGrantVmAccess");

        hcsLoaded = true;
        return true;
    }

    // Run an HCS async operation and wait synchronously (call from worker thread only).
    // Returns S_OK on success or an HRESULT error.
    HRESULT runOperation(std::function<HRESULT(HCS_OPERATION)> fn,
                         DWORD timeoutMs = 60000) {
        HCS_OPERATION op = fnCreateOp(nullptr, nullptr);
        if (!op) return E_OUTOFMEMORY;

        HRESULT initHr = fn(op);

        // Skip wait only on hard synchronous failures (not S_OK or HCS_E_OP_PENDING)
        if (FAILED(initHr) && initHr != HCS_E_OP_PENDING) {
            fnCloseOp(op);
            return initHr;
        }

        wchar_t *resultDoc = nullptr;
        HRESULT hr = fnWaitResult(op, timeoutMs, &resultDoc);
        if (FAILED(hr) && resultDoc)
            qWarning() << "HCS op result:" << QString::fromWCharArray(resultDoc);
        if (resultDoc) LocalFree(resultDoc);
        fnCloseOp(op);
        return hr;
    }

    // Force-terminate the VM inline (safe to call from any thread; no signal emissions).
    void forceTerminate() {
        if (!system || !hcsLoaded || !fnCreateOp || !fnTerminate) return;
        HCS_OPERATION op = fnCreateOp(nullptr, nullptr);
        if (!op) return;
        fnTerminate(system, op, nullptr);
        wchar_t *r = nullptr;
        fnWaitResult(op, 5000, &r);
        if (r) LocalFree(r);
        fnCloseOp(op);
    }

    void grantDiskAccess(const QString &path) {
        if (!fnGrantAccess || path.isEmpty()) return;
        HRESULT hr = fnGrantAccess(vmId.toStdWString().c_str(),
                                   path.toStdWString().c_str());
        if (FAILED(hr))
            qWarning() << "HcsGrantVmAccess failed for" << path
                       << "hr=" << Qt::hex << static_cast<quint32>(hr);
    }
#endif // Q_OS_WIN
};

// ---------------------------------------------------------------------------
// HCS JSON builder
// ---------------------------------------------------------------------------
QString HyperVManager::buildHcsJsonString(const HcsConfig &cfg) {
    json doc;
    doc["SchemaVersion"] = {{"Major", 2}, {"Minor", 1}};
    doc["Owner"] = "Chimera";
    doc["ShouldTerminateOnLastHandleClosed"] = true;

    auto &vm = doc["VirtualMachine"];
    vm["StopOnReset"] = true;

    auto &chipset = vm["Chipset"];
    chipset["Uefi"]["ApplyDefaultRuntimeFirmware"] = false;
    if (!cfg.kernelPath.isEmpty()) {
        auto &lkd = chipset["LinuxKernelDirect"];
        lkd["KernelFilePath"] = cfg.kernelPath.toStdString();
        if (!cfg.initrdPath.isEmpty())
            lkd["InitRdPath"] = cfg.initrdPath.toStdString();
        if (!cfg.kernelCmdLine.isEmpty())
            lkd["KernelCmdLine"] = cfg.kernelCmdLine.toStdString();
    }

    auto &topo = vm["ComputeTopology"];
    topo["Memory"]["SizeInMB"]        = cfg.ramMB;
    topo["Memory"]["AllowOvercommit"] = true;
    topo["Processor"]["Count"]        = cfg.cpus;

    auto &scsi = vm["Devices"]["Scsi"]["primary"]["Attachments"];
    int idx = 0;
    for (const auto &path : cfg.readonlyDiskPaths) {
        scsi[std::to_string(idx++)] = {
            {"Type", "VirtualDisk"}, {"Path", path.toStdString()}, {"ReadOnly", true}
        };
    }
    if (!cfg.writableDiskPath.isEmpty()) {
        scsi[std::to_string(idx)] = {
            {"Type", "VirtualDisk"}, {"Path", cfg.writableDiskPath.toStdString()}, {"ReadOnly", false}
        };
    }

    vm["Devices"]["HvSocket"]["HvSocketConfig"]["DefaultBindSecurityDescriptor"] =
        "D:P(A;;FA;;;SY)(A;;FA;;;BA)";

    if (cfg.gpuMode != GpuNone) {
        vm["Devices"]["GpuP"]["AssignmentMode"]       = "Mirror";
        vm["Devices"]["GpuP"]["AllowVendorExtension"] = true;
    }

    return QString::fromStdString(doc.dump(2));
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------
HyperVManager::HyperVManager(QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>()) {}

HyperVManager::~HyperVManager() {
    d->aborted = true;

#ifdef Q_OS_WIN
    // Unblock any pending HcsWaitForOperationResult in the worker thread
    d->forceTerminate();
    if (d->system && d->fnCloseSys) {
        d->fnCloseSys(d->system);
        d->system = nullptr;
    }
#endif

    // Safe to join — forceTerminate above causes any blocking wait to return
    if (d->workerThread.joinable()) d->workerThread.join();
}

// ---------------------------------------------------------------------------
// Static capability queries
// ---------------------------------------------------------------------------
bool HyperVManager::isAvailable() {
#ifdef Q_OS_WIN
    QLibrary lib("computecore");
    return lib.load();
#else
    return false;
#endif
}

bool HyperVManager::isGpuPartitionSupported() {
#ifdef Q_OS_WIN
    DISPLAY_DEVICEW dd{};
    dd.cb = sizeof(dd);
    for (DWORD i = 0; EnumDisplayDevicesW(nullptr, i, &dd, 0); ++i) {
        QString name = QString::fromWCharArray(dd.DeviceString);
        bool candidate = (name.contains("NVIDIA", Qt::CaseInsensitive) ||
                          name.contains("AMD",    Qt::CaseInsensitive) ||
                          name.contains("Intel",  Qt::CaseInsensitive)) &&
                         !name.contains("Basic",  Qt::CaseInsensitive) &&
                         !name.contains("Remote", Qt::CaseInsensitive);
        if (candidate) return true;
        dd = {};
        dd.cb = sizeof(dd);
    }
#endif
    return false;
}

int HyperVManager::availableGpuPartitions() {
    return isGpuPartitionSupported() ? 1 : 0;
}

// ---------------------------------------------------------------------------
// VM lifecycle helpers
// ---------------------------------------------------------------------------
namespace {
// Emit stateChanged safely: must be called from worker thread only.
// Captures QPointer (created on main thread before spawning) to guard against
// HyperVManager being deleted between the QMetaObject post and main-thread dispatch.
void emitStateAsync(HyperVManager *self,
                    const QPointer<HyperVManager> &guard,
                    HyperVManager::State s,
                    const std::atomic<bool> &aborted) {
    if (aborted) return;
    QMetaObject::invokeMethod(self, [guard, s]() {
        if (guard) emit guard->stateChanged(s);
    }, Qt::QueuedConnection);
}

void emitErrorAsync(HyperVManager *self,
                    const QPointer<HyperVManager> &guard,
                    const QString &msg,
                    const std::atomic<bool> &aborted) {
    if (aborted) return;
    QMetaObject::invokeMethod(self, [guard, msg]() {
        if (guard) emit guard->error(msg);
    }, Qt::QueuedConnection);
}
} // anonymous namespace

// ---------------------------------------------------------------------------
// createVm
// ---------------------------------------------------------------------------
bool HyperVManager::createVm(const HcsConfig &config) {
    if (d->currentState != State::Idle && d->currentState != State::Error) {
        d->setError("VM already exists — call terminateVm() first");
        return false;
    }
#ifndef Q_OS_WIN
    d->setError("HCS is Windows-only");
    return false;
#else
    if (!d->loadHcs()) {
        emit error(d->getError());
        return false;
    }

    d->config = config;
    d->vmId   = QUuid::createUuid().toString(QUuid::WithBraces);
    d->currentState = State::Creating;
    emit stateChanged(State::Creating);

    for (const auto &p : config.readonlyDiskPaths) d->grantDiskAccess(p);
    if (!config.writableDiskPath.isEmpty())         d->grantDiskAccess(config.writableDiskPath);

    std::wstring jsonW  = buildHcsJsonString(config).toStdWString();
    std::wstring vmIdW  = d->vmId.toStdWString();
    QPointer<HyperVManager> guard(this);  // created on main thread

    if (d->workerThread.joinable()) d->workerThread.join();
    d->workerThread = std::thread([this, guard, jsonW, vmIdW]() {
        HRESULT hr = d->runOperation([&](HCS_OPERATION op) {
            return d->fnCreateSys(vmIdW.c_str(), jsonW.c_str(), op, nullptr, &d->system);
        });

        if (SUCCEEDED(hr)) {
            d->currentState = State::Stopped;
            emitStateAsync(this, guard, State::Stopped, d->aborted);
        } else {
            const QString msg = QStringLiteral("HcsCreateComputeSystem failed: 0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
            d->setError(msg);
            d->currentState = State::Error;
            emitErrorAsync(this, guard, msg, d->aborted);
            emitStateAsync(this, guard, State::Error, d->aborted);
        }
    });
    return true;
#endif
}

// ---------------------------------------------------------------------------
// startVm
// ---------------------------------------------------------------------------
bool HyperVManager::startVm() {
    if (d->currentState != State::Stopped) {
        d->setError("startVm() requires State::Stopped");
        return false;
    }
#ifndef Q_OS_WIN
    return false;
#else
    if (!d->system) {
        d->setError("No system handle — call createVm() first");
        return false;
    }

    d->currentState = State::Starting;
    emit stateChanged(State::Starting);

    QPointer<HyperVManager> guard(this);
    if (d->workerThread.joinable()) d->workerThread.join();
    d->workerThread = std::thread([this, guard]() {
        HRESULT hr = d->runOperation([&](HCS_OPERATION op) {
            return d->fnStartSys(d->system, op, nullptr);
        });

        if (SUCCEEDED(hr)) {
            d->currentState = State::Running;
            emitStateAsync(this, guard, State::Running, d->aborted);
        } else {
            const QString msg = QStringLiteral("HcsStartComputeSystem failed: 0x%1")
                .arg(static_cast<quint32>(hr), 8, 16, QChar('0'));
            d->setError(msg);
            d->currentState = State::Error;
            emitErrorAsync(this, guard, msg, d->aborted);
            emitStateAsync(this, guard, State::Error, d->aborted);
        }
    });
    return true;
#endif
}

// ---------------------------------------------------------------------------
// stopVm
// ---------------------------------------------------------------------------
bool HyperVManager::stopVm() {
    if (d->currentState != State::Running) return false;
#ifndef Q_OS_WIN
    return false;
#else
    d->currentState = State::Stopping;
    emit stateChanged(State::Stopping);

    QPointer<HyperVManager> guard(this);
    if (d->workerThread.joinable()) d->workerThread.join();
    d->workerThread = std::thread([this, guard]() {
        HRESULT hr = d->runOperation([&](HCS_OPERATION op) {
            return d->fnShutDown(d->system, op, nullptr);
        }, 10000);

        if (SUCCEEDED(hr)) {
            d->currentState = State::Stopped;
            emitStateAsync(this, guard, State::Stopped, d->aborted);
        } else {
            qWarning() << "HcsShutDownComputeSystem failed (0x"
                       << Qt::hex << static_cast<quint32>(hr) << "); forcing terminate";
            // Inline force-terminate (safe from worker thread; no signal from forceTerminate)
            d->forceTerminate();
            d->currentState = State::Terminated;
            emitStateAsync(this, guard, State::Terminated, d->aborted);
        }
    });
    return true;
#endif
}

// ---------------------------------------------------------------------------
// terminateVm
// ---------------------------------------------------------------------------
bool HyperVManager::terminateVm() {
    const State s = d->currentState.load();
    if (s == State::Idle || s == State::Terminated) return false;
#ifndef Q_OS_WIN
    return false;
#else
    if (!d->hcsLoaded || !d->fnCreateOp || !d->fnTerminate) return false;

    // forceTerminate is safe to call from any thread and emits no signals
    d->forceTerminate();
    d->currentState = State::Terminated;
    // Must emit from main thread
    emit stateChanged(State::Terminated);
    return true;
#endif
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------
HyperVManager::State HyperVManager::state() const { return d->currentState.load(); }
bool    HyperVManager::isRunning()  const { return d->currentState == State::Running; }
QString HyperVManager::lastError()  const { return d->getError(); }
