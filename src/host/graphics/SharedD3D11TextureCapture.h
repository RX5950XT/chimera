#pragma once

#include "FramebufferCapture.h"
#include <atomic>
#include <thread>

namespace chimera::graphics {

class SharedD3D11TextureCapture : public FramebufferCapture {
    Q_OBJECT

public:
    explicit SharedD3D11TextureCapture(QString mappingName,
                                       QString frameEventName = {},
                                       QObject *parent = nullptr);
    ~SharedD3D11TextureCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running.load(std::memory_order_acquire); }
    QString backendName() const override { return QStringLiteral("SharedD3D11Texture"); }

private:
    bool openMapping();
    void closeMapping();
    bool readFrame();
    void workerLoop();

    QString m_mappingName;
    QString m_frameEventName;
    std::atomic_bool m_running{false};
    std::atomic_bool m_stopRequested{false};
    std::thread m_worker;
    void *m_mapping = nullptr;
    void *m_frameEvent = nullptr;
    uchar *m_view = nullptr;
    qsizetype m_viewSize = 0;
    quint64 m_lastSequence = 0;
};

} // namespace chimera::graphics
