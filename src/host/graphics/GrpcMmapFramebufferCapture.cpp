#include "GrpcMmapFramebufferCapture.h"
#include "GrpcFramebufferCapture.h"
#include "SharedD3D11TexturePublisher.h"

#include <QDebug>
#include <QDir>
#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUuid>
#include <QUrl>
#include <QtEndian>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace chimera::graphics {

namespace {

constexpr quint8 kGrpcUncompressed = 0;
constexpr int kRgba8888 = 1;
constexpr int kRgb888 = 2;
constexpr int kMaxFrameBytes = 64 * 1024 * 1024;
constexpr int kStallTimeoutMs = 2000;
constexpr quint32 kDxgiFormatRgba8888 = 28; // DXGI_FORMAT_R8G8B8A8_UNORM

void appendVarintField(QByteArray *out, int fieldNumber, quint64 value) {
    GrpcFramebufferCapture::appendVarint(out, static_cast<quint64>(fieldNumber) << 3);
    GrpcFramebufferCapture::appendVarint(out, value);
}

void appendBytesField(QByteArray *out, int fieldNumber, const QByteArray &value) {
    GrpcFramebufferCapture::appendVarint(out, (static_cast<quint64>(fieldNumber) << 3) | 2);
    GrpcFramebufferCapture::appendVarint(out, static_cast<quint64>(value.size()));
    out->append(value);
}

void appendGrpcFrame(QByteArray *out, const QByteArray &payload) {
    out->append(static_cast<char>(kGrpcUncompressed));
    char length[4];
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), length);
    out->append(length, 4);
    out->append(payload);
}

QString localObjectName(const QString &suffix) {
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
#ifdef _WIN32
    return QStringLiteral("Local\\ChimeraGrpcMmap%1_%2").arg(suffix, id);
#else
    return QStringLiteral("ChimeraGrpcMmap%1_%2").arg(suffix, id);
#endif
}

} // namespace

GrpcMmapFramebufferCapture::GrpcMmapFramebufferCapture(QString host,
                                                       int port,
                                                       int requestedWidth,
                                                       int requestedHeight,
                                                       QObject *parent)
    : FramebufferCapture(parent),
      m_host(std::move(host)),
      m_port(port) {
    const QSize size = GrpcFramebufferCapture::normalizedCaptureSize(requestedWidth, requestedHeight);
    m_requestedWidth = size.width();
    m_requestedHeight = size.height();
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this, &GrpcMmapFramebufferCapture::checkStall);
}

GrpcMmapFramebufferCapture::~GrpcMmapFramebufferCapture() {
    stop();
}

bool GrpcMmapFramebufferCapture::start() {
    if (m_running) return true;
    if (!prepareMmapSlots()) return false;

    m_running = true;
    m_paceTimer.start();
    m_hasLastSequence = false;
    m_lastSequence = 0;
    m_lastFrameMs = 0;
    m_streamBuffer.clear();
    m_texturePublishFailed = false;

    startStream();
    m_watchdog.start();
    return true;
}

void GrpcMmapFramebufferCapture::stop() {
    m_running = false;
    m_watchdog.stop();
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_streamBuffer.clear();
    if (m_texturePublisher) {
        m_texturePublisher->stop();
        m_texturePublisher.reset();
    }
    m_texturePublisherSize = QSize();
    m_texturePublishFailed = false;
    closeMmapSlots();
}

void GrpcMmapFramebufferCapture::notifyInputActivity() {
    if (m_running && !m_reply)
        startStream();
}

bool GrpcMmapFramebufferCapture::prepareMmapSlots() {
    closeMmapSlots();
    return prepareMmapSlot(&m_slot);
}

bool GrpcMmapFramebufferCapture::prepareMmapSlot(MmapSlot *slot) {
    if (!slot) return false;
    slot->file.setFileTemplate(QDir::tempPath() +
                               QStringLiteral("/chimera-grpc-mmap-XXXXXX.rgba"));
    slot->file.setAutoRemove(true);
    if (!slot->file.open()) {
        emit captureError(QStringLiteral("failed to create gRPC MMAP file"));
        return false;
    }

    const qint64 bytes = static_cast<qint64>(m_requestedWidth) *
                         static_cast<qint64>(m_requestedHeight) * 4;
    if (bytes <= 0 || bytes > kMaxFrameBytes) {
        emit captureError(QStringLiteral("invalid gRPC MMAP size"));
        return false;
    }
    if (!slot->file.resize(bytes)) {
        emit captureError(QStringLiteral("failed to size gRPC MMAP file"));
        return false;
    }
    slot->mappedSize = static_cast<int>(bytes);
    slot->mappedPixels = slot->file.map(0, bytes);
    if (!slot->mappedPixels) {
        emit captureError(QStringLiteral("failed to map gRPC MMAP file"));
        return false;
    }

    const QString handle = QUrl::fromLocalFile(slot->file.fileName()).toString(QUrl::FullyEncoded);
    appendGrpcFrame(&slot->requestBody,
                    buildMmapImageFormatRequest(m_requestedWidth, m_requestedHeight, handle));
    return true;
}

void GrpcMmapFramebufferCapture::closeMmapSlots() {
    if (m_slot.mappedPixels) {
        m_slot.file.unmap(m_slot.mappedPixels);
        m_slot.mappedPixels = nullptr;
    }
    m_slot.file.close();
    m_slot.mappedSize = 0;
    m_slot.requestBody.clear();
}

void GrpcMmapFramebufferCapture::startStream() {
    if (!m_running || m_reply) return;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(QStringLiteral(
        "/android.emulation.control.EmulatorController/streamScreenshot"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("TE", "trailers");
    request.setRawHeader("grpc-encoding", "identity");
    request.setRawHeader("grpc-accept-encoding", "identity");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::AlwaysNetwork);
    request.setAttribute(QNetworkRequest::CacheSaveControlAttribute, false);

    m_streamBuffer.clear();
    m_reply = m_network.post(request, m_slot.requestBody);
    if (!m_reply) return;
    connect(m_reply, &QNetworkReply::readyRead, this, &GrpcMmapFramebufferCapture::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &GrpcMmapFramebufferCapture::onStreamFinished);
}

void GrpcMmapFramebufferCapture::restartStream() {
    if (!m_running) return;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply.clear();
    }
    m_streamBuffer.clear();
    QTimer::singleShot(200, this, [this]() { startStream(); });
}

void GrpcMmapFramebufferCapture::onReadyRead() {
    if (!m_reply) return;
    m_streamBuffer.append(m_reply->readAll());
    parseStreamFrames();
}

void GrpcMmapFramebufferCapture::onStreamFinished() {
    if (!m_reply) return;
    m_reply->deleteLater();
    m_reply.clear();
    if (m_running)
        QTimer::singleShot(200, this, [this]() { startStream(); });
}

void GrpcMmapFramebufferCapture::checkStall() {
    if (!m_running || !m_paceTimer.isValid()) return;
    if (m_lastFrameMs == 0) return;
    if (m_paceTimer.elapsed() - m_lastFrameMs > kStallTimeoutMs)
        restartStream();
}

void GrpcMmapFramebufferCapture::parseStreamFrames() {
    while (m_streamBuffer.size() >= 5) {
        if (static_cast<quint8>(m_streamBuffer.at(0)) != kGrpcUncompressed) {
            restartStream();
            return;
        }
        const auto length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(m_streamBuffer.constData() + 1));
        if (length > kMaxFrameBytes) {
            restartStream();
            return;
        }
        if (m_streamBuffer.size() < static_cast<int>(length) + 5)
            return;

        const QByteArray payload = m_streamBuffer.mid(5, static_cast<int>(length));
        m_streamBuffer.remove(0, static_cast<int>(length) + 5);

        ImageMetadata metadata;
        if (parseImageMetadata(payload, &metadata)) {
            m_lastFrameMs = m_paceTimer.elapsed();
            emitMappedFrame(&m_slot, metadata);
        }
    }
}

QByteArray GrpcMmapFramebufferCapture::buildMmapImageFormatRequest(int width,
                                                                   int height,
                                                                   const QString &handle) {
    QByteArray transport;
    appendVarintField(&transport, 1, 1); // ImageTransport.MMAP
    appendBytesField(&transport, 2, handle.toUtf8());

    QByteArray payload;
    appendVarintField(&payload, 1, kRgba8888);
    appendVarintField(&payload, 3, static_cast<quint64>(width));
    appendVarintField(&payload, 4, static_cast<quint64>(height));
    appendBytesField(&payload, 6, transport);
    return payload;
}

bool GrpcMmapFramebufferCapture::skipField(const QByteArray &data, int *offset, int wireType) {
    quint64 ignored = 0;
    switch (wireType) {
    case 0:
        return GrpcFramebufferCapture::readVarint(data, offset, &ignored);
    case 1:
        if (data.size() - *offset < 8) return false;
        *offset += 8;
        return true;
    case 2:
        if (!GrpcFramebufferCapture::readVarint(data, offset, &ignored)) return false;
        if (ignored > static_cast<quint64>(data.size() - *offset)) return false;
        *offset += static_cast<int>(ignored);
        return true;
    case 5:
        if (data.size() - *offset < 4) return false;
        *offset += 4;
        return true;
    default:
        return false;
    }
}

bool GrpcMmapFramebufferCapture::parseImageMetadata(const QByteArray &payload,
                                                    ImageMetadata *metadata) {
    int offset = 0;
    while (offset < payload.size()) {
        quint64 tag = 0;
        if (!GrpcFramebufferCapture::readVarint(payload, &offset, &tag)) return false;
        const int field = static_cast<int>(tag >> 3);
        const int wire = static_cast<int>(tag & 0x07);

        if (field == 1 && wire == 2) {
            quint64 len = 0;
            if (!GrpcFramebufferCapture::readVarint(payload, &offset, &len)) return false;
            if (len > static_cast<quint64>(payload.size() - offset)) return false;
            const QByteArray fmt(payload.constData() + offset, static_cast<qsizetype>(len));
            offset += static_cast<int>(len);
            int fmtOffset = 0;
            while (fmtOffset < fmt.size()) {
                quint64 fmtTag = 0;
                if (!GrpcFramebufferCapture::readVarint(fmt, &fmtOffset, &fmtTag)) return false;
                const int fmtField = static_cast<int>(fmtTag >> 3);
                const int fmtWire = static_cast<int>(fmtTag & 0x07);
                quint64 value = 0;
                if ((fmtField == 1 || fmtField == 3 || fmtField == 4) && fmtWire == 0) {
                    if (!GrpcFramebufferCapture::readVarint(fmt, &fmtOffset, &value)) return false;
                    if (fmtField == 1) metadata->format = static_cast<int>(value);
                    if (fmtField == 3) metadata->width = static_cast<int>(value);
                    if (fmtField == 4) metadata->height = static_cast<int>(value);
                    continue;
                }
                if (!skipField(fmt, &fmtOffset, fmtWire)) return false;
            }
            continue;
        }

        if ((field == 2 || field == 3 || field == 5) && wire == 0) {
            quint64 value = 0;
            if (!GrpcFramebufferCapture::readVarint(payload, &offset, &value)) return false;
            if (field == 2 && metadata->width == 0) metadata->width = static_cast<int>(value);
            if (field == 3 && metadata->height == 0) metadata->height = static_cast<int>(value);
            if (field == 5) metadata->sequence = static_cast<quint32>(value);
            continue;
        }

        if (!skipField(payload, &offset, wire)) return false;
    }
    return metadata->width > 0 && metadata->height > 0 &&
           (metadata->format == kRgba8888 || metadata->format == kRgb888);
}

bool GrpcMmapFramebufferCapture::emitMappedFrame(MmapSlot *slot,
                                                 const ImageMetadata &metadata) {
    const int bytesPerPixel = metadata.format == kRgb888 ? 3 : 4;
    const qsizetype frameBytes = static_cast<qsizetype>(metadata.width) *
                                 static_cast<qsizetype>(metadata.height) * bytesPerPixel;
    if (!slot || !slot->mappedPixels || frameBytes <= 0 || frameBytes > slot->mappedSize)
        return false;

    const bool contentChanged = !m_hasLastSequence || metadata.sequence != m_lastSequence;
    m_hasLastSequence = true;
    m_lastSequence = metadata.sequence;
    emit streamFrameReceived(contentChanged);
    if (!contentChanged)
        return true;

    const int stride = metadata.width * bytesPerPixel;
    const QSize size(metadata.width, metadata.height);
    if (metadata.format == kRgba8888 &&
        publishRgbaTextureFrame(slot->mappedPixels, stride, size)) {
        return true;
    }

    QImage image(metadata.width, metadata.height, QImage::Format_RGBA8888);
    for (int y = 0; y < metadata.height; ++y) {
        const uchar *src = slot->mappedPixels + y * stride;
        uchar *dst = image.scanLine(y);
        if (metadata.format == kRgb888) {
            for (int x = 0; x < metadata.width; ++x) {
                dst[x * 4 + 0] = src[x * 3 + 0];
                dst[x * 4 + 1] = src[x * 3 + 1];
                dst[x * 4 + 2] = src[x * 3 + 2];
                dst[x * 4 + 3] = 0xff;
            }
        } else {
            memcpy(dst, src, static_cast<size_t>(stride));
        }
    }
    emit frameReady(image);
    return true;
}

bool GrpcMmapFramebufferCapture::ensureTexturePublisher(const QSize &size) {
    if (m_texturePublishFailed || !size.isValid())
        return false;
    if (m_texturePublisher && m_texturePublisherSize == size)
        return true;

    if (m_texturePublisher) {
        m_texturePublisher->stop();
        m_texturePublisher.reset();
    }

    SharedD3D11TexturePublisher::Config config;
    config.metadataName = localObjectName(QStringLiteral("Meta"));
    config.textureName = localObjectName(QStringLiteral("Texture"));
    config.frameEventName = localObjectName(QStringLiteral("Frame"));
    config.size = size;
    config.dxgiFormat = kDxgiFormatRgba8888;
    config.hasAlpha = true;

    auto publisher = std::make_unique<SharedD3D11TexturePublisher>(config);
    QString error;
    if (!publisher->start(&error)) {
        m_texturePublishFailed = true;
        emit captureError(QStringLiteral("gRPC MMAP D3D11 texture publisher unavailable; using QImage fallback: %1")
                              .arg(error));
        return false;
    }

    m_texturePublisher = std::move(publisher);
    m_texturePublisherSize = size;
    qDebug() << "gRPC MMAP D3D11 texture publisher started at"
             << size.width() << "x" << size.height();
    return true;
}

bool GrpcMmapFramebufferCapture::publishRgbaTextureFrame(const uchar *data,
                                                         int bytesPerLine,
                                                         const QSize &size) {
    if (!ensureTexturePublisher(size))
        return false;

    QString error;
    if (!m_texturePublisher->publishBgraFrame(data, bytesPerLine, &error)) {
        m_texturePublishFailed = true;
        emit captureError(QStringLiteral("gRPC MMAP D3D11 texture publish failed; using QImage fallback: %1")
                              .arg(error));
        return false;
    }

    emit sharedD3D11TextureReady(m_texturePublisher->textureName(),
                                 size,
                                 m_texturePublisher->sequence(),
                                 true);
    return true;
}

} // namespace chimera::graphics
