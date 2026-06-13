#pragma once

#include "FramebufferCapture.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTemporaryFile>
#include <QTimer>
#include <memory>

class QNetworkReply;

namespace chimera::graphics {

class SharedD3D11TexturePublisher;

class GrpcMmapFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    GrpcMmapFramebufferCapture(QString host, int port, int requestedWidth, int requestedHeight,
                               QObject *parent = nullptr);
    ~GrpcMmapFramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }
    QString backendName() const override { return QStringLiteral("gRPC-MMAP"); }
    void notifyInputActivity();

    static QByteArray buildMmapImageFormatRequest(int width, int height, const QString &handle);

private:
    struct ImageMetadata {
        int width = 0;
        int height = 0;
        int format = 2;
        quint32 sequence = 0;
    };

    struct MmapSlot {
        QTemporaryFile file;
        uchar *mappedPixels = nullptr;
        int mappedSize = 0;
        QByteArray requestBody;
    };

    static bool parseImageMetadata(const QByteArray &payload, ImageMetadata *metadata);
    static bool skipField(const QByteArray &data, int *offset, int wireType);

    bool prepareMmapSlots();
    bool prepareMmapSlot(MmapSlot *slot);
    void closeMmapSlots();
    void startStream();
    void restartStream();
    void onReadyRead();
    void onStreamFinished();
    void checkStall();
    void parseStreamFrames();
    bool emitMappedFrame(MmapSlot *slot, const ImageMetadata &metadata);
    bool ensureTexturePublisher(const QSize &size);
    bool publishRgbaTextureFrame(const uchar *data, int bytesPerLine, const QSize &size);

    QString m_host;
    int m_port = 0;
    int m_requestedWidth = 0;
    int m_requestedHeight = 0;
    bool m_running = false;
    bool m_hasLastSequence = false;
    quint32 m_lastSequence = 0;
    qint64 m_lastFrameMs = 0;
    QElapsedTimer m_paceTimer;
    QTimer m_watchdog;
    QNetworkAccessManager m_network;
    MmapSlot m_slot;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_streamBuffer;
    std::unique_ptr<SharedD3D11TexturePublisher> m_texturePublisher;
    QSize m_texturePublisherSize;
    bool m_texturePublishFailed = false;
};

} // namespace chimera::graphics
