#include "QmpInput.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>
#include <algorithm>

namespace chimera::input {

namespace {

constexpr int kQmpAbsMax = 0x7fff;

int scaledAxis(int value, int dimension) {
    if (dimension <= 1) return 0;
    const int clamped = std::clamp(value, 0, dimension - 1);
    return static_cast<int>((static_cast<double>(clamped) / (dimension - 1)) * kQmpAbsMax);
}

QString qmpButtonName(int button) {
    switch (button) {
    case 1:
        return QStringLiteral("right");
    case 2:
        return QStringLiteral("middle");
    case 0:
    default:
        return QStringLiteral("left");
    }
}

QJsonObject makeAbsEvent(const QString &axis, int value) {
    QJsonObject data;
    data["axis"] = axis;
    data["value"] = value;

    QJsonObject event;
    event["type"] = QStringLiteral("abs");
    event["data"] = data;
    return event;
}

QJsonObject makeInputCommand(const QJsonArray &events) {
    QJsonObject args;
    args["events"] = events;

    QJsonObject cmd;
    cmd["execute"] = QStringLiteral("input-send-event");
    cmd["arguments"] = args;
    return cmd;
}

} // namespace

QmpInput::QmpInput(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &QmpInput::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &QmpInput::onError);
    connect(m_socket, &QTcpSocket::disconnected, this, [this]() {
        m_capabilitiesReceived = false;
        if (m_autoReconnect && !m_disconnectRequested && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start();
        }
    });
    connect(m_reconnectTimer, &QTimer::timeout, this, &QmpInput::onReconnectTimeout);
}

QmpInput::~QmpInput() {
    disconnect();
}

bool QmpInput::connectToHost(const QString &host, int port) {
    m_host = host;
    m_port = port;
    m_disconnectRequested = false;
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(2000)) {
        qWarning() << "QMP: Failed to connect to" << host << port;
        m_socket->abort();
        if (m_autoReconnect && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start();
        }
        return false;
    }
    // Disable Nagle's algorithm: QMP commands are tiny single packets, so
    // coalescing only adds up to ~40ms of input latency. KeepAlive detects
    // a dead emulator faster so auto-reconnect can kick in.
    m_socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
    m_socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
    m_lastMoveX = -1;
    m_lastMoveY = -1;
    // Wait for capabilities greeting
    if (!m_socket->waitForReadyRead(2000)) {
        qWarning() << "QMP: No greeting received";
        m_socket->disconnectFromHost();
        if (m_autoReconnect && !m_reconnectTimer->isActive()) {
            m_reconnectTimer->start();
        }
        return false;
    }
    onReadyRead();
    return true;
}

void QmpInput::disconnect() {
    m_disconnectRequested = true;
    m_reconnectTimer->stop();
    if (m_socket->state() == QAbstractSocket::ConnectedState) {
        m_socket->disconnectFromHost();
    }
    m_capabilitiesReceived = false;
}

void QmpInput::setAutoReconnect(bool enabled, int intervalMs) {
    m_autoReconnect = enabled;
    if (enabled) {
        m_reconnectTimer->setInterval(intervalMs);
    } else {
        m_reconnectTimer->stop();
    }
}

bool QmpInput::autoReconnect() const {
    return m_autoReconnect;
}

void QmpInput::setDisplaySize(int width, int height) {
    if (width > 0) m_displayWidth = width;
    if (height > 0) m_displayHeight = height;
}

void QmpInput::onReconnectTimeout() {
    if (isConnected()) return;
    qDebug() << "QMP: Attempting auto-reconnect to" << m_host << m_port;
    if (connectToHost(m_host, m_port)) {
        qDebug() << "QMP: Auto-reconnect successful";
        m_reconnectTimer->stop();
    }
}

bool QmpInput::isConnected() const {
    return m_socket->state() == QAbstractSocket::ConnectedState && m_capabilitiesReceived;
}

bool QmpInput::sendKey(int keyCode, bool down) {
    if (!isConnected()) return false;
    return sendCommand(buildKeyCommand(keyCode, down));
}

bool QmpInput::sendMouseButton(int button, bool down, int x, int y) {
    if (!isConnected()) return false;
    return sendCommand(buildMouseButtonCommand(button, down, x, y, m_displayWidth, m_displayHeight));
}

bool QmpInput::sendMouseMove(int x, int y) {
    if (!isConnected()) return false;
    // Drop duplicate positions: high-rate mouse hardware emits many identical
    // coordinates that would otherwise flood the socket with no-op events.
    if (x == m_lastMoveX && y == m_lastMoveY) return true;
    m_lastMoveX = x;
    m_lastMoveY = y;
    return sendCommand(buildMouseMoveCommand(x, y, m_displayWidth, m_displayHeight));
}

QJsonObject QmpInput::buildKeyCommand(int keyCode, bool down) {
    QJsonObject keyData;
    keyData["type"] = QStringLiteral("number");
    keyData["data"] = keyCode;

    QJsonObject data;
    data["key"] = keyData;
    data["down"] = down;

    QJsonObject event;
    event["type"] = QStringLiteral("key");
    event["data"] = data;

    QJsonArray events;
    events.append(event);
    return makeInputCommand(events);
}

QJsonObject QmpInput::buildMouseMoveCommand(int x, int y, int width, int height) {
    QJsonArray events;
    events.append(makeAbsEvent(QStringLiteral("x"), scaledAxis(x, width)));
    events.append(makeAbsEvent(QStringLiteral("y"), scaledAxis(y, height)));
    return makeInputCommand(events);
}

QJsonObject QmpInput::buildMouseButtonCommand(int button, bool down, int x, int y,
                                              int width, int height) {
    QJsonArray events;
    events.append(makeAbsEvent(QStringLiteral("x"), scaledAxis(x, width)));
    events.append(makeAbsEvent(QStringLiteral("y"), scaledAxis(y, height)));

    QJsonObject data;
    data["button"] = qmpButtonName(button);
    data["down"] = down;

    QJsonObject event;
    event["type"] = QStringLiteral("btn");
    event["data"] = data;
    events.append(event);

    return makeInputCommand(events);
}

bool QmpInput::sendCommand(const QJsonObject &cmd) {
    QJsonDocument doc(cmd);
    QByteArray payload = doc.toJson(QJsonDocument::Compact) + "\r\n";
    m_commandTimer.start(); // Measure round-trip time
    qint64 written = m_socket->write(payload);
    m_socket->flush();
    return written == payload.size();
}

void QmpInput::onReadyRead() {
    m_readBuffer.append(m_socket->readAll());
    // QMP uses line-delimited JSON
    int pos;
    while ((pos = m_readBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_readBuffer.left(pos).trimmed();
        m_readBuffer.remove(0, pos + 1);
        if (!line.isEmpty()) {
            processResponse(line);
        }
    }
}

void QmpInput::processResponse(const QByteArray &data) {
    if (m_commandTimer.isValid()) {
        m_lastLatencyMs = static_cast<double>(m_commandTimer.elapsed());
    }

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) return;
    QJsonObject obj = doc.object();

    if (obj.contains("QMP")) {
        // Capabilities greeting received
        m_capabilitiesReceived = true;
        // Send qmp_capabilities command
        QJsonObject capCmd;
        capCmd["execute"] = QStringLiteral("qmp_capabilities");
        sendCommand(capCmd);
        qDebug() << "QMP: Connected and capabilities negotiated";
    }
}

void QmpInput::onError(QAbstractSocket::SocketError error) {
    qWarning() << "QMP socket error:" << error << m_socket->errorString();
    m_capabilitiesReceived = false;
    if (m_autoReconnect && !m_disconnectRequested && !m_reconnectTimer->isActive()) {
        m_reconnectTimer->start();
    }
}

} // namespace chimera::input
