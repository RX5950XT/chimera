// Include winsock2.h BEFORE any other Windows headers to avoid fd_set redefinition.
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2def.h>
#  include <objbase.h>    // CLSIDFromString, GUID
#endif

#include "HvSocketTransport.h"

#include <QDebug>
#include <QPointer>
#include <QSocketNotifier>
#include <thread>

using namespace chimera::input;

// ---------------------------------------------------------------------------
// Windows-specific HvSocket types (only visible in this TU)
// ---------------------------------------------------------------------------
#ifdef _WIN32

#ifndef AF_HYPERV
static constexpr ADDRESS_FAMILY AF_HYPERV = 34;
#endif
#ifndef HV_PROTOCOL_RAW
static constexpr int HV_PROTOCOL_RAW = 1;
#endif

struct SOCKADDR_HV {
    ADDRESS_FAMILY Family;
    USHORT         Reserved;
    GUID           VmId;
    GUID           ServiceId;
};
static_assert(sizeof(SOCKADDR_HV) == 36, "SOCKADDR_HV layout mismatch");

// Input bridge service GUID: {00000010-facb-11e6-bd58-64006a7986d3}
static constexpr GUID CHIMERA_HV_SERVICE_INPUT = {
    0x00000010u, 0xfacbu, 0x11e6u,
    { 0xbdu, 0x58u, 0x64u, 0x00u, 0x6au, 0x79u, 0x86u, 0xd3u }
};

// Linux input_event (16 bytes, matches 64-bit kernel struct)
#pragma pack(push, 1)
struct LinuxInputEvent {
    int64_t  tv_sec;
    uint16_t type;
    uint16_t code;
    int32_t  value;
};
static_assert(sizeof(LinuxInputEvent) == 16, "LinuxInputEvent layout mismatch");
#pragma pack(pop)

// EV_* / BTN_* / ABS_* constants
static constexpr uint16_t EV_SYN  = 0x00, EV_KEY = 0x01, EV_ABS = 0x03;
static constexpr uint16_t SYN_REPORT = 0x00;
static constexpr uint16_t ABS_X = 0x00, ABS_Y = 0x01;
static constexpr uint16_t BTN_LEFT = 0x110, BTN_RIGHT = 0x111, BTN_MIDDLE = 0x112;

static LinuxInputEvent makeSyn() { return {0, EV_SYN, SYN_REPORT, 0}; }

#endif // _WIN32

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
class HvSocketTransport::Impl {
public:
    QString vmIdString;
    bool    autoReconnect = false;
    int     reconnectMs   = 3000;
    int     lastMoveX     = -1;
    int     lastMoveY     = -1;

    QSocketNotifier *notifier = nullptr;

#ifdef _WIN32
    SOCKET socket       = INVALID_SOCKET;
    GUID   vmGuid       = {};
    bool   winsockInited = false;
    bool   connected    = false;

    bool initWinsock(const std::function<void(const QString&)> &onError) {
        if (winsockInited) return true;
        WSADATA wd{};
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
            onError(QStringLiteral("HvSocket: WSAStartup failed"));
            return false;
        }
        winsockInited = true;
        return true;
    }

    bool parseVmId(const std::function<void(const QString&)> &onError) {
        if (vmIdString.isEmpty()) { vmGuid = {}; return true; }
        // CLSIDFromString requires {xxxxxxxx-...} with braces
        QString id = vmIdString.trimmed();
        if (!id.startsWith(QLatin1Char('{'))) id = QLatin1Char('{') + id + QLatin1Char('}');
        HRESULT hr = CLSIDFromString(reinterpret_cast<LPCOLESTR>(id.utf16()), &vmGuid);
        if (FAILED(hr)) {
            onError(QStringLiteral("HvSocket: invalid VM GUID: ") + vmIdString);
            return false;
        }
        return true;
    }

    void closeSocket() {
        delete notifier;
        notifier  = nullptr;
        connected = false;
        lastMoveX = lastMoveY = -1;
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }
#else
    bool connected = false;
    void closeSocket() { delete notifier; notifier = nullptr; connected = false; }
#endif
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
HvSocketTransport::HvSocketTransport(const QString &vmId, QObject *parent)
    : QObject(parent), d(std::make_unique<Impl>())
{
    d->vmIdString = vmId;
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    connect(t, &QTimer::timeout, this, &HvSocketTransport::onReconnectTimeout);
    t->setObjectName("reconnectTimer");
}

void HvSocketTransport::setVmId(const QString &vmId) {
    d->vmIdString = vmId;
#ifdef _WIN32
    d->vmGuid = {};
#endif
}

HvSocketTransport::~HvSocketTransport() {
    disconnect();
#ifdef _WIN32
    if (d->winsockInited) WSACleanup();
#endif
}

bool HvSocketTransport::connectToVm() {
#ifndef _WIN32
    emit error(QStringLiteral("HvSocket: Windows only"));
    return false;
#else
    if (d->connected) return true;

    auto onErr = [this](const QString &m){ emit error(m); };
    if (!d->initWinsock(onErr)) return false;
    if (!d->parseVmId(onErr))   return false;

    SOCKET s = ::socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET) {
        const int err = WSAGetLastError();
        emit error(QStringLiteral("HvSocket: socket() failed: %1").arg(err));
        return false;
    }

    SOCKADDR_HV addr{};
    addr.Family    = AF_HYPERV;
    addr.VmId      = d->vmGuid;
    addr.ServiceId = CHIMERA_HV_SERVICE_INPUT;

    // Run blocking ::connect() in a background thread so the Qt event loop
    // stays responsive.  QPointer guard prevents use-after-free if this
    // QObject is destroyed before the thread reports back.
    QPointer<HvSocketTransport> guard(this);
    std::thread([s, addr, guard, autoReconnect = d->autoReconnect,
                 reconnectMs = d->reconnectMs, vmId = d->vmIdString]() mutable {
        const int cr = ::connect(s, reinterpret_cast<const SOCKADDR *>(&addr), sizeof(addr));
        const int connectErr = (cr == SOCKET_ERROR) ? WSAGetLastError() : 0;

        QMetaObject::invokeMethod(guard, [s, connectErr, guard,
                                          autoReconnect, reconnectMs, vmId]() {
            if (!guard) { closesocket(s); return; }
            HvSocketTransport *self = guard.data();

            if (connectErr != 0) {
                closesocket(s);
                emit self->error(QStringLiteral("HvSocket: connect() failed: %1").arg(connectErr));
                if (autoReconnect) {
                    auto *t = self->findChild<QTimer*>("reconnectTimer");
                    if (t) t->start(reconnectMs);
                }
                return;
            }

            self->d->socket    = s;
            self->d->connected = true;

            self->d->notifier = new QSocketNotifier(
                static_cast<qintptr>(s), QSocketNotifier::Read, self);
            QObject::connect(self->d->notifier, &QSocketNotifier::activated,
                             self, &HvSocketTransport::onSocketReadable);

            qDebug() << "HvSocket input: connected to" << vmId;
            emit self->connected();
        }, Qt::QueuedConnection);
    }).detach();

    return true; // connect pending in background
#endif
}

void HvSocketTransport::disconnect() {
    if (auto *t = findChild<QTimer*>("reconnectTimer")) t->stop();
    const bool was = d->connected;
    d->closeSocket();
    if (was) emit disconnected();
}

bool HvSocketTransport::isConnected() const { return d->connected; }

void HvSocketTransport::setAutoReconnect(bool enabled, int intervalMs) {
    d->autoReconnect = enabled;
    d->reconnectMs   = intervalMs;
}

// ---------------------------------------------------------------------------
// Input injection
// ---------------------------------------------------------------------------
bool HvSocketTransport::sendKey(int linuxKeyCode, bool pressed) {
#ifndef _WIN32
    Q_UNUSED(linuxKeyCode); Q_UNUSED(pressed); return false;
#else
    if (!d->connected) return false;
    LinuxInputEvent evts[2] = {
        {0, EV_KEY, static_cast<uint16_t>(linuxKeyCode), pressed ? 1 : 0},
        makeSyn()
    };
    const int n = ::send(d->socket, reinterpret_cast<const char*>(evts), sizeof(evts), 0);
    return n != SOCKET_ERROR;
#endif
}

bool HvSocketTransport::sendMouseMove(int x, int y) {
#ifndef _WIN32
    Q_UNUSED(x); Q_UNUSED(y); return false;
#else
    if (!d->connected) return false;
    x = qBound(0, x, 32767);
    y = qBound(0, y, 32767);
    if (x == d->lastMoveX && y == d->lastMoveY) return true;
    d->lastMoveX = x; d->lastMoveY = y;
    LinuxInputEvent evts[3] = {
        {0, EV_ABS, ABS_X, x},
        {0, EV_ABS, ABS_Y, y},
        makeSyn()
    };
    const int n = ::send(d->socket, reinterpret_cast<const char*>(evts), sizeof(evts), 0);
    return n != SOCKET_ERROR;
#endif
}

bool HvSocketTransport::sendMouseButton(int qtButton, bool pressed) {
#ifndef _WIN32
    Q_UNUSED(qtButton); Q_UNUSED(pressed); return false;
#else
    if (!d->connected) return false;
    uint16_t code;
    switch (qtButton) {
    case 0x1: code = BTN_LEFT;   break;
    case 0x2: code = BTN_RIGHT;  break;
    case 0x4: code = BTN_MIDDLE; break;
    default:  return true;
    }
    LinuxInputEvent evts[2] = {{0, EV_KEY, code, pressed ? 1 : 0}, makeSyn()};
    const int n = ::send(d->socket, reinterpret_cast<const char*>(evts), sizeof(evts), 0);
    return n != SOCKET_ERROR;
#endif
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void HvSocketTransport::onSocketReadable() {
#ifdef _WIN32
    char buf[64];
    const int n = recv(d->socket, buf, sizeof(buf), 0);
    if (n == 0 || n == SOCKET_ERROR) {
        qDebug() << "HvSocket: guest closed connection";
        d->closeSocket();
        emit disconnected();
        if (auto *t = findChild<QTimer*>("reconnectTimer"))
            if (d->autoReconnect) t->start(d->reconnectMs);
    }
#endif
}

void HvSocketTransport::onReconnectTimeout() {
    qDebug() << "HvSocket input: reconnecting...";
    connectToVm();
}
