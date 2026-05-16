#include "GrpcFramebufferCapture.h"

#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QtEndian>
#include <cstring>
#include <utility>

namespace chimera::graphics {

namespace {

constexpr quint8 kGrpcUncompressed = 0;
constexpr int kMaxFrameBytes = 64 * 1024 * 1024;

void appendVarintField(QByteArray *out, int fieldNumber, quint64 value) {
    GrpcFramebufferCapture::appendVarint(out, static_cast<quint64>(fieldNumber) << 3);
    GrpcFramebufferCapture::appendVarint(out, value);
}

bool readLengthDelimited(const QByteArray &data, int *offset, QByteArray *payload) {
    quint64 length = 0;
    if (!GrpcFramebufferCapture::readVarint(data, offset, &length)) return false;
    if (length > static_cast<quint64>(data.size() - *offset)) return false;
    *payload = QByteArray(data.constData() + *offset, static_cast<qsizetype>(length));
    *offset += static_cast<int>(length);
    return true;
}

} // namespace

GrpcFramebufferCapture::GrpcFramebufferCapture(QString host, int port, int requestedWidth,
                                               int requestedHeight, QObject *parent)
    : FramebufferCapture(parent),
      m_host(std::move(host)),
      m_port(port),
      m_requestedWidth(requestedWidth),
      m_requestedHeight(requestedHeight) {}

GrpcFramebufferCapture::~GrpcFramebufferCapture() {
    stop();
}

bool GrpcFramebufferCapture::start() {
    if (m_running) return true;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(QStringLiteral("/android.emulation.control.EmulatorController/streamScreenshot"));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/grpc"));
    request.setRawHeader("TE", "trailers");
    request.setRawHeader("grpc-encoding", "identity");
    request.setRawHeader("grpc-accept-encoding", "identity");
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, true);
    request.setAttribute(QNetworkRequest::Http2DirectAttribute, true);

    QByteArray body;
    appendGrpcFrame(&body, buildImageFormatRequest(m_requestedWidth, m_requestedHeight));

    m_buffer.clear();
    m_reply = m_network.post(request, body);
    if (!m_reply) return false;

    connect(m_reply, &QNetworkReply::readyRead, this, &GrpcFramebufferCapture::onReadyRead);
    connect(m_reply, &QNetworkReply::finished, this, &GrpcFramebufferCapture::onFinished);
    connect(m_reply, &QNetworkReply::errorOccurred, this, &GrpcFramebufferCapture::onError);

    m_running = true;
    return true;
}

void GrpcFramebufferCapture::stop() {
    m_running = false;
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_buffer.clear();
}

QByteArray GrpcFramebufferCapture::buildImageFormatRequest(int width, int height, int displayId) {
    QByteArray payload;
    appendVarintField(&payload, 1, 2); // RGB888: 25% less bandwidth than RGBA8888.
    if (width > 0) appendVarintField(&payload, 3, static_cast<quint64>(width));
    if (height > 0) appendVarintField(&payload, 4, static_cast<quint64>(height));
    if (displayId > 0) appendVarintField(&payload, 5, static_cast<quint64>(displayId));
    return payload;
}

void GrpcFramebufferCapture::appendGrpcFrame(QByteArray *out, const QByteArray &payload) {
    out->append(static_cast<char>(kGrpcUncompressed));
    char length[4];
    qToBigEndian<quint32>(static_cast<quint32>(payload.size()), length);
    out->append(length, 4);
    out->append(payload);
}

void GrpcFramebufferCapture::appendVarint(QByteArray *out, quint64 value) {
    while (value >= 0x80) {
        out->append(static_cast<char>((value & 0x7f) | 0x80));
        value >>= 7;
    }
    out->append(static_cast<char>(value));
}

bool GrpcFramebufferCapture::readVarint(const QByteArray &data, int *offset, quint64 *value) {
    quint64 result = 0;
    int shift = 0;
    while (*offset < data.size() && shift <= 63) {
        const auto byte = static_cast<quint8>(data.at((*offset)++));
        result |= static_cast<quint64>(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            *value = result;
            return true;
        }
        shift += 7;
    }
    return false;
}

bool GrpcFramebufferCapture::skipField(const QByteArray &data, int *offset, int wireType) {
    quint64 ignored = 0;
    switch (wireType) {
    case 0:
        return readVarint(data, offset, &ignored);
    case 1:
        if (data.size() - *offset < 8) return false;
        *offset += 8;
        return true;
    case 2:
        if (!readVarint(data, offset, &ignored)) return false;
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

bool GrpcFramebufferCapture::parseImageFormat(const QByteArray &payload, int *width, int *height,
                                              int *format) {
    int offset = 0;
    while (offset < payload.size()) {
        quint64 tag = 0;
        if (!readVarint(payload, &offset, &tag)) return false;
        const int field = static_cast<int>(tag >> 3);
        const int wire = static_cast<int>(tag & 0x07);
        quint64 value = 0;
        if ((field == 1 || field == 3 || field == 4) && wire == 0) {
            if (!readVarint(payload, &offset, &value)) return false;
            if (field == 1) *format = static_cast<int>(value);
            if (field == 3) *width = static_cast<int>(value);
            if (field == 4) *height = static_cast<int>(value);
            continue;
        }
        if (!skipField(payload, &offset, wire)) return false;
    }
    return true;
}

bool GrpcFramebufferCapture::decodeImageMessage(const QByteArray &payload, DecodedImage *out) {
    int width = 0;
    int height = 0;
    int format = 1;
    QByteArray pixels;
    int offset = 0;
    while (offset < payload.size()) {
        quint64 tag = 0;
        if (!readVarint(payload, &offset, &tag)) return false;
        const int field = static_cast<int>(tag >> 3);
        const int wire = static_cast<int>(tag & 0x07);

        if (field == 1 && wire == 2) {
            QByteArray nested;
            if (!readLengthDelimited(payload, &offset, &nested)) return false;
            if (!parseImageFormat(nested, &width, &height, &format)) return false;
            continue;
        }

        if ((field == 2 || field == 3) && wire == 0) {
            quint64 value = 0;
            if (!readVarint(payload, &offset, &value)) return false;
            if (field == 2 && width == 0) width = static_cast<int>(value);
            if (field == 3 && height == 0) height = static_cast<int>(value);
            continue;
        }

        if (field == 4 && wire == 2) {
            if (!readLengthDelimited(payload, &offset, &pixels)) return false;
            continue;
        }

        if (!skipField(payload, &offset, wire)) return false;
    }

    if (width <= 0 || height <= 0 || pixels.isEmpty()) return false;
    const int bytesPerPixel = (format == 2) ? 3 : 4;
    if (format != 1 && format != 2) return false;
    const auto expected = static_cast<qint64>(width) * static_cast<qint64>(height) * bytesPerPixel;
    if (expected <= 0 || expected > kMaxFrameBytes || pixels.size() != expected) return false;

    out->width = width;
    out->height = height;
    out->format = format;
    out->pixels = std::move(pixels);
    return true;
}

void GrpcFramebufferCapture::onReadyRead() {
    if (!m_reply) return;
    m_buffer.append(m_reply->readAll());
    parseBufferedFrames();
}

void GrpcFramebufferCapture::parseBufferedFrames() {
    while (m_buffer.size() >= 5) {
        const auto compressed = static_cast<quint8>(m_buffer.at(0));
        const auto length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar*>(m_buffer.constData() + 1));
        if (compressed != kGrpcUncompressed) {
            emit captureError(QStringLiteral("gRPC compressed frames are not supported"));
            stop();
            return;
        }
        if (length > kMaxFrameBytes) {
            emit captureError(QStringLiteral("gRPC frame is too large"));
            stop();
            return;
        }
        if (m_buffer.size() < static_cast<int>(length) + 5) return;

        const QByteArray payload = m_buffer.mid(5, static_cast<int>(length));
        m_buffer.remove(0, static_cast<int>(length) + 5);

        DecodedImage decoded;
        if (!decodeImageMessage(payload, &decoded)) {
            continue; // Empty pre-boot frames are valid.
        }
        emit frameReady(imageFromTopDown(decoded));
    }
}

QImage GrpcFramebufferCapture::imageFromTopDown(const DecodedImage &decoded) const {
    const int bytesPerPixel = (decoded.format == 2) ? 3 : 4;
    const auto imageFormat = (decoded.format == 2) ? QImage::Format_RGB888 : QImage::Format_RGBA8888;
    QImage image(decoded.width, decoded.height, imageFormat);
    const int stride = decoded.width * bytesPerPixel;
    for (int y = 0; y < decoded.height; ++y) {
        const char *src = decoded.pixels.constData() + y * stride;
        memcpy(image.scanLine(y), src, static_cast<size_t>(stride));
    }
    return image;
}

void GrpcFramebufferCapture::onFinished() {
    if (!m_running) return;
    const QString error = m_reply ? m_reply->errorString() : QStringLiteral("unknown error");
    if (m_reply) {
        m_reply->deleteLater();
        m_reply = nullptr;
    }
    m_running = false;
    emit captureError(QStringLiteral("gRPC stream ended: ") + error);
}

void GrpcFramebufferCapture::onError() {
    if (!m_running || !m_reply) return;
    const QString error = m_reply->errorString();
    stop();
    emit captureError(QStringLiteral("gRPC capture error: ") + error);
}

} // namespace chimera::graphics
