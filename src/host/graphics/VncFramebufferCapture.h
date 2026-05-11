#pragma once

#include "FramebufferCapture.h"
#include <QTcpSocket>
#include <QTimer>
#include <QImage>

namespace chimera::graphics {

/**
 * @brief Frame capture via VNC RFB protocol.
 *
 * Connects to a QEMU VNC server and receives framebuffer updates.
 * Use with custom QEMU builds: `-display vnc=:0`
 *
 * Supports raw encoding only (sufficient for local use).
 */
class VncFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    explicit VncFramebufferCapture(const QString &host, int port = 5900,
                                    QObject *parent = nullptr);
    ~VncFramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QString backendName() const override { return QStringLiteral("vnc-rfb"); }

private slots:
    void onSocketConnected();
    void onSocketDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketReadyRead();
    void requestFrameUpdate();

private:
    enum class State {
        Disconnected,
        Handshake,
        Security,
        SecurityResult,
        ClientInit,
        ServerInit,
        Ready,
        FramebufferUpdate
    };

    void processHandshake(const QByteArray &data);
    void processSecurity(const QByteArray &data);
    void processSecurityResult(const QByteArray &data);
    void processServerInit(const QByteArray &data);
    void processServerInit(QByteArray &buffer);
    void processFramebufferUpdate(QByteArray &buffer);
    void sendClientInit();
    void sendPixelFormat();
    void sendFramebufferUpdateRequest(bool incremental);
    void processRectangles(QByteArray &buffer);

    QString m_host;
    int m_port = 5900;
    QTcpSocket *m_socket = nullptr;
    QTimer *m_updateTimer = nullptr;
    State m_state = State::Disconnected;
    bool m_running = false;

    // VNC server info
    uint16_t m_fbWidth = 0;
    uint16_t m_fbHeight = 0;
    uint8_t m_bitsPerPixel = 32;
    uint8_t m_depth = 24;
    bool m_bigEndian = false;
    bool m_trueColour = true;
    uint16_t m_redMax = 255, m_greenMax = 255, m_blueMax = 255;
    uint8_t m_redShift = 16, m_greenShift = 8, m_blueShift = 0;

    // Framebuffer update parsing state
    QByteArray m_readBuffer;
    uint16_t m_rectanglesRemaining = 0;
    uint16_t m_rectX = 0, m_rectY = 0, m_rectW = 0, m_rectH = 0;
    uint32_t m_rectEncoding = 0;
    int m_rectBytesRemaining = 0;
    QByteArray m_rectPixelData;
    QImage m_framebuffer;
};

} // namespace chimera::graphics
