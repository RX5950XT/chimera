#include "VncFramebufferCapture.h"
#include <QDebug>
#include <cstring>

using namespace chimera::graphics;

namespace {

// RFB protocol constants
constexpr char RFB_VERSION_3_8[] = "RFB 003.008\n";
constexpr uint8_t SECURITY_TYPE_NONE = 1;
constexpr uint32_t ENCODING_RAW = 0;
// Pseudo-encodings (signed int32, sent as big-endian in the wire format)
constexpr int32_t ENCODING_DESKTOP_SIZE     = -223;  // server framebuffer resize
constexpr int32_t ENCODING_EXT_DESKTOP_SIZE = -308;  // client-driven resize (0xFFFFFECC)
constexpr int32_t ENCODING_CURSOR           = -239;  // cursor shape update (ignore)

// Read N bytes from buffer, return true if enough data
bool consumeBytes(QByteArray &buffer, int n, QByteArray &out) {
    if (buffer.size() < n) return false;
    out = buffer.left(n);
    buffer.remove(0, n);
    return true;
}

uint16_t readU16BE(const uint8_t *p) {
    return (static_cast<uint16_t>(p[0]) << 8) | p[1];
}

uint32_t readU32BE(const uint8_t *p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           p[3];
}

} // anonymous namespace

VncFramebufferCapture::VncFramebufferCapture(const QString &host, int port,
                                              QObject *parent)
    : FramebufferCapture(parent)
    , m_host(host)
    , m_port(port)
    , m_socket(new QTcpSocket(this))
    , m_updateTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::connected, this, &VncFramebufferCapture::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &VncFramebufferCapture::onSocketDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &VncFramebufferCapture::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead, this, &VncFramebufferCapture::onSocketReadyRead);
    connect(m_updateTimer, &QTimer::timeout, this, &VncFramebufferCapture::requestFrameUpdate);
    m_updateTimer->setInterval(500); // stall watchdog only

    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        if (!m_running && m_autoReconnect) {
            qDebug() << "VNC: auto-reconnecting to" << m_host << m_port;
            start();
        }
    });
}

VncFramebufferCapture::~VncFramebufferCapture() {
    m_autoReconnect = false;
    stop();
}

void VncFramebufferCapture::setAutoReconnect(bool enable, int intervalMs) {
    m_autoReconnect = enable;
    m_reconnectTimer->setInterval(intervalMs);
}

void VncFramebufferCapture::setDesiredResolution(int w, int h) {
    m_desiredWidth  = (w > 0) ? w : 0;
    m_desiredHeight = (h > 0) ? h : 0;
}

bool VncFramebufferCapture::start() {
    if (m_running) return true;
    // Ensure socket is clean before (re-)connecting
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_readBuffer.clear();
    m_updateInFlight = false;
    m_waitingForRectPixels = false;
    m_resizedThisUpdate = false;
    m_running = true;
    m_state = State::Disconnected;
    m_socket->connectToHost(m_host, m_port);
    return true;
}

void VncFramebufferCapture::stop() {
    m_running = false;
    m_updateTimer->stop();
    m_updateInFlight = false;
    m_waitingForRectPixels = false;
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    } else {
        m_socket->abort();
    }
}

bool VncFramebufferCapture::isRunning() const {
    return m_running;
}

void VncFramebufferCapture::onSocketConnected() {
    qDebug() << "VNC: Connected to" << m_host << m_port;
    m_state = State::Handshake;
    m_readBuffer.clear();
}

void VncFramebufferCapture::onSocketDisconnected() {
    qDebug() << "VNC: Disconnected";
    m_running = false;
    m_updateTimer->stop();
    m_updateInFlight = false;
    m_waitingForRectPixels = false;
    m_state = State::Disconnected;
    if (m_autoReconnect) m_reconnectTimer->start();
}

void VncFramebufferCapture::onSocketError(QAbstractSocket::SocketError error) {
    qWarning() << "VNC socket error:" << error << m_socket->errorString();
    emit captureError(QStringLiteral("VNC connection error: ") + m_socket->errorString());
    m_running = false;
    if (m_autoReconnect) m_reconnectTimer->start();
}

void VncFramebufferCapture::onSocketReadyRead() {
    m_readBuffer.append(m_socket->readAll());

    while (m_running) {
        switch (m_state) {
            case State::Handshake: {
                if (m_readBuffer.size() < 12) return;
                QByteArray version = m_readBuffer.left(12);
                m_readBuffer.remove(0, 12);
                qDebug() << "VNC server version:" << version.trimmed();
                m_socket->write(RFB_VERSION_3_8, 12);
                m_state = State::Security;
                break;
            }
            case State::Security: {
                if (m_readBuffer.size() < 1) return;
                uint8_t numTypes = static_cast<uint8_t>(m_readBuffer[0]);
                if (numTypes == 0) {
                    // Error string follows
                    if (m_readBuffer.size() < 4) return;
                    uint32_t len = readU32BE(reinterpret_cast<const uint8_t*>(m_readBuffer.constData() + 1));
                    if (m_readBuffer.size() < static_cast<int>(5 + len)) return;
                    QString reason = QString::fromLatin1(m_readBuffer.mid(5, len));
                    emit captureError(QStringLiteral("VNC security error: ") + reason);
                    stop();
                    return;
                }
                if (m_readBuffer.size() < 1 + numTypes) return;
                bool hasNone = false;
                for (int i = 0; i < numTypes; ++i) {
                    if (static_cast<uint8_t>(m_readBuffer[1 + i]) == SECURITY_TYPE_NONE) {
                        hasNone = true; break;
                    }
                }
                m_readBuffer.remove(0, 1 + numTypes);
                if (!hasNone) {
                    emit captureError(QStringLiteral("VNC server does not support None security"));
                    stop(); return;
                }
                uint8_t selectNone = SECURITY_TYPE_NONE;
                m_socket->write(reinterpret_cast<const char*>(&selectNone), 1);
                m_state = State::SecurityResult;
                break;
            }
            case State::SecurityResult: {
                if (m_readBuffer.size() < 4) return;
                uint32_t result = readU32BE(reinterpret_cast<const uint8_t*>(m_readBuffer.constData()));
                m_readBuffer.remove(0, 4);
                if (result != 0) {
                    emit captureError(QStringLiteral("VNC authentication failed"));
                    stop(); return;
                }
                sendClientInit();
                m_state = State::ClientInit;
                break;
            }
            case State::ClientInit:
                // Wait for ServerInit (20 bytes header + name length)
                if (m_readBuffer.size() < 24) return;
                processServerInit(m_readBuffer);
                break;
            case State::Ready:
                // Wait for FramebufferUpdate
                if (m_readBuffer.size() < 4) return;
                processFramebufferUpdate(m_readBuffer);
                break;
            case State::FramebufferUpdate:
                processRectangles(m_readBuffer);
                if (m_state == State::FramebufferUpdate) return;
                break;
            default:
                return;
        }
    }
}

void VncFramebufferCapture::sendClientInit() {
    uint8_t shared = 1;
    m_socket->write(reinterpret_cast<const char*>(&shared), 1);
}

void VncFramebufferCapture::processServerInit(QByteArray &buffer) {
    const uint8_t *p = reinterpret_cast<const uint8_t*>(buffer.constData());
    m_fbWidth = readU16BE(p);
    m_fbHeight = readU16BE(p + 2);
    m_bitsPerPixel = p[4];
    m_depth = p[5];
    m_bigEndian = p[6] != 0;
    m_trueColour = p[7] != 0;
    m_redMax = readU16BE(p + 8);
    m_greenMax = readU16BE(p + 10);
    m_blueMax = readU16BE(p + 12);
    m_redShift = p[14];
    m_greenShift = p[15];
    m_blueShift = p[16];
    uint32_t nameLen = readU32BE(p + 20);

    int totalLen = 24 + nameLen;
    if (buffer.size() < totalLen) return;

    buffer.remove(0, totalLen);

    qDebug() << "VNC framebuffer:" << m_fbWidth << "x" << m_fbHeight
             << "bpp:" << m_bitsPerPixel << "depth:" << m_depth;

    m_framebuffer = QImage(m_fbWidth, m_fbHeight, QImage::Format_RGB32);
    m_framebuffer.fill(Qt::black);
    m_state = State::Ready;
    sendPixelFormat();
    sendEncodings();
    sendFramebufferUpdateRequest(false);
    if (m_desiredWidth > 0 && m_desiredHeight > 0 &&
        (m_desiredWidth != m_fbWidth || m_desiredHeight != m_fbHeight)) {
        sendDesktopSizeRequest(m_desiredWidth, m_desiredHeight);
    }
    m_updateTimer->start(); // 500ms stall watchdog
}

void VncFramebufferCapture::sendPixelFormat() {
    // RFB SetPixelFormat layout: 1 byte type, 3 bytes padding, 16 bytes pixel-format
    uint8_t msg[20] = {};
    msg[0] = 0;   // message-type: SetPixelFormat
    // msg[1..3] = padding (zeroed)
    msg[4] = 32;  // bits-per-pixel
    msg[5] = 24;  // depth
    msg[6] = 0;   // big-endian-flag: 0 = little-endian
    msg[7] = 1;   // true-colour-flag
    // red-max = 255 (big-endian uint16)
    msg[8]  = 0;  msg[9]  = 255;
    // green-max = 255
    msg[10] = 0;  msg[11] = 255;
    // blue-max = 255
    msg[12] = 0;  msg[13] = 255;
    msg[14] = 16; // red-shift
    msg[15] = 8;  // green-shift
    msg[16] = 0;  // blue-shift
    // msg[17..19] = padding (zeroed)
    m_socket->write(reinterpret_cast<const char*>(msg), 20);
}

void VncFramebufferCapture::sendEncodings() {
    // RAW pixels + DesktopSize + ExtendedDesktopSize (enables client SetDesktopSize) + Cursor
    const uint8_t msg[] = {
        2,                          // SetEncodings
        0,                          // padding
        0, 4,                       // number-of-encodings = 4
        0x00, 0x00, 0x00, 0x00,     // RAW = 0
        0xFF, 0xFF, 0xFF, 0x21,     // DesktopSize = -223
        0xFF, 0xFF, 0xFE, 0xCC,     // ExtendedDesktopSize = -308
        0xFF, 0xFF, 0xFF, 0x11,     // Cursor = -239
    };
    m_socket->write(reinterpret_cast<const char*>(msg), sizeof(msg));
}

void VncFramebufferCapture::sendFramebufferUpdateRequest(bool incremental) {
    if (m_updateInFlight) return;

    uint8_t msg[10] = {};
    msg[0] = 3; // FramebufferUpdateRequest
    msg[1] = incremental ? 1 : 0;
    msg[2] = 0; msg[3] = 0; // x
    msg[4] = 0; msg[5] = 0; // y
    msg[6] = static_cast<uint8_t>(m_fbWidth >> 8);
    msg[7] = static_cast<uint8_t>(m_fbWidth & 0xFF);
    msg[8] = static_cast<uint8_t>(m_fbHeight >> 8);
    msg[9] = static_cast<uint8_t>(m_fbHeight & 0xFF);
    m_socket->write(reinterpret_cast<const char*>(msg), 10);
    m_updateInFlight = true;
    m_updateInFlightTicks = 0; // reset timeout counter
}

void VncFramebufferCapture::processFramebufferUpdate(QByteArray &buffer) {
    if (buffer.size() < 4) return;
    const uint8_t *p = reinterpret_cast<const uint8_t*>(buffer.constData());
    // msg type (1 byte), padding (1 byte), numRectangles (2 bytes)
    uint16_t numRects = readU16BE(p + 2);
    buffer.remove(0, 4);
    m_rectanglesRemaining = numRects;
    m_waitingForRectPixels = false;
    m_resizedThisUpdate = false;
    m_state = State::FramebufferUpdate;
    processRectangles(buffer);
}

void VncFramebufferCapture::processRectangles(QByteArray &buffer) {
    while (m_rectanglesRemaining > 0) {
        if (!m_waitingForRectPixels) {
            if (buffer.size() < 12) return;
            const uint8_t *p = reinterpret_cast<const uint8_t*>(buffer.constData());
            m_rectX = readU16BE(p);
            m_rectY = readU16BE(p + 2);
            m_rectW = readU16BE(p + 4);
            m_rectH = readU16BE(p + 6);
            m_rectEncoding = readU32BE(p + 8);
            buffer.remove(0, 12);

            const int32_t encSigned = static_cast<int32_t>(m_rectEncoding);

            // Resize pseudo-encodings use rect x/y as status/error codes, not pixel coords.
            // Skip bounds check for them.
            if (encSigned != ENCODING_DESKTOP_SIZE && encSigned != ENCODING_EXT_DESKTOP_SIZE &&
                encSigned != ENCODING_CURSOR) {
                if (m_rectX + m_rectW > m_fbWidth || m_rectY + m_rectH > m_fbHeight) {
                    emit captureError(QStringLiteral("VNC rectangle exceeds framebuffer bounds"));
                    stop();
                    return;
                }
            }

            if (encSigned == ENCODING_DESKTOP_SIZE || encSigned == ENCODING_EXT_DESKTOP_SIZE) {
                // ExtendedDesktopSize carries a screen-array payload that must be drained.
                if (encSigned == ENCODING_EXT_DESKTOP_SIZE) {
                    if (buffer.isEmpty()) return;
                    const int numScreens = static_cast<uint8_t>(buffer.at(0));
                    const int payload = 4 + numScreens * 16; // 1+3pad + 16 per screen
                    if (buffer.size() < payload) return;
                    buffer.remove(0, payload);
                }
                // Only treat as a resize when dimensions actually change.
                // QEMU includes ExtDesktopSize in every FBU response (informational), so
                // marking m_resizedThisUpdate unconditionally creates an infinite loop:
                //   receive same-size ExtDesktopSize → send non-incremental → repeat.
                const bool dimensionsChanged =
                    (static_cast<int>(m_rectW) != m_fbWidth ||
                     static_cast<int>(m_rectH) != m_fbHeight);
                m_fbWidth  = m_rectW;
                m_fbHeight = m_rectH;
                if (dimensionsChanged) {
                    m_framebuffer = QImage(m_fbWidth, m_fbHeight, QImage::Format_RGB32);
                    m_framebuffer.fill(Qt::black);
                    m_resizedThisUpdate = true;
                }
                qDebug() << "VNC resize:" << m_fbWidth << "x" << m_fbHeight;
                m_rectanglesRemaining--;
                // Don't return early — QEMU can bundle more rects (e.g. Cursor) in the same
                // FramebufferUpdate. An early return here causes the remaining rect header bytes
                // to be mis-parsed as a new FramebufferUpdate message → protocol desync.
                continue;
            }

            if (encSigned == ENCODING_CURSOR) {
                // Cursor shape update: skip pixel data + bitmask, no screen change.
                const int bytesPerPixel = m_bitsPerPixel / 8;
                const int cursorBytes =
                    m_rectW * m_rectH * bytesPerPixel +
                    ((m_rectW + 7) / 8) * m_rectH;
                if (buffer.size() < cursorBytes) return;
                buffer.remove(0, cursorBytes);
                m_rectanglesRemaining--;
                continue;
            }

            if (m_rectEncoding != ENCODING_RAW) {
                emit captureError(QStringLiteral("Unsupported VNC encoding: ") + QString::number(m_rectEncoding));
                stop();
                return;
            }

            const int bytesPerPixel = m_bitsPerPixel / 8;
            if (bytesPerPixel != 4 || m_rectW == 0 || m_rectH == 0) {
                emit captureError(QStringLiteral("Unsupported VNC pixel format"));
                stop();
                return;
            }
            m_rectBytesRemaining = m_rectW * m_rectH * bytesPerPixel;
            m_waitingForRectPixels = true;
        }

        if (buffer.size() < m_rectBytesRemaining) return;

        const auto *pixels = reinterpret_cast<const uchar*>(buffer.constData());
        for (int y = 0; y < m_rectH; ++y) {
            auto *dst = reinterpret_cast<QRgb*>(m_framebuffer.scanLine(m_rectY + y)) + m_rectX;
            const uchar *src = pixels + (y * m_rectW * 4);
            for (int x = 0; x < m_rectW; ++x) {
                dst[x] = qRgb(src[x * 4 + 2], src[x * 4 + 1], src[x * 4]);
            }
        }
        buffer.remove(0, m_rectBytesRemaining);
        m_waitingForRectPixels = false;
        m_rectanglesRemaining--;
    }

    if (m_rectanglesRemaining == 0) {
        emit frameReady(m_framebuffer.copy());
        m_state = State::Ready;
        m_updateInFlight = false;
        const bool wasResize = m_resizedThisUpdate;
        m_resizedThisUpdate = false;
        // After a resize, send non-incremental so QEMU fills the new framebuffer dimensions.
        // Otherwise, incremental keeps the continuous-update loop alive efficiently.
        sendFramebufferUpdateRequest(!wasResize);
    }
}

void VncFramebufferCapture::requestFrameUpdate() {
    if (m_state != State::Ready ||
        m_socket->state() != QAbstractSocket::ConnectedState) return;

    if (!m_updateInFlight) {
        m_updateInFlightTicks = 0;
        sendFramebufferUpdateRequest(true);
    } else if (++m_updateInFlightTicks >= 4) {
        // 4 × 500 ms = 2 s without a response.
        // QEMU virtio-gpu 2D holds VNC updates until the guest calls RESOURCE_FLUSH.
        // Force a non-incremental re-request to break the deadlock; QEMU will respond
        // with whatever is currently in the scanout buffer (even if all-black).
        qDebug() << "VNC: watchdog forcing non-incremental re-request (no response for 2s)";
        m_updateInFlight = false;
        m_updateInFlightTicks = 0;
        sendFramebufferUpdateRequest(false);
    }
}

void VncFramebufferCapture::sendDesktopSizeRequest(int w, int h) {
    if (w <= 0 || h <= 0 || m_socket->state() != QAbstractSocket::ConnectedState) return;
    const auto uw = static_cast<uint16_t>(w);
    const auto uh = static_cast<uint16_t>(h);
    // RFB SetDesktopSize (type 251) — 26 bytes total:
    //   [0]   type=251   [1]   pad
    //   [2-3] width      [4-5] height
    //   [6]   num_screens=1   [7-9] pad (3 bytes)
    //   [10-13] screen_id=0   [14-15] x=0   [16-17] y=0
    //   [18-19] screen_w      [20-21] screen_h
    //   [22-25] flags=0
    uint8_t msg[26] = {};
    msg[0]  = 251;
    msg[2]  = static_cast<uint8_t>(uw >> 8);
    msg[3]  = static_cast<uint8_t>(uw & 0xFF);
    msg[4]  = static_cast<uint8_t>(uh >> 8);
    msg[5]  = static_cast<uint8_t>(uh & 0xFF);
    msg[6]  = 1;                                // number-of-screens
    // msg[7..9] = 0 (3 bytes padding)
    // msg[10..13] = 0 (screen_id)
    // msg[14..17] = 0 (screen x, y)
    msg[18] = static_cast<uint8_t>(uw >> 8);
    msg[19] = static_cast<uint8_t>(uw & 0xFF);
    msg[20] = static_cast<uint8_t>(uh >> 8);
    msg[21] = static_cast<uint8_t>(uh & 0xFF);
    // msg[22..25] = 0 (flags)
    m_socket->write(reinterpret_cast<const char*>(msg), 26);
    qDebug() << "VNC: requested SetDesktopSize" << w << "x" << h;
}
