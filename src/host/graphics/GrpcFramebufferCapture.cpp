#include "GrpcFramebufferCapture.h"

#include <QDebug>
#include <QImage>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QtEndian>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <utility>

namespace chimera::graphics {

namespace {

constexpr quint8 kGrpcUncompressed = 0;
constexpr int kMaxFrameBytes = 64 * 1024 * 1024;
constexpr int kIdleCaptureIntervalMs = 50;
constexpr int kInteractiveWindowMs = 2000;
constexpr int kDuplicateFramesBeforeIdle = 6;

void appendVarintField(QByteArray *out, int fieldNumber, quint64 value) {
    GrpcFramebufferCapture::appendVarint(out, static_cast<quint64>(fieldNumber) << 3);
    GrpcFramebufferCapture::appendVarint(out, value);
}

} // namespace

GrpcFramebufferCapture::GrpcFramebufferCapture(QString host, int port, int requestedWidth,
                                               int requestedHeight, QObject *parent)
    : FramebufferCapture(parent),
      m_host(std::move(host)),
      m_port(port),
      m_requestedWidth(normalizedCaptureSize(requestedWidth, requestedHeight).width()),
      m_requestedHeight(normalizedCaptureSize(requestedWidth, requestedHeight).height()) {
    m_watchdog.setInterval(1000);
    connect(&m_watchdog, &QTimer::timeout, this, &GrpcFramebufferCapture::checkStall);
}

GrpcFramebufferCapture::~GrpcFramebufferCapture() {
    stop();
}

bool GrpcFramebufferCapture::start() {
    if (m_running) return true;

    m_requestBody.clear();
    appendGrpcFrame(&m_requestBody,
                    buildImageFormatRequest(m_requestedWidth, m_requestedHeight));

    m_running = true;
    m_paceTimer.start();
    m_nextDispatchMs = 0;
    m_lastFrameMs = 0;
    m_everReceived = false;
    m_hasLastFingerprint = false;
    m_lastFingerprint = 0;
    m_duplicateStreak = 0;
    m_interactiveUntilMs = kInteractiveWindowMs;
    // Prime the pipeline with paced dispatches. scheduleNext() staggers them
    // by the frame interval, so the requests don't all fire at once and the
    // capture loop settles into a steady ~target-FPS cadence.
    for (int i = 0; i < m_pipelineDepth; ++i)
        scheduleNext();
    m_watchdog.start();
    return true;
}

void GrpcFramebufferCapture::checkStall() {
    // The pipeline can wedge if the HTTP/2 streams hang (observed: every
    // in-flight getScreenshot stops completing, freezing capture entirely).
    // Once the first frame has arrived, treat a long frame gap as a stall and
    // rebuild the pipeline so capture recovers on its own.
    if (!m_running || !m_everReceived) return;
    if (m_paceTimer.elapsed() - m_lastFrameMs > m_stallTimeoutMs) {
        qWarning() << "[GrpcFramebufferCapture] capture stalled (no frame for"
                   << (m_paceTimer.elapsed() - m_lastFrameMs) << "ms) — restarting pipeline";
        restartPipeline();
    }
}

void GrpcFramebufferCapture::restartPipeline() {
    if (!m_running) return;
    // Do NOT abort in-flight requests here. Aborting every reply and re-priming
    // the full depth at once dumps a burst of duplicate getScreenshot calls on
    // the emulator — which is already slow, hence the stall — making it slower
    // still. That feedback loop is what permanently collapsed capture to ~5fps.
    // A genuinely hung request already self-aborts via setTransferTimeout and is
    // replaced through the error path; here we only top the pipeline back up to
    // depth so a transient gap can recover without a thundering herd.
    const qint64 now = m_paceTimer.elapsed();
    m_lastFrameMs = now; // grant a fresh stall window before checking again
    if (m_nextDispatchMs < now) m_nextDispatchMs = now;
    for (int i = static_cast<int>(m_replies.size()); i < m_pipelineDepth; ++i)
        scheduleNext();
}

void GrpcFramebufferCapture::scheduleNext() {
    if (!m_running) return;
    // Pace dispatches to the target frame interval so the capture loop never
    // busy-polls. A fast machine is capped near the display refresh rate
    // instead of pegging a host CPU core (and the emulator's) flat out; a slow
    // machine simply runs the pipeline back-to-back and self-throttles.
    const qint64 now = m_paceTimer.elapsed();
    const int interval = currentIntervalMs(now);
    qint64 slot = m_nextDispatchMs;
    if (slot < now) slot = now; // fell behind — dispatch now, never burst to catch up
    m_nextDispatchMs = slot + interval;
    const int delay = static_cast<int>(slot - now);
    if (delay <= 0)
        sendRequest();
    else
        QTimer::singleShot(delay, this, [this]() { sendRequest(); });
}

void GrpcFramebufferCapture::notifyInputActivity() {
    if (!m_running || !m_paceTimer.isValid()) return;
    const qint64 now = m_paceTimer.elapsed();
    m_interactiveUntilMs = now + kInteractiveWindowMs;
    m_duplicateStreak = 0;
    if (m_nextDispatchMs > now)
        m_nextDispatchMs = now;
    if (m_replies.size() < m_pipelineDepth)
        scheduleNext();
}

void GrpcFramebufferCapture::stop() {
    m_running = false;
    m_watchdog.stop();
    const auto replies = m_replies;
    m_replies.clear();
    for (QNetworkReply *reply : replies) {
        if (reply) {
            reply->abort();
            reply->deleteLater();
        }
    }
    m_requestBody.clear();
}

void GrpcFramebufferCapture::sendRequest() {
    if (!m_running) return;

    QUrl url;
    url.setScheme(QStringLiteral("http"));
    url.setHost(m_host);
    url.setPort(m_port);
    url.setPath(QStringLiteral(
        "/android.emulation.control.EmulatorController/getScreenshot"));

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
    // Abort a request that makes no progress for this long. A hung HTTP/2
    // stream would otherwise occupy a pipeline slot forever; the timeout turns
    // it into a normal error that the retry path replaces.
    request.setTransferTimeout(std::chrono::milliseconds(m_stallTimeoutMs));

    QNetworkReply *reply = m_network.post(request, m_requestBody);
    if (!reply) return;
    m_replies.insert(reply);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply]() { onReplyFinished(reply); });
}

void GrpcFramebufferCapture::onReplyFinished(QNetworkReply *reply) {
    if (!reply) return;
    const bool tracked = m_replies.remove(reply);
    reply->deleteLater();

    // A stale reply from a previous stream — ignore it entirely.
    if (!tracked || !m_running) return;

    if (reply->error() == QNetworkReply::NoError) {
        // A completed reply proves the pipeline is alive — feed the watchdog.
        m_lastFrameMs = m_paceTimer.elapsed();
        m_everReceived = true;
        const QByteArray body = reply->readAll();
        DecodedImage decoded;
        // gRPC unary response: [compressed:1][length:4 BE][payload].
        if (body.size() >= 5 &&
            static_cast<quint8>(body.at(0)) == kGrpcUncompressed) {
            const auto length = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(body.constData() + 1));
            if (length <= kMaxFrameBytes && body.size() >= static_cast<int>(length) + 5) {
                const QByteArray payload = body.mid(5, static_cast<int>(length));
                if (decodeImageMessage(payload, &decoded)) {
                    const quint64 fingerprint = frameFingerprint(decoded);
                    const bool contentChanged =
                        !m_hasLastFingerprint || fingerprint != m_lastFingerprint;
                    m_hasLastFingerprint = true;
                    m_lastFingerprint = fingerprint;
                    if (contentChanged) {
                        m_duplicateStreak = 0;
                        m_interactiveUntilMs = m_paceTimer.elapsed() + kInteractiveWindowMs;
                    } else {
                        ++m_duplicateStreak;
                    }
                    emit streamFrameReceived(contentChanged);
                    if (contentChanged)
                        emit frameReady(imageFromTopDown(decoded));
                }
                // else: empty pre-boot frame — valid, just skip.
            }
        }
        // One request completed — schedule a paced replacement to keep the
        // pipeline full without busy-polling.
        scheduleNext();
    } else {
        // A transient error (emulator gRPC not up yet) must not kill the
        // stream or spin a tight retry loop — back off briefly, then refill.
        QTimer::singleShot(200, this, [this]() { sendRequest(); });
    }
}

QByteArray GrpcFramebufferCapture::buildImageFormatRequest(int width, int height, int displayId) {
    QByteArray payload;
    appendVarintField(&payload, 1, 2); // RGB888: 25% less bandwidth than RGBA8888.
    if (width > 0) appendVarintField(&payload, 3, static_cast<quint64>(width));
    if (height > 0) appendVarintField(&payload, 4, static_cast<quint64>(height));
    if (displayId > 0) appendVarintField(&payload, 5, static_cast<quint64>(displayId));
    return payload;
}

QSize GrpcFramebufferCapture::normalizedCaptureSize(int requestedWidth, int requestedHeight) {
    return QSize((std::max)(requestedWidth, kMinimumCaptureWidth),
                 (std::max)(requestedHeight, kMinimumCaptureHeight));
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
            quint64 nestedLen = 0;
            if (!readVarint(payload, &offset, &nestedLen)) return false;
            if (nestedLen > static_cast<quint64>(payload.size() - offset)) return false;
            const QByteArray nested(payload.constData() + offset,
                                    static_cast<qsizetype>(nestedLen));
            offset += static_cast<int>(nestedLen);
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
            quint64 pixelLen = 0;
            if (!readVarint(payload, &offset, &pixelLen)) return false;
            if (pixelLen > static_cast<quint64>(payload.size() - offset)) return false;
            pixels = QByteArray(payload.constData() + offset,
                                static_cast<qsizetype>(pixelLen));
            offset += static_cast<int>(pixelLen);
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

quint64 GrpcFramebufferCapture::frameFingerprint(const DecodedImage &decoded) const {
    quint64 hash = 1469598103934665603ull;
    auto mix = [&hash](quint64 value) {
        for (int i = 0; i < 8; ++i) {
            hash ^= (value >> (i * 8)) & 0xffu;
            hash *= 1099511628211ull;
        }
    };

    mix(static_cast<quint64>(decoded.width));
    mix(static_cast<quint64>(decoded.height));
    mix(static_cast<quint64>(decoded.format));
    mix(static_cast<quint64>(decoded.pixels.size()));

    const int size = decoded.pixels.size();
    if (size <= 0) return hash;

    const auto *bytes = reinterpret_cast<const uchar *>(decoded.pixels.constData());
    int i = 0;
    for (; i + 8 <= size; i += 8) {
        quint64 word = 0;
        memcpy(&word, bytes + i, sizeof(word));
        mix(word);
    }
    for (; i < size; ++i)
        mix(bytes[i]);

    return hash;
}

int GrpcFramebufferCapture::currentIntervalMs(qint64 now) const {
    const int activeInterval = m_intervalMs > 0 ? m_intervalMs : 16;
    const bool interactive = now <= m_interactiveUntilMs;
    if (interactive || m_duplicateStreak < kDuplicateFramesBeforeIdle)
        return activeInterval;
    return (std::max)(activeInterval, kIdleCaptureIntervalMs);
}

} // namespace chimera::graphics
