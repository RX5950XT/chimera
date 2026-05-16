// Include winsock2.h BEFORE any other Windows headers to avoid fd_set redefinition.
#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <winsock2.h>
#  include <ws2def.h>
#  include <objbase.h>    // CLSIDFromString, GUID
#endif

#include "HvSocketFramebufferCapture.h"

#include <QDebug>
#include <QPointer>
#include <QSocketNotifier>
#include <thread>

using namespace chimera::graphics;

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

// Display bridge service GUID: {00000011-facb-11e6-bd58-64006a7986d3}
static constexpr GUID CHIMERA_HV_SERVICE_DISPLAY = {
    0x00000011u, 0xfacbu, 0x11e6u,
    { 0xbdu, 0x58u, 0x64u, 0x00u, 0x6au, 0x79u, 0x86u, 0xd3u }
};

#endif // _WIN32

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
class HvSocketFramebufferCapture::Impl {
public:
    QString vmIdString;
    bool    autoReconnect = false;
    int     reconnectMs   = 3000;
    bool    running       = false;

    // Frame parser state
    enum class ParseState { ReadingHeader, ReadingPixels };
    static constexpr quint32 HEADER_SIZE       = 8;
    static constexpr quint32 MAX_FRAME_DIMENSION = 7680;

    ParseState parseState          = ParseState::ReadingHeader;
    quint32    frameWidth          = 0;
    quint32    frameHeight         = 0;
    quint32    pixelBytesExpected  = 0;
    QByteArray readBuf;
    QByteArray pixelBuf;

    QSocketNotifier *notifier = nullptr;

#ifdef _WIN32
    SOCKET socket         = INVALID_SOCKET;
    GUID   vmGuid         = {};
    bool   winsockInited  = false;

    bool initWinsock(const std::function<void(const QString&)> &onError) {
        if (winsockInited) return true;
        WSADATA wd{};
        if (WSAStartup(MAKEWORD(2, 2), &wd) != 0) {
            onError(QStringLiteral("HvSocket-fb: WSAStartup failed"));
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
            onError(QStringLiteral("HvSocket-fb: invalid VM GUID: ") + vmIdString);
            return false;
        }
        return true;
    }

    void closeSocket() {
        delete notifier;
        notifier = nullptr;
        if (socket != INVALID_SOCKET) {
            closesocket(socket);
            socket = INVALID_SOCKET;
        }
    }
#else
    void closeSocket() { delete notifier; notifier = nullptr; }
#endif

    void resetParser() {
        parseState         = ParseState::ReadingHeader;
        frameWidth         = 0;
        frameHeight        = 0;
        pixelBytesExpected = 0;
        readBuf.clear();
        pixelBuf.clear();
    }
};

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
HvSocketFramebufferCapture::HvSocketFramebufferCapture(const QString &vmId,
                                                         QObject *parent)
    : FramebufferCapture(parent), d(std::make_unique<Impl>())
{
    d->vmIdString = vmId;
    auto *t = new QTimer(this);
    t->setSingleShot(true);
    t->setObjectName("reconnectTimer");
    connect(t, &QTimer::timeout, this, &HvSocketFramebufferCapture::onReconnectTimeout);
}

HvSocketFramebufferCapture::~HvSocketFramebufferCapture() {
    stop();
#ifdef _WIN32
    if (d->winsockInited) WSACleanup();
#endif
}

bool HvSocketFramebufferCapture::start() {
#ifndef _WIN32
    emit captureError(QStringLiteral("HvSocket-fb: Windows only"));
    return false;
#else
    if (d->running) return true;

    auto onErr = [this](const QString &m){ emit captureError(m); };
    if (!d->initWinsock(onErr)) return false;
    if (!d->parseVmId(onErr))   return false;

    d->running = true;

    SOCKET s = ::socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (s == INVALID_SOCKET) {
        const int err = WSAGetLastError();
        d->running = false;
        emit captureError(QStringLiteral("HvSocket-fb: socket() failed: %1").arg(err));
        if (d->autoReconnect) {
            if (auto *t = findChild<QTimer*>("reconnectTimer")) t->start(d->reconnectMs);
        }
        return false;
    }

    SOCKADDR_HV addr{};
    addr.Family    = AF_HYPERV;
    addr.VmId      = d->vmGuid;
    addr.ServiceId = CHIMERA_HV_SERVICE_DISPLAY;

    // Run blocking ::connect() in a background thread so the Qt event loop
    // stays responsive.  QPointer guard prevents use-after-free.
    QPointer<HvSocketFramebufferCapture> guard(this);
    std::thread([s, addr, guard, autoReconnect = d->autoReconnect,
                 reconnectMs = d->reconnectMs, vmId = d->vmIdString]() mutable {
        const int cr = ::connect(s, reinterpret_cast<const SOCKADDR *>(&addr), sizeof(addr));
        const int connectErr = (cr == SOCKET_ERROR) ? WSAGetLastError() : 0;

        QMetaObject::invokeMethod(guard, [s, connectErr, guard,
                                          autoReconnect, reconnectMs, vmId]() {
            if (!guard) { closesocket(s); return; }
            HvSocketFramebufferCapture *self = guard.data();

            if (connectErr != 0) {
                closesocket(s);
                self->d->running = false;
                emit self->captureError(
                    QStringLiteral("HvSocket-fb: connect() failed: %1").arg(connectErr));
                if (autoReconnect) {
                    auto *t = self->findChild<QTimer*>("reconnectTimer");
                    if (t) t->start(reconnectMs);
                }
                return;
            }

            self->d->socket = s;
            self->d->resetParser();

            self->d->notifier = new QSocketNotifier(
                static_cast<qintptr>(s), QSocketNotifier::Read, self);
            QObject::connect(self->d->notifier, &QSocketNotifier::activated,
                             self, &HvSocketFramebufferCapture::onSocketReadable);

            qDebug() << "HvSocket-fb: connected to VM" << vmId;
        }, Qt::QueuedConnection);
    }).detach();

    return true; // connect pending in background
#endif
}

void HvSocketFramebufferCapture::stop() {
    if (auto *t = findChild<QTimer*>("reconnectTimer")) t->stop();
    d->closeSocket();
    d->resetParser();
    d->running = false;
}

bool HvSocketFramebufferCapture::isRunning() const { return d->running; }

void HvSocketFramebufferCapture::setVmId(const QString &vmId) {
    d->vmIdString = vmId;
#ifdef _WIN32
    d->vmGuid = {};
#endif
}

void HvSocketFramebufferCapture::setAutoReconnect(bool enabled, int intervalMs) {
    d->autoReconnect = enabled;
    d->reconnectMs   = intervalMs;
}

// ---------------------------------------------------------------------------
// Private slots
// ---------------------------------------------------------------------------
void HvSocketFramebufferCapture::onSocketReadable() {
#ifdef _WIN32
    char tmp[65536];
    const int n = recv(d->socket, tmp, sizeof(tmp), 0);
    if (n == 0 || n == SOCKET_ERROR) {
        const int err = (n == SOCKET_ERROR) ? WSAGetLastError() : 0;
        qWarning() << "HvSocket-fb: connection closed, err=" << err;
        d->closeSocket();
        emit captureError(QStringLiteral("HvSocket-fb: peer closed (WSA %1)").arg(err));
        if (d->autoReconnect && d->running) {
            if (auto *t = findChild<QTimer*>("reconnectTimer")) t->start(d->reconnectMs);
        }
        return;
    }
    d->readBuf.append(tmp, n);
    processIncoming();
#endif
}

void HvSocketFramebufferCapture::onReconnectTimeout() {
    qDebug() << "HvSocket-fb: reconnecting...";
    d->resetParser();
    start();
}

// ---------------------------------------------------------------------------
// Frame parser
// ---------------------------------------------------------------------------
void HvSocketFramebufferCapture::processIncoming() {
    while (true) {
        if (d->parseState == Impl::ParseState::ReadingHeader) {
            if (d->readBuf.size() < static_cast<int>(Impl::HEADER_SIZE)) break;

            const auto *p = reinterpret_cast<const uint8_t *>(d->readBuf.constData());
            quint32 w = 0, h = 0;
            memcpy(&w, p,     4);
            memcpy(&h, p + 4, 4);

            if (w == 0 || h == 0 || w > Impl::MAX_FRAME_DIMENSION || h > Impl::MAX_FRAME_DIMENSION) {
                emit captureError(QStringLiteral("HvSocket-fb: invalid frame dimensions %1x%2").arg(w).arg(h));
                d->closeSocket();
                if (d->autoReconnect && d->running) {
                    if (auto *t = findChild<QTimer*>("reconnectTimer")) t->start(d->reconnectMs);
                }
                return;
            }

            d->frameWidth         = w;
            d->frameHeight        = h;
            d->pixelBytesExpected = w * h * 3u;
            d->readBuf.remove(0, static_cast<int>(Impl::HEADER_SIZE));
            d->parseState = Impl::ParseState::ReadingPixels;
        }

        if (d->parseState == Impl::ParseState::ReadingPixels) {
            const int needed = static_cast<int>(d->pixelBytesExpected);
            if (d->readBuf.size() < needed) break;

            const QImage frame(
                reinterpret_cast<const uchar *>(d->readBuf.constData()),
                static_cast<int>(d->frameWidth),
                static_cast<int>(d->frameHeight),
                static_cast<int>(d->frameWidth * 3u),
                QImage::Format_RGB888);

            emit frameReady(frame.copy());

            d->readBuf.remove(0, needed);
            d->parseState = Impl::ParseState::ReadingHeader;
        }
    }
}
