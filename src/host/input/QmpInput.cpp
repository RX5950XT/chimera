#include "QmpInput.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QDebug>

namespace chimera::input {

QmpInput::QmpInput(QObject *parent)
    : QObject(parent)
    , m_socket(new QTcpSocket(this))
    , m_reconnectTimer(new QTimer(this))
{
    connect(m_socket, &QTcpSocket::readyRead, this, &QmpInput::onReadyRead);
    connect(m_socket, &QTcpSocket::errorOccurred, this, &QmpInput::onError);
    connect(m_reconnectTimer, &QTimer::timeout, this, &QmpInput::onReconnectTimeout);
}

QmpInput::~QmpInput() {
    disconnect();
}

bool QmpInput::connectToHost(const QString &host, int port) {
    m_host = host;
    m_port = port;
    m_socket->connectToHost(host, port);
    if (!m_socket->waitForConnected(2000)) {
        qWarning() << "QMP: Failed to connect to" << host << port;
        return false;
    }
    // Wait for capabilities greeting
    if (!m_socket->waitForReadyRead(2000)) {
        qWarning() << "QMP: No greeting received";
        return false;
    }
    return true;
}

void QmpInput::disconnect() {
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

    QJsonObject args;
    args["events"] = events;

    QJsonObject cmd;
    cmd["execute"] = QStringLiteral("input-send-event");
    cmd["arguments"] = args;

    return sendCommand(cmd);
}

bool QmpInput::sendMouseButton(int button, bool down, int x, int y) {
    if (!isConnected()) return false;
    // button: 0=left, 1=middle, 2=right
    QJsonObject data;
    data["button"] = QStringLiteral("left"); // Simplified
    data["down"] = down;

    QJsonObject event;
    event["type"] = QStringLiteral("btn");
    event["data"] = data;

    QJsonArray events;
    events.append(event);

    QJsonObject args;
    args["events"] = events;

    QJsonObject cmd;
    cmd["execute"] = QStringLiteral("input-send-event");
    cmd["arguments"] = args;

    return sendCommand(cmd);
}

bool QmpInput::sendMouseMove(int x, int y) {
    if (!isConnected()) return false;
    QJsonObject data;
    data["axis"] = QStringLiteral("abs");
    data["x"] = x;
    data["y"] = y;

    QJsonObject event;
    event["type"] = QStringLiteral("rel");
    event["data"] = data;

    QJsonArray events;
    events.append(event);

    QJsonObject args;
    args["events"] = events;

    QJsonObject cmd;
    cmd["execute"] = QStringLiteral("input-send-event");
    cmd["arguments"] = args;

    return sendCommand(cmd);
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
}

} // namespace chimera::input
