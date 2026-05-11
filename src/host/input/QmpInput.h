#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QTimer>

namespace chimera::input {

/**
 * @brief Injects input events via QEMU Machine Protocol (QMP).
 *
 * Connects to QEMU's JSON-based monitor to send input events directly,
 * bypassing ADB shell latency (~100ms → <5ms).
 *
 * QMP command example:
 *   {"execute": "input-send-event",
 *    "arguments": {
 *      "events": [
 *        {"type": "key", "data": {"key": {"type": "number", "data": 30}, "down": true}}
 *      ]
 *    }}
 */
class QmpInput : public QObject {
    Q_OBJECT
public:
    explicit QmpInput(QObject *parent = nullptr);
    ~QmpInput();

    bool connectToHost(const QString &host, int port);
    void disconnect();
    bool isConnected() const;

    // Auto-reconnect configuration
    void setAutoReconnect(bool enabled, int intervalMs = 5000);
    bool autoReconnect() const;

    // Latency measurement (last command round-trip in ms)
    double lastLatencyMs() const { return m_lastLatencyMs; }

    // Send events
    bool sendKey(int keyCode, bool down);
    bool sendMouseButton(int button, bool down, int x, int y);
    bool sendMouseMove(int x, int y);

private slots:
    void onReadyRead();
    void onError(QAbstractSocket::SocketError error);
    void onReconnectTimeout();

private:
    bool sendCommand(const QJsonObject &cmd);
    void processResponse(const QByteArray &data);

    QTcpSocket *m_socket = nullptr;
    bool m_capabilitiesReceived = false;
    QByteArray m_readBuffer;
    QElapsedTimer m_commandTimer;
    double m_lastLatencyMs = 0.0;

    // Auto-reconnect
    QTimer *m_reconnectTimer = nullptr;
    QString m_host;
    int m_port = 5554;
    bool m_autoReconnect = false;
};

} // namespace chimera::input
