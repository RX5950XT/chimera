#include "EmulatorGrpcInput.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QtEndian>
#include <algorithm>
#include <utility>

namespace chimera::input {

namespace {

// --- Minimal protobuf / gRPC encoding -------------------------------------
// KeyboardEvent (emulator_controller.proto): field 1 codeType, field 2
// eventType, field 3 keyCode (all varint enums/int32), field 5 text (string).
// TouchEvent: field 1 repeated Touch, field 2 display. Touch: field 1 x,
// field 2 y, field 3 identifier, field 4 pressure.

void appendVarint(QByteArray *out, quint64 value) {
    while (value >= 0x80) {
        out->append(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out->append(static_cast<char>(value));
}

void appendVarintField(QByteArray *out, int field, quint64 value) {
    appendVarint(out, (static_cast<quint64>(field) << 3) | 0); // wire type 0
    appendVarint(out, value);
}

void appendStringField(QByteArray *out, int field, const QByteArray &data) {
    appendVarint(out, (static_cast<quint64>(field) << 3) | 2); // wire type 2
    appendVarint(out, static_cast<quint64>(data.size()));
    out->append(data);
}

void appendMessageField(QByteArray *out, int field, const QByteArray &data) {
    appendStringField(out, field, data);
}

// Wraps a protobuf payload in a gRPC length-prefixed frame: [0][len:4 BE][msg].
QByteArray grpcFrame(const QByteArray &payload) {
    QByteArray frame;
    frame.append('\0'); // uncompressed
    char len[4];
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), len);
    frame.append(len, 4);
    frame.append(payload);
    return frame;
}

constexpr int kCodeTypeEvdev = 1; // KeyboardEvent.KeyCodeType.Evdev
constexpr int kTouchPressure = 1;

} // namespace

EmulatorGrpcInput::EmulatorGrpcInput(QString host, int port, QObject *parent)
    : QObject(parent), m_host(std::move(host)), m_port(port) {}

void EmulatorGrpcInput::sendKey(int evdevCode, bool down) {
    if (evdevCode <= 0) return;
    QByteArray msg;
    appendVarintField(&msg, 1, kCodeTypeEvdev);          // codeType = Evdev
    appendVarintField(&msg, 2, down ? 0u : 1u);          // eventType = keydown/keyup
    appendVarintField(&msg, 3, static_cast<quint64>(evdevCode)); // keyCode
    post(QStringLiteral("sendKey"), msg);
}

void EmulatorGrpcInput::sendText(const QString &utf8text) {
    if (utf8text.isEmpty()) return;
    QByteArray msg;
    // Per the proto, when `text` is set the other fields are ignored.
    appendStringField(&msg, 5, utf8text.toUtf8());
    post(QStringLiteral("sendKey"), msg);
}

void EmulatorGrpcInput::sendTouch(int identifier, int x, int y, int pressure) {
    if (identifier < 0) return;
    QByteArray touch;
    appendVarintField(&touch, 1, static_cast<quint64>(std::max(0, x)));
    appendVarintField(&touch, 2, static_cast<quint64>(std::max(0, y)));
    appendVarintField(&touch, 3, static_cast<quint64>(identifier));
    appendVarintField(&touch, 4, static_cast<quint64>(std::max(0, pressure)));

    QByteArray event;
    appendMessageField(&event, 1, touch);
    post(QStringLiteral("sendTouch"), event);
}

void EmulatorGrpcInput::sendTouchSwipe(int identifier,
                                       int startX,
                                       int startY,
                                       int endX,
                                       int endY,
                                       int durationMs) {
    if (durationMs <= 0) {
        sendTouch(identifier, startX, startY, kTouchPressure);
        sendTouch(identifier, endX, endY, kTouchPressure);
        sendTouch(identifier, endX, endY, 0);
        return;
    }

    const int delay = std::max(8, durationMs / 3);
    sendTouch(identifier, startX, startY, kTouchPressure);
    QTimer::singleShot(delay, this, [this, identifier, startX, startY, endX, endY]() {
        sendTouch(identifier, (startX + endX) / 2, (startY + endY) / 2, kTouchPressure);
    });
    QTimer::singleShot(delay * 2, this, [this, identifier, endX, endY]() {
        sendTouch(identifier, endX, endY, kTouchPressure);
    });
    QTimer::singleShot(delay * 3, this, [this, identifier, endX, endY]() {
        sendTouch(identifier, endX, endY, 0);
    });
}

void EmulatorGrpcInput::post(const QString &rpcName, const QByteArray &payload) {
    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(QStringLiteral("/android.emulation.control.EmulatorController/") + rpcName);

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("TE", "trailers");
    request.setRawHeader("grpc-encoding", "identity");
    request.setRawHeader("grpc-accept-encoding", "identity");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);

    QNetworkReply *reply = m_net.post(request, grpcFrame(payload));
    if (reply) {
        // Fire-and-forget: keystrokes are time-sensitive, the reply is empty.
        QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    }
}

} // namespace chimera::input
