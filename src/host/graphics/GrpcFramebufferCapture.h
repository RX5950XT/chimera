#pragma once

#include "FramebufferCapture.h"
#include <QByteArray>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QString>

class QNetworkReply;

namespace chimera::graphics {

class GrpcFramebufferCapture : public FramebufferCapture {
    Q_OBJECT

public:
    GrpcFramebufferCapture(QString host, int port, int requestedWidth, int requestedHeight,
                           QObject *parent = nullptr);
    ~GrpcFramebufferCapture() override;

    bool start() override;
    void stop() override;
    bool isRunning() const override { return m_running; }
    QString backendName() const override { return QStringLiteral("gRPC"); }

    static QByteArray buildImageFormatRequest(int width, int height, int displayId = 0);
    static void appendVarint(QByteArray *out, quint64 value);
    static bool readVarint(const QByteArray &data, int *offset, quint64 *value);

private slots:
    void onReadyRead();
    void onFinished();
    void onError();

private:
    struct DecodedImage {
        int width = 0;
        int height = 0;
        int format = 1;
        QByteArray pixels;
    };

    static bool decodeImageMessage(const QByteArray &payload, DecodedImage *out);
    static void appendGrpcFrame(QByteArray *out, const QByteArray &payload);
    static bool skipField(const QByteArray &data, int *offset, int wireType);
    static bool parseImageFormat(const QByteArray &payload, int *width, int *height, int *format);

    void parseBufferedFrames();
    QImage imageFromTopDown(const DecodedImage &decoded) const;

    QString m_host;
    int m_port = 0;
    int m_requestedWidth = 0;
    int m_requestedHeight = 0;
    bool m_running = false;
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QByteArray m_buffer;
};

} // namespace chimera::graphics
