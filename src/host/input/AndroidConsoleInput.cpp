#include "AndroidConsoleInput.h"
#include <QDebug>
#include <QFile>
#include <cstdlib>
#include <algorithm>

namespace chimera::input {

static constexpr int kMinReconnectMs = 1000;
static constexpr int kMaxReconnectMs = 30000;

static const char *stateName(AndroidConsoleInput::State s) {
    switch (s) {
    case AndroidConsoleInput::State::Disconnected:      return "Disconnected";
    case AndroidConsoleInput::State::ConnectedUnauthed: return "ConnectedUnauthed";
    case AndroidConsoleInput::State::AuthPending:       return "AuthPending";
    case AndroidConsoleInput::State::Probing:           return "Probing";
    case AndroidConsoleInput::State::Ready:             return "Ready";
    case AndroidConsoleInput::State::FailedAuth:        return "FailedAuth";
    }
    return "Unknown";
}

AndroidConsoleInput::AndroidConsoleInput(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_socket, &QTcpSocket::connected,
            this, &AndroidConsoleInput::onSocketConnected);
    connect(m_socket, &QTcpSocket::readyRead,
            this, &AndroidConsoleInput::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred,
            this, &AndroidConsoleInput::onSocketError);
    connect(m_socket, &QTcpSocket::disconnected,
            this, &AndroidConsoleInput::onSocketDisconnected);
    connect(m_reconnectTimer, &QTimer::timeout,
            this, &AndroidConsoleInput::onReconnectTimer);
}

AndroidConsoleInput::~AndroidConsoleInput() {
    disconnectFromHost();
}

void AndroidConsoleInput::connectToHost(const QString &host, int port) {
    m_host = host;
    m_port = port;
    m_disconnectRequested = false;
    m_probe   = {};
    m_probeStep = 0;
    m_readBuffer.clear();
    m_socket->connectToHost(host, static_cast<quint16>(port));
}

void AndroidConsoleInput::disconnectFromHost() {
    m_disconnectRequested = true;
    m_reconnectTimer->stop();
    if (m_socket->state() != QAbstractSocket::UnconnectedState)
        m_socket->disconnectFromHost();
    setState(State::Disconnected);
}

void AndroidConsoleInput::setAutoReconnect(bool enabled) {
    m_autoReconnect = enabled;
    if (!enabled) {
        m_reconnectTimer->stop();
        m_reconnectAttempts = 0;
    }
}

void AndroidConsoleInput::setState(State s) {
    if (m_state == s) return;
    qDebug() << "[AndroidConsoleInput] state:" << stateName(m_state) << "->" << stateName(s);
    m_state = s;
    emit stateChanged(s);
}

int AndroidConsoleInput::reconnectDelayMs() const {
    // Exponential backoff capped at 30s
    const int ms = kMinReconnectMs << std::min(m_reconnectAttempts, 5);
    return std::min(ms, kMaxReconnectMs);
}

QString AndroidConsoleInput::readAuthToken() const {
    const char *up = std::getenv("USERPROFILE");
    if (!up) return {};
    QFile f(QString::fromLocal8Bit(up) + "/.emulator_console_auth_token");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    return QString::fromUtf8(f.readAll()).trimmed();
}

void AndroidConsoleInput::onSocketConnected() {
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    setState(State::ConnectedUnauthed);
    // Greeting "OK\r\n" arrives in onReadyRead
}

void AndroidConsoleInput::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    int pos;
    while ((pos = m_readBuffer.indexOf('\n')) >= 0) {
        QByteArray raw = m_readBuffer.left(pos);
        m_readBuffer.remove(0, pos + 1);
        if (!raw.isEmpty() && raw.back() == '\r') raw.chop(1);
        const QString line = QString::fromUtf8(raw).trimmed();
        if (!line.isEmpty()) processLine(line);
    }
}

void AndroidConsoleInput::processLine(const QString &line) {
    switch (m_state) {
    case State::ConnectedUnauthed:
        // The greeting is multi-line; the final line is "OK"
        if (line == QLatin1String("OK")) {
            const QString token = readAuthToken();
            if (token.isEmpty()) {
                // Token not yet written (emulator still starting). Retry.
                qWarning() << "[AndroidConsoleInput] auth token not found; disconnecting to retry";
                m_socket->disconnectFromHost();
                return;
            }
            sendLine(QLatin1String("auth ") + token);
            setState(State::AuthPending);
        }
        break;

    case State::AuthPending:
        if (line == QLatin1String("OK")) {
            m_probe     = {};
            m_probeStep = 0;
            setState(State::Probing);
            startProbe();
        } else {
            qWarning() << "[AndroidConsoleInput] auth rejected:" << line;
            setState(State::FailedAuth);
        }
        break;

    case State::Probing: {
        const bool ok = (line == QLatin1String("OK"));
        if (m_probeStep == 0) {
            m_probe.mouseCmdOk = ok;
            if (!ok)
                qWarning() << "[AndroidConsoleInput] probe: event-mouse FAIL:" << line;
            ++m_probeStep;
            // Probe keyboard: KEYCODE_UNKNOWN (0) down event — safe, produces no visible action
            sendLine(QLatin1String("event keydown 0"));
            m_latencyTimer.start();
        } else if (m_probeStep == 1) {
            m_probe.textKeyCmdOk = ok;
            if (!ok)
                qWarning() << "[AndroidConsoleInput] probe: event-keydown FAIL:" << line;
            finishProbe();
        }
        break;
    }

    case State::Ready:
        if (m_latencyTimer.isValid()) {
            m_lastLatencyMs = static_cast<double>(m_latencyTimer.elapsed());
            m_latencyTimer.invalidate();
        }
        break;

    default:
        break;
    }
}

void AndroidConsoleInput::startProbe() {
    m_probeStep = 0;
    // Move to (0,0) with no buttons pressed — harmless, just tests the command is accepted
    sendLine(QLatin1String("event mouse 0 0 0 0"));
    m_latencyTimer.start();
}

void AndroidConsoleInput::finishProbe() {
    qDebug() << "[AndroidConsoleInput] probe: event-mouse="
             << (m_probe.mouseCmdOk   ? "OK" : "FAIL")
             << "event-keydown="
             << (m_probe.textKeyCmdOk ? "OK" : "FAIL");
    if (!m_probe.textKeyCmdOk) {
        qWarning() << "[AndroidConsoleInput] keyboard probe failed — keyboard will use ADB fallback";
    }
    m_reconnectAttempts = 0;
    setState(State::Ready);
}

void AndroidConsoleInput::sendLine(const QString &line) {
    if (m_socket->state() != QAbstractSocket::ConnectedState) return;
    m_socket->write(line.toUtf8() + "\r\n");
    m_socket->flush();
}

void AndroidConsoleInput::onSocketError(QAbstractSocket::SocketError error) {
    qWarning() << "[AndroidConsoleInput] socket error:" << error << m_socket->errorString();
    setState(State::Disconnected);
    if (m_autoReconnect && !m_disconnectRequested && !m_reconnectTimer->isActive()) {
        ++m_reconnectAttempts;
        m_reconnectTimer->start(reconnectDelayMs());
    }
}

void AndroidConsoleInput::onSocketDisconnected() {
    if (m_state != State::Disconnected)
        setState(State::Disconnected);
    if (m_autoReconnect && !m_disconnectRequested && !m_reconnectTimer->isActive()) {
        ++m_reconnectAttempts;
        m_reconnectTimer->start(reconnectDelayMs());
    }
}

void AndroidConsoleInput::onReconnectTimer() {
    if (m_disconnectRequested || m_state == State::Ready) return;
    qDebug() << "[AndroidConsoleInput] reconnect attempt" << m_reconnectAttempts
             << "to" << m_host << "port" << m_port;
    m_probe     = {};
    m_probeStep = 0;
    m_readBuffer.clear();
    m_socket->connectToHost(m_host, static_cast<quint16>(m_port));
}

bool AndroidConsoleInput::sendMouseEvent(int x, int y, int buttons) {
    if (!isMouseReady()) return false;
    if (x == m_lastMouseX && y == m_lastMouseY && buttons == m_lastMouseButtons) return true;
    m_lastMouseX       = x;
    m_lastMouseY       = y;
    m_lastMouseButtons = buttons;
    sendLine(QStringLiteral("event mouse %1 %2 0 %3").arg(x).arg(y).arg(buttons));
    m_latencyTimer.start();
    return true;
}

bool AndroidConsoleInput::sendMouseMove(int x, int y) {
    return sendMouseEvent(x, y, 0);
}

bool AndroidConsoleInput::sendKeyEvent(int androidKeyCode, bool down) {
    if (!isKeyboardReady()) return false;
    const QString cmd = down
        ? QStringLiteral("event keydown %1").arg(androidKeyCode)
        : QStringLiteral("event keyup %1").arg(androidKeyCode);
    sendLine(cmd);
    m_latencyTimer.start();
    return true;
}

bool AndroidConsoleInput::sendGeoFix(double lon, double lat, double alt) {
    if (!isConnected()) return false;
    sendLine(QStringLiteral("geo fix %1 %2 %3")
                 .arg(lon, 0, 'f', 6)
                 .arg(lat, 0, 'f', 6)
                 .arg(alt, 0, 'f', 1));
    return true;
}

} // namespace chimera::input
