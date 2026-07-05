#pragma once

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <atomic>

class QTimer;

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

    // Channel liveness. gRPC is the ONLY pointer-input channel once wired
    // (InputBridge's console/QMP/ADB branches are unreachable while non-null),
    // and post() is fire-and-forget — so a dead/hijacked endpoint used to mean
    // every click vanished silently with no fallback ("picture but nothing
    // responds", Sessions 106-108). Transport results feed a 3-strike breaker:
    // unhealthy flips InputBridge to its console/QMP/ADB fallbacks, and a
    // periodic getStatus probe (side-effect-free RPC) restores gRPC once the
    // endpoint answers again.
    bool isHealthy() const { return m_healthy.load(std::memory_order_relaxed); }
    // Internal accounting, public so the breaker logic is unit-testable
    // without a live gRPC server. ok = transport-level success (connected,
    // HTTP 200); per-RPC grpc-status rejections do NOT count as channel death.
    void recordTransportResult(bool ok);

private:
    void post(const QString &rpcName, const QByteArray &payload);
    void probeHealth();
    QString m_host;
    int m_port;
    QNetworkAccessManager m_net;
    std::atomic_bool m_healthy{true};
    int m_consecutiveFailures = 0;
    QTimer *m_probeTimer = nullptr;
};

} // namespace chimera::input
