#include "VncFramebufferCapture.h"
#include <QDebug>
#include <cstring>

using namespace chimera::graphics;

namespace {

// RFB protocol constants
constexpr char RFB_VERSION_3_8[] = "RFB 003.008\n";
constexpr uint8_t SECURITY_TYPE_NONE = 1;
constexpr uint32_t ENCODING_RAW = 0;

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
{
    connect(m_socket, &QTcpSocket::connected, this, &VncFramebufferCapture::onSocketConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, &VncFramebufferCapture::onSocketDisconnected);
    connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this, &VncFramebufferCapture::onSocketError);
    connect(m_socket, &QTcpSocket::readyRead, this, &VncFramebufferCapture::onSocketReadyRead);
    connect(m_updateTimer, &QTimer::timeout, this, &VncFramebufferCapture::requestFrameUpdate);
}

VncFramebufferCapture::~VncFramebufferCapture() {
    stop();
}

bool VncFramebufferCapture::start() {
    if (m_running) return true;
    m_running = true;
    m_state = State::Disconnected;
    m_socket->connectToHost(m_host, m_port);
    return true;
}

void VncFramebufferCapture::stop() {
    m_running = false;
    m_updateTimer->stop();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
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
    m_state = State::Disconnected;
}

void VncFramebufferCapture::onSocketError(QAbstractSocket::SocketError error) {
    qWarning() << "VNC socket error:" << error << m_socket->errorString();
    emit captureError(QStringLiteral("VNC connection error: ") + m_socket->errorString());
    m_running = false;
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
            case State::ServerInit:
                m_state = State::Ready;
                sendPixelFormat();
                sendFramebufferUpdateRequest(false); // Full update first
                m_updateTimer->start(m_intervalMs);
                break;
            case State::Ready:
                // Wait for FramebufferUpdate
                if (m_readBuffer.size() < 4) return;
                processFramebufferUpdate(m_readBuffer);
                break;
            case State::FramebufferUpdate:
                // Handled inside processFramebufferUpdate
                return;
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
}

void VncFramebufferCapture::sendPixelFormat() {
    // Request 32-bit RGBA little-endian
    uint8_t msg[20] = {};
    msg[0] = 0; // SetPixelFormat
    msg[3] = 32; // bits per pixel
    msg[4] = 24; // depth
    msg[5] = 0;  // little endian
    msg[6] = 1;  // true colour
    msg[7] = 0; msg[8] = 255; // red max
    msg[9] = 0; msg[10] = 255; // green max
    msg[11] = 0; msg[12] = 255; // blue max
    msg[13] = 16; // red shift
    msg[14] = 8;  // green shift
    msg[15] = 0;  // blue shift
    m_socket->write(reinterpret_cast<const char*>(msg), 20);
}

void VncFramebufferCapture::sendFramebufferUpdateRequest(bool incremental) {
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
}

void VncFramebufferCapture::processFramebufferUpdate(QByteArray &buffer) {
    if (buffer.size() < 4) return;
    const uint8_t *p = reinterpret_cast<const uint8_t*>(buffer.constData());
    // msg type (1 byte), padding (1 byte), numRectangles (2 bytes)
    uint16_t numRects = readU16BE(p + 2);
    buffer.remove(0, 4);
    m_rectanglesRemaining = numRects;
    m_state = State::FramebufferUpdate;
    processRectangles(buffer);
}

void VncFramebufferCapture::processRectangles(QByteArray &buffer) {
    while (m_rectanglesRemaining > 0) {
        if (buffer.size() < 12) return;
        const uint8_t *p = reinterpret_cast<const uint8_t*>(buffer.constData());
        m_rectX = readU16BE(p);
        m_rectY = readU16BE(p + 2);
        m_rectW = readU16BE(p + 4);
        m_rectH = readU16BE(p + 6);
        m_rectEncoding = readU32BE(p + 8);
        buffer.remove(0, 12);

        if (m_rectEncoding == ENCODING_RAW) {
            int bytesPerPixel = m_bitsPerPixel / 8;
            m_rectBytesRemaining = m_rectW * m_rectH * bytesPerPixel;
            if (buffer.size() < m_rectBytesRemaining) return;
            QByteArray pixels = buffer.left(m_rectBytesRemaining);
            buffer.remove(0, m_rectBytesRemaining);

            // Copy pixels to framebuffer
            for (int y = 0; y < m_rectH; ++y) {
                for (int x = 0; x < m_rectW; ++x) {
                    int srcOffset = (y * m_rectW + x) * bytesPerPixel;
                    const uint8_t *pixel = reinterpret_cast<const uint8_t*>(pixels.constData()) + srcOffset;
                    uint32_t rgba = (pixel[3] << 24) | (pixel[0] << 16) | (pixel[1] << 8) | pixel[2];
                    m_framebuffer.setPixel(m_rectX + x, m_rectY + y, rgba);
                }
            }
        } else {
            emit captureError(QStringLiteral("Unsupported VNC encoding: ") + QString::number(m_rectEncoding));
            stop(); return;
        }
        m_rectanglesRemaining--;
    }

    if (m_rectanglesRemaining == 0) {
        emit frameReady(m_framebuffer.copy());
        m_state = State::Ready;
    }
}

void VncFramebufferCapture::requestFrameUpdate() {
    if (m_state == State::Ready && m_socket->state() == QAbstractSocket::ConnectedState) {
        sendFramebufferUpdateRequest(true); // incremental
    }
}
