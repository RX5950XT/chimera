#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>

namespace chimera::input {

// Low-latency keyboard input via the emulator's gRPC EmulatorController.
//
// The Android console (port 5554) has no working keyboard channel on this
// emulator build — `event keydown` is unsupported and `event send` only
// reaches the touch device. ADB `input keyevent` works but costs ~100ms per
// key (shell spawn). The emulator's gRPC input RPCs route through the
// emulator's own input layer, so this is the primary low-latency path for
// keyboard, IME text, and touchscreen taps.
class EmulatorGrpcInput : public QObject {
    Q_OBJECT

public:
    EmulatorGrpcInput(QString host, int port, QObject *parent = nullptr);

    // evdevCode: a Linux input-event-codes KEY_* value. Sent with codeType
    // Evdev so the emulator passes it straight through.
    void sendKey(int evdevCode, bool down);
    // Types a UTF-8 string via the KeyboardEvent `text` field (IME path).
    void sendText(const QString &utf8text);
    // Sends a touchscreen contact. Use pressure > 0 for down/move and 0 for up.
    void sendTouch(int identifier, int x, int y, int pressure);
    void sendTouchSwipe(int identifier, int startX, int startY, int endX, int endY, int durationMs);

private:
    void post(const QString &rpcName, const QByteArray &payload);
    QString m_host;
    int m_port;
    QNetworkAccessManager m_net;
};

} // namespace chimera::input
