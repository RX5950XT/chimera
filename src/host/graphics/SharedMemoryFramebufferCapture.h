#pragma once

#include "FramebufferCapture.h"

#include <QTimer>

namespace chimera::graphics {

class SharedMemoryFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    explicit SharedMemoryFramebufferCapture(QString mappingName,
                                            QString frameEventName = {},
                                            QObject *parent = nullptr);
    ~SharedMemoryFramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }
    QString backendName() const override { return QStringLiteral("SharedMemory"); }

private slots:
    void pollFrame();

private:
    bool openMapping();
    void closeMapping();
    bool readFrame();

    QString m_mappingName;
    QString m_frameEventName;
    QTimer m_pollTimer;
    bool m_running = false;
    quint64 m_lastSequence = 0;

#ifdef _WIN32
    void *m_mapping = nullptr;
    void *m_frameEvent = nullptr;
    uchar *m_view = nullptr;
    qsizetype m_viewSize = 0;
#endif
};

} // namespace chimera::graphics
