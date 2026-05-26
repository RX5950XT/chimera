#pragma once

#include "FramebufferCapture.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QNetworkAccessManager>
#include <QSet>
#include <QString>
#include <QTimer>

class QNetworkReply;

namespace chimera::graphics {

// Captures the guest framebuffer from the Android emulator's gRPC service.
//
// The emulator's server-streaming `streamScreenshot` RPC is effectively
// throttled (~0.1 fps observed), so this backend instead polls the unary
// `getScreenshot` RPC with several requests kept in flight. Pipelining lets
// the emulator grab frame N+1 while frame N is still transferring, which
// lifts the sustained rate near display cadence at moderate capture sizes.
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
    void notifyInputActivity();

    static QByteArray buildImageFormatRequest(int width, int height, int displayId = 0);
    static void appendVarint(QByteArray *out, quint64 value);
    static bool readVarint(const QByteArray &data, int *offset, quint64 *value);

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

    void sendRequest();
    // Dispatches the next request, paced to the target frame interval so the
    // capture loop never busy-polls (which pegs both the host and emulator CPU).
    void scheduleNext();
    void onReplyFinished(QNetworkReply *reply);
    // Refills missing pipeline slots after a long frame gap without aborting
    // in-flight requests; transfer timeouts reclaim genuinely hung replies.
    void restartPipeline();
    // Watchdog tick: if no frame has arrived for too long the pipeline has
    // stalled (hung HTTP/2 streams) — restart it so capture self-heals.
    void checkStall();
    QImage imageFromTopDown(const DecodedImage &decoded) const;
    quint64 frameFingerprint(const DecodedImage &decoded) const;
    int currentIntervalMs(qint64 now) const;

    QString m_host;
    int m_port = 0;
    int m_requestedWidth = 0;
    int m_requestedHeight = 0;
    bool m_running = false;
    // Number of concurrent getScreenshot requests kept in flight. Each request
    // triggers a GPU readback on the emulator; too many in flight makes those
    // readbacks contend with guest rendering and with each other, which is what
    // dragged sustained capture down. Depth 3 pipelines enough to hide one
    // round-trip's latency without piling readbacks on the emulator.
    int m_pipelineDepth = 3;
    QNetworkAccessManager m_network;
    QByteArray m_requestBody;
    QSet<QNetworkReply *> m_replies;
    // Frame pacing: dispatch requests on a fixed cadence (m_intervalMs from the
    // base class) instead of re-firing instantly on every reply.
    QElapsedTimer m_paceTimer;
    qint64 m_nextDispatchMs = 0;
    // Stall watchdog: every getScreenshot reply stamps m_lastFrameMs; if the
    // gap exceeds m_stallTimeoutMs the pipeline is restarted. Only armed once
    // the first frame has arrived (boot-time gaps are normal and handled by
    // the retry timer in main.cpp).
    QTimer m_watchdog;
    qint64 m_lastFrameMs = 0;
    bool m_everReceived = false;
    int m_stallTimeoutMs = 2000;
    bool m_hasLastFingerprint = false;
    quint64 m_lastFingerprint = 0;
    int m_duplicateStreak = 0;
    qint64 m_interactiveUntilMs = 0;
};

} // namespace chimera::graphics
