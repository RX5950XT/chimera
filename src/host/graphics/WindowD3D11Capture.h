#pragma once

#include "FramebufferCapture.h"

#include <QTimer>
#include <memory>

namespace chimera::graphics {

class WindowD3D11Capture final : public FramebufferCapture {
    Q_OBJECT

public:
    WindowD3D11Capture(quint32 rootProcessId,
                       QString instanceName,
                       int consolePort,
                       QObject *parent = nullptr);
    ~WindowD3D11Capture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override;
    QString backendName() const override { return QStringLiteral("Window-D3D11"); }

private:
    bool tryStartCapture();
    void teardownCapture();

    struct Impl;
    std::unique_ptr<Impl> d;
    quint32 m_rootProcessId = 0;
    QString m_instanceName;
    int m_consolePort = 0;
    bool m_running = false;
    QTimer m_retryTimer;
};

} // namespace chimera::graphics
