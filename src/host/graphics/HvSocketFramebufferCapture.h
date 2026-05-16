#pragma once

#include "FramebufferCapture.h"
#include <QImage>
#include <QTimer>
#include <memory>

namespace chimera::graphics {

/**
 * @brief Framebuffer capture via AF_HYPERV (HvSocket) streaming socket.
 *
 * Receives raw RGB24 frames from a display relay daemon inside an HCS VM.
 *
 * Wire protocol (guest → host, little-endian):
 *   [uint32 width][uint32 height][width * height * 3 bytes RGB24]
 *
 * Service GUID: {00000011-facb-11e6-bd58-64006a7986d3} (VSOCK port 17)
 *
 * Phase 5a display path (replaces VNC for HCS-managed VMs).
 * Later replaced by gfxstream zero-copy path (Phase 5b).
 *
 * Requires: Windows 11, Hyper-V enabled, VM running under HyperVManager.
 */
class HvSocketFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    explicit HvSocketFramebufferCapture(const QString &vmId,
                                         QObject *parent = nullptr);
    ~HvSocketFramebufferCapture() override;

    void    setVmId(const QString &vmId);
    bool    start()       override;
    void    stop()        override;
    bool    isRunning()   const override;
    QString backendName() const override { return QStringLiteral("hvsocket-fb"); }

    void setAutoReconnect(bool enabled, int intervalMs = 3000);

private slots:
    void onSocketReadable();
    void onReconnectTimeout();

private:
    void processIncoming();

    class Impl;
    std::unique_ptr<Impl> d;
};

} // namespace chimera::graphics
