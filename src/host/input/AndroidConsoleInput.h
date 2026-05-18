#pragma once

#include <QObject>
#include <QTcpSocket>
#include <QElapsedTimer>
#include <QTimer>
#include <QString>
#include <string>

namespace chimera::input {

/**
 * @brief Android Emulator Console input — replaces broken QmpInput-on-5554.
 *
 * Port 5554 speaks the Android Console telnet protocol, not JSON QMP.
 * This class implements the correct handshake:
 *   1. Wait for greeting "OK"
 *   2. Send "auth <token>" (from %USERPROFILE%\.emulator_console_auth_token)
 *   3. Probe supported event commands (non-destructive)
 *   4. Enter Ready state — events are injected via "event mouse / event keydown"
 *
 * Injection latency target: <10ms (vs ADB ~100ms).
 */
class AndroidConsoleInput : public QObject {
    Q_OBJECT
public:
    enum class State {
        Disconnected,
        ConnectedUnauthed,
        AuthPending,
        Probing,
        Ready,
        FailedAuth
    };
    Q_ENUM(State)

    struct ProbeResult {
        bool mouseCmdOk   = false;
        bool textKeyCmdOk = false; // event keydown / keyup supported
    };

    explicit AndroidConsoleInput(QObject *parent = nullptr);
    ~AndroidConsoleInput() override;

    void connectToHost(const QString &host, int port);
    void disconnectFromHost();

    bool isConnected() const     { return m_state == State::Ready; }
    bool isMouseReady() const    { return m_state == State::Ready && m_probe.mouseCmdOk; }
    bool isKeyboardReady() const { return m_state == State::Ready && m_probe.textKeyCmdOk; }

    void setAutoReconnect(bool enabled);
    State       state()       const { return m_state; }
    ProbeResult probeResult() const { return m_probe; }
    double lastLatencyMs()    const { return m_lastLatencyMs; }

    // buttons: bitmask — bit0=left(1), bit1=right(2), bit2=middle(4)
    bool sendMouseEvent(int x, int y, int buttons);
    bool sendMouseMove(int x, int y);
    bool sendKeyEvent(int androidKeyCode, bool down);
    bool sendGeoFix(double lon, double lat, double alt);
    bool sendClipboardSet(const std::string &utf8text);

signals:
    void stateChanged(State state);

private slots:
    void onSocketConnected();
    void onReadyRead();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSocketDisconnected();
    void onReconnectTimer();

private:
    void    setState(State s);
    void    processLine(const QString &line);
    void    sendLine(const QString &line);
    void    startProbe();
    void    finishProbe();
    QString readAuthToken() const;
    int     reconnectDelayMs() const;

    QTcpSocket *m_socket        = nullptr;
    QTimer     *m_reconnectTimer= nullptr;
    QByteArray  m_readBuffer;

    State       m_state      = State::Disconnected;
    ProbeResult m_probe;
    int         m_probeStep  = 0;

    QString m_host;
    int     m_port                = 5554;
    bool    m_autoReconnect       = false;
    bool    m_disconnectRequested = false;
    int     m_reconnectAttempts   = 0;

    QElapsedTimer m_latencyTimer;
    double        m_lastLatencyMs = 0.0;

    int m_lastMouseX       = -1;
    int m_lastMouseY       = -1;
    int m_lastMouseButtons = -1;
};

} // namespace chimera::input
