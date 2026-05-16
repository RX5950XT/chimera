#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

namespace chimera::input {

/**
 * @brief Host-side input sender over an AF_HYPERV (HvSocket) streaming socket.
 *
 * Replaces QmpInput for HCS-managed VMs. Sends Linux input_event structs to
 * a uinput relay daemon running inside the guest.
 *
 * Service GUID: {00000010-facb-11e6-bd58-64006a7986d3} (VSOCK port 16)
 *
 * Requires: Windows 11, Hyper-V enabled, VM running under HyperVManager.
 * Link: ws2_32.lib, ole32.lib (added automatically via CMakeLists.txt)
 */
class HvSocketTransport : public QObject {
    Q_OBJECT

public:
    explicit HvSocketTransport(const QString &vmId, QObject *parent = nullptr);
    ~HvSocketTransport() override;

    void setVmId(const QString &vmId);
    bool connectToVm();
    void disconnect();
    bool isConnected() const;

    void setAutoReconnect(bool enabled, int intervalMs = 3000);

    // Input injection — return false if not connected or send failed
    bool sendKey(int linuxKeyCode, bool pressed);
    bool sendMouseMove(int x, int y);           // coords in [0, 32767]
    bool sendMouseButton(int qtButton, bool pressed);

signals:
    void connected();
    void disconnected();
    void error(const QString &msg);

private slots:
    void onSocketReadable();
    void onReconnectTimeout();

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera::input
